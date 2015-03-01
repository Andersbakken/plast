#include "Remote.h"
#include "Daemon.h"
#include "CompilerVersion.h"
#include <rct/Log.h>
#include <unistd.h>

Remote::Remote()
    : mNextId(0), mRequestedCount(0), mRescheduleTimeout(-1)
{
}

Remote::~Remote()
{
}

void Remote::init()
{
    mServer.newConnection().connect([this](SocketServer* server) {
            error() << "Got a connection";
            SocketClient::SharedPtr client;
            for (;;) {
                client = server->nextConnection();
                if (!client)
                    return;
                error() << "remote client connected";
                addClient(client);
            }
        });

    const Daemon::Options& opts = Daemon::instance()->options();
    mPreprocessor.setCount(opts.preprocessCount);
    mRescheduleTimer.restart(opts.rescheduleCheck);
    mRescheduleTimeout = opts.rescheduleTimeout;

    if (!mServer.listen(opts.localPort)) {
        error() << "Unable to tcp listen";
        abort();
    }
    error() << "Listening" << opts.localPort;

    mConnection.newMessage().connect([this](const std::shared_ptr<Message>& message, Connection* conn) {
            error() << "Got a message" << message->messageId() << __LINE__;
            switch (message->messageId()) {
            case HasJobsMessage::MessageId:
                handleHasJobsMessage(std::static_pointer_cast<HasJobsMessage>(message), conn);
                break;
            default:
                error("Unexpected message Remote::init: %d", message->messageId());
                break;
            }
        });
    mConnection.finished().connect(std::bind([](){ error() << "server finished connection"; EventLoop::eventLoop()->quit(); }));
    mConnection.disconnected().connect(std::bind([](){ error() << "server closed connection"; EventLoop::eventLoop()->quit(); }));
    if (!mConnection.connectTcp(opts.serverHost, opts.serverPort)) {
        error("Can't seem to connect to server");
        abort();
    }
    {
        String hn;
        hn.resize(sysconf(_SC_HOST_NAME_MAX));
        if (gethostname(hn.data(), hn.size()) == 0) {
            hn.resize(strlen(hn.constData()));
            mConnection.send(PeerMessage(hn, opts.localPort));
        }
    }

    mRescheduleTimer.timeout().connect([this](Timer*) {
            //error() << "checking for reschedule!!!";
            const uint64_t now = Rct::monoMs();
            // reschedule outstanding jobs only, local will get to pending jobs eventually
#warning Should we reschedule pending remote jobs?
            bool done = false;
            auto it = mBuildingByTime.begin();
            while (it != mBuildingByTime.end() && !done) {
                const uint64_t started = it->first;
                auto sub = it->second.begin();
                while (sub != it->second.end()) {
                    assert(mBuildingById.contains((*sub)->jobid));
                    error() << "considering" << now << started << (now - started) << mRescheduleTimeout;
                    if (now - started < mRescheduleTimeout) {
                        done = true;
                        break;
                    }
                    error() << "job has expired";
                    // reschedule
                    Job::SharedPtr job = (*sub)->job.lock();
                    if (job) {
                        error() << "job still exists" << job->status() << job->id();
                        if (job->status() != Job::RemotePending) {
#warning should we reschedule jobs we have partially received in case the connection times out?
                            // can only reschedule remotepending jobs
                            ++it;
                            continue;
                        }
                        error() << "rescheduling" << job->id() << "now" << now << "started" << started;
                        job->updateStatus(Job::Idle);
                        job->increaseSerial();
                        job->start();
                    }
                    error() << "removed job 1" << (*sub)->jobid;
                    mBuildingById.erase((*sub)->jobid);
                    sub = it->second.erase(sub);
                    if (it->second.isEmpty()) {
                        mBuildingByTime.erase(it++);
                        break;
                    }
                }
            }
            if (!mPending.isEmpty()) {
                //mConnection.send(HasJobsMessage(mPending.size(), Daemon::instance()->options().localPort));
                for (const auto& p : mPending) {
                    mConnection.send(HasJobsMessage(p.first.type, p.first.major, p.first.target, p.second.size(),
                                                    Daemon::instance()->options().localPort));
                }
            }
        });
}

void Remote::handleJobMessage(const JobMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle job message!";
    // let's make a job out of this
    Job::SharedPtr job = Job::create(msg->path(), msg->args(), Job::RemoteJob, msg->remoteName(),
                                     msg->id(), msg->preprocessed(), msg->serial(),
                                     msg->compilerType(), msg->compilerMajor(), msg->compilerTarget());
    job->statusChanged().connect([conn](Job* job, Job::Status status) {
            error() << "remote job status changed" << job << status;
            switch (status) {
            case Job::Compiled:
                conn->send(JobResponseMessage(JobResponseMessage::Compiled, job->remoteId(),
                                              job->serial(), job->objectCode()));
                break;
            case Job::Error:
                conn->send(JobResponseMessage(JobResponseMessage::Error, job->remoteId(),
                                              job->serial(), job->error()));
                break;
            default:
                break;
            }
        });
    job->readyReadStdOut().connect([conn](Job* job) {
            error() << "remote job ready stdout";
            conn->send(JobResponseMessage(JobResponseMessage::Stdout, job->remoteId(),
                                          job->serial(), job->readAllStdOut()));
        });
    job->readyReadStdErr().connect([conn](Job* job) {
            error() << "remote job ready stderr";
            conn->send(JobResponseMessage(JobResponseMessage::Stderr, job->remoteId(),
                                          job->serial(), job->readAllStdErr()));
        });
    job->start();
}

void Remote::handleRequestJobsMessage(const RequestJobsMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle request jobs message" << msg->count();
    // take count jobs
    const plast::CompilerKey k = { msg->compilerType(), msg->compilerMajor(), msg->compilerTarget() };
    auto p = mPending.find(k);
    if (p == mPending.end()) {
        conn->send(LastJobMessage(k.type, k.major, k.target, 0, false));
        return;
    }
    auto& pending = p->second;
    assert(!pending.isEmpty());

    int rem = msg->count();
    bool sent = false, empty = false;
    for (;;) {
        Job::SharedPtr job = pending.front().lock();
        pending.removeFirst();
        if (job) {
            // add job to building map
            std::shared_ptr<Building> b = std::make_shared<Building>(Rct::monoMs(), job->id(), job, conn);
            mBuildingByTime[b->started].append(b);
            mBuildingById[b->jobid] = b;

            assert(job->isPreprocessed());
            // send this job to remote;
            error() << "sending job back";
            job->updateStatus(Job::RemotePending);
            conn->send(JobMessage(job->path(), job->args(), job->id(), job->preprocessed(),
                                  job->serial(), job->remoteName(), job->compilerType(),
                                  job->compilerMajor(), job->compilerTarget()));
            if (!--rem)
                break;
        }
        if (pending.empty())
            break;
    }
    conn->send(LastJobMessage(k.type, k.major, k.target, msg->count() - rem, !pending.empty()));
    if (pending.empty())
        mPending.erase(p);
}

void Remote::handleHasJobsMessage(const HasJobsMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle has jobs message";

    // do we have the compiler in question?
    if (!CompilerVersion::hasCompiler(msg->compilerType(), msg->compilerMajor(), msg->compilerTarget())) {
        error() << "we don't have compiler" << msg->compilerType() << msg->compilerMajor() << msg->compilerTarget();
        return;
    }
    error() << "we have compiler" << msg->compilerType() << msg->compilerMajor() << msg->compilerTarget();

    Connection* remoteConn = 0;
    {
        const Peer key = { msg->peer(), msg->port() };
        Map<Peer, Connection*>::const_iterator peer = mPeersByKey.find(key);
        if (peer == mPeersByKey.end()) {
            // make connection
            SocketClient::SharedPtr client = std::make_shared<SocketClient>();
            client->connect(key.peer, key.port);
            Connection* conn = addClient(client);

            mPeersByKey[key] = conn;
            mPeersByConn[conn] = key;
            remoteConn = conn;

            conn->send(HandshakeMessage(Daemon::instance()->options().localPort));
        } else {
            remoteConn = peer->second;
        }
    }

    assert(remoteConn);

    const ConnectionKey ck = { remoteConn, msg->compilerType(), msg->compilerMajor(), msg->compilerTarget() };
    if (mRequested.contains(ck)) {
        error() << "already asked";
        // we already asked this host for jobs, wait until it gets back to us
        return;
    }

    requestMore(ck);
}

void Remote::handleHandshakeMessage(const HandshakeMessage::SharedPtr& msg, Connection* conn)
{
    const Peer key = { conn->client()->peerName(), msg->port() };
    if (mPeersByKey.contains(key)) {
        // drop the connection
        assert(!mPeersByConn.contains(conn));
        conn->finish();
    } else {
        mPeersByKey[key] = conn;
        mPeersByConn[conn] = key;
    }
}

void Remote::handleJobResponseMessage(const JobResponseMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle job response" << msg->mode() << msg->id();
    Job::SharedPtr job = Job::job(msg->id());
    if (!job) {
        error() << "job not found for response";
        return;
    }
    const Job::Status status = job->status();
    switch (status) {
    case Job::RemotePending:
        job->updateStatus(Job::RemoteReceiving);
        // fall through
    case Job::RemoteReceiving:
        // accept the above statuses
        break;
    default:
        error() << "job no longer remote compiling";
        removeJob(job->id());
        return;
    }
    if (msg->serial() != job->serial()) {
        error() << "job serial doesn't match, rescheduled remote?";
        return;
    }
    switch (msg->mode()) {
    case JobResponseMessage::Stdout:
        job->mStdOut += msg->data();
        job->mReadyReadStdOut(job.get());
        break;
    case JobResponseMessage::Stderr:
        job->mStdErr += msg->data();
        job->mReadyReadStdErr(job.get());
        break;
    case JobResponseMessage::Error:
        removeJob(job->id());
        job->mError = msg->data();
        job->updateStatus(Job::Error);
        Job::finish(job.get());
        break;
    case JobResponseMessage::Compiled:
        error() << "job successfully remote compiled" << job->id();
        removeJob(job->id());
        job->writeFile(msg->data());
        job->updateStatus(Job::Compiled);
        Job::finish(job.get());
        break;
    }
}

void Remote::handleLastJobMessage(const LastJobMessage::SharedPtr& msg, Connection* conn)
{
    error() << "last job msg";
    const ConnectionKey ck = { conn, msg->compilerType(), msg->compilerMajor(), msg->compilerTarget() };

    auto it = mRequested.find(ck);
    assert(it != mRequested.end());
    assert(it->second >= msg->count());
    mRequestedCount -= it->second;
    mRequested.erase(it);

    if (msg->hasMore()) {
        error() << "still has more";
        mHasMore.insert(ck);
    } else {
        error() << "no more" << conn;;
        mHasMore.erase(ck);
    }
    requestMore();
}

void Remote::requestMore()
{
    if (Daemon::instance()->local().availableCount() <= mRequestedCount)
        return;
    for (const auto& it : mHasMore) {
        if (!mRequested.contains(it)) {
            requestMore(it);
            return;
        }
    }
}

void Remote::requestMore(const ConnectionKey& key)
{
    const unsigned int idle = Daemon::instance()->local().availableCount();
    if (idle > mRequestedCount) {
        const int count = std::min<int>(idle - mRequestedCount, 5);
        error() << "asking for" << count << "since" << mRequestedCount << "<" << idle;
        mRequestedCount += count;
        mRequested[key] = count;
        key.conn->send(RequestJobsMessage(key.type, key.major, key.target, count));
    } else {
        error() << "not asking," << mRequestedCount << ">=" << idle;
    }
}

void Remote::removeJob(uint64_t id)
{
    auto idit = mBuildingById.find(id);
    if (idit == mBuildingById.end())
        return;
    error() << "removed job 2" << id;
    //assert(idit->second.use_count() == 2);
    auto tit = mBuildingByTime.find(idit->second->started);
    assert(tit != mBuildingByTime.end());
    mBuildingById.erase(idit);

    auto sit = tit->second.begin();
    const auto send = tit->second.cend();
    while (sit != send) {
        if ((*sit)->jobid == id) {
            tit->second.erase(sit);
            if (tit->second.empty()) {
                mBuildingByTime.erase(tit);
            }
            break;
        }
        ++sit;
    }
}

Job::SharedPtr Remote::take()
{
#warning we should probably only take these after some timeout since we already paid the cost of preprocessing
    // prefer jobs that are not sent out
    while (!mPending.isEmpty()) {
        auto p = mPending.begin();
        assert(!p->second.isEmpty());
        Job::SharedPtr job = p->second.front().lock();
        p->second.removeFirst();
        if (p->second.isEmpty())
            mPending.erase(p);
        if (job)
            return job;
    }
    // take newest pending jobs first, the assumption is that this
    // will be the job that will take the longest to get back to us
    auto time = mBuildingByTime.rbegin();
    while (time != mBuildingByTime.rend()) {
        for (auto cand : time->second) {
            Job::SharedPtr job = cand->job.lock();
            if (job && job->status() == Job::RemotePending) {
#warning should we take jobs we have partially received in case the connection times out?
                // we can take this job since we haven't received any data for it yet
                job->increaseSerial();
                job->updateStatus(Job::Idle);
                const uint64_t id = cand->jobid;
                assert(id == job->id());
                removeJob(id);
                return job;
            }
        }
        ++time;
    }
    return Job::SharedPtr();
}

Connection* Remote::addClient(const SocketClient::SharedPtr& client)
{
    error() << "remote client added";
    Connection* conn = new Connection(client);
    conn->newMessage().connect([this](const std::shared_ptr<Message>& msg, Connection* conn) {
            error() << "Got a message" << msg->messageId() << __LINE__;
            switch (msg->messageId()) {
            case JobMessage::MessageId:
                handleJobMessage(std::static_pointer_cast<JobMessage>(msg), conn);
                break;
            case RequestJobsMessage::MessageId:
                handleRequestJobsMessage(std::static_pointer_cast<RequestJobsMessage>(msg), conn);
                break;
            case HandshakeMessage::MessageId:
                handleHandshakeMessage(std::static_pointer_cast<HandshakeMessage>(msg), conn);
                break;
            case JobResponseMessage::MessageId:
                handleJobResponseMessage(std::static_pointer_cast<JobResponseMessage>(msg), conn);
                break;
            case LastJobMessage::MessageId:
                handleLastJobMessage(std::static_pointer_cast<LastJobMessage>(msg), conn);
                break;
            default:
                error() << "Unexpected message Remote::addClient" << msg->messageId();
                conn->finish(1);
                break;
            }
        });
    conn->disconnected().connect([this](Connection* conn) {
            conn->disconnected().disconnect();
            EventLoop::deleteLater(conn);

            auto ck = mRequested.begin();
            while (ck != mRequested.end()) {
                if (ck->first.conn == conn) {
                    mRequestedCount -= ck->second;
                    mHasMore.erase(ck->first);
                    mRequested.erase(ck++);
                } else {
                    ++ck;
                }
            }
            // go through all pending jobs, we'll need to hard
            // reschedule all jobs from this connection
            {
                auto t = mBuildingByTime.begin();
                while (t != mBuildingByTime.end()) {
                    assert(!t->second.isEmpty());
                    auto b = t->second.begin();
                    while (b != t->second.end()) {
                        if ((*b)->conn == conn) {
                            Job::SharedPtr j = (*b)->job.lock();
                            if (j) {
                                // reschedule
                                assert(j->status() != Job::Compiled);
                                assert(j->id() == (*b)->jobid);

                                b = t->second.erase(b);
                                assert(mBuildingById.contains(j->id()));
                                mBuildingById.erase(j->id());

                                error() << "hard rescheduling" << j->id();
                                j->updateStatus(Job::Idle);
                                j->increaseSerial();
                                j->start();
                                continue;
                            } else {
                                // no job? that's strange. take it out
                                mBuildingById.erase((*b)->jobid);
                                b = t->second.erase(b);
                                continue;
                            }
                        }
                        ++b;
                    }
                    if (t->second.isEmpty()) {
                        mBuildingByTime.erase(t++);
                    } else {
                        ++t;
                    }
                }
            }

            auto itc = mPeersByConn.find(conn);
            if (itc == mPeersByConn.end())
                return;
            const Peer key = itc->second;
            mPeersByConn.erase(itc);
            assert(mPeersByKey.contains(key));
            mPeersByKey.erase(key);

            requestMore();
        });
    return conn;
}

void Remote::post(const Job::SharedPtr& job)
{
    error() << "local post";
    // queue for preprocess if not already done
    const plast::CompilerKey k = { job->compilerType(), job->compilerMajor(), job->compilerTarget() };
    if (!job->isPreprocessed()) {
        job->statusChanged().connect([this, k](Job* job, Job::Status status) {
                if (status == Job::Preprocessed) {
                    error() << "preproc size" << job->preprocessed().size();
                    mPending[k].push_back(job->shared_from_this());
                    // send a HasJobsMessage to the scheduler
                    mConnection.send(HasJobsMessage(k.type, k.major, k.target, mPending[k].size(),
                                                    Daemon::instance()->options().localPort));
                }
            });
        mPreprocessor.preprocess(job);
    } else {
        mPending[k].push_back(job);
        // send a HasJobsMessage to the scheduler
        mConnection.send(HasJobsMessage(k.type, k.major, k.target, mPending[k].size(),
                                        Daemon::instance()->options().localPort));
    }
}
