#include "Remote.h"
#include "Daemon.h"
#include "CompilerVersion.h"
#include <rct/Log.h>
#include <unistd.h>

Remote::Remote()
    : mNextId(0), mRequestedCount(0), mRescheduleTimeout(-1), mReconnectTimeout(1000),
      mMaxPreprocessPending(0), mCurPreprocessed(0), mConnectionError(false)
{
}

Remote::~Remote()
{
}

void Remote::init()
{
    mServer.newConnection().connect([this](SocketServer* server) {
            warning() << "Got a connection";
            SocketClient::SharedPtr client;
            for (;;) {
                client = server->nextConnection();
                if (!client)
                    return;
                warning() << "remote client connected";
                addClient(client);
            }
        });

    const Daemon::Options& opts = Daemon::instance()->options();
    mPreprocessor.setCount(opts.preprocessCount);
    mRescheduleTimer.restart(opts.rescheduleCheck);
    mRescheduleTimeout = opts.rescheduleTimeout;
    mMaxPreprocessPending = opts.maxPreprocessPending;

    if (!mServer.listen(opts.localPort)) {
        error() << "Unable to tcp listen";
        abort();
    }
    warning() << "Listening" << opts.localPort;

    auto connectToScheduler = [this, opts]() {
        mConnectionError = false;
        mConnection.reset(new Connection);
        mConnection->newMessage().connect([this](const std::shared_ptr<Message>& message, Connection*) {
                error() << "Got a message" << message->messageId() << __LINE__;
                switch (message->messageId()) {
                case HasJobsMessage::MessageId:
                    handleHasJobsMessage(std::static_pointer_cast<HasJobsMessage>(message), mConnection);
                    break;
                default:
                    error("Unexpected message Remote::init: %d", message->messageId());
                    break;
                }
            });
        mConnection->finished().connect(std::bind([]() {
                    error() << "server finished connection";
                    EventLoop::eventLoop()->quit();
                }));
        mConnection->error().connect(std::bind([this] {
                    mConnectionError = true;
                    error() << "unable to reconnect, retrying in" << mReconnectTimeout << "ms";
                    mReconnectTimer.restart(mReconnectTimeout, Timer::SingleShot);
                }));
        mConnection->disconnected().connect(std::bind([this]() {
                    if (mConnectionError)
                        return;
                    mReconnectTimeout = 1000;
                    error() << "server closed connection, retrying in" << mReconnectTimeout << "ms";
                    mReconnectTimer.restart(mReconnectTimeout, Timer::SingleShot);
                }));
        mConnection->connected().connect(std::bind([this, opts]() {
                    mReconnectTimeout = 1000;
                    error() << "connected to scheduler";
                    String hn;
                    hn.resize(sysconf(_SC_HOST_NAME_MAX));
                    if (gethostname(hn.data(), hn.size()) == 0) {
                        hn.resize(strlen(hn.constData()));
                        mConnection->send(PeerMessage(hn, opts.localPort));
                    }
                }));
        if (!mConnection->connectTcp(opts.serverHost, opts.serverPort)) {
            error() << "unable to reconnect, retrying in" << mReconnectTimeout << "ms";
            mReconnectTimer.restart(mReconnectTimeout, Timer::SingleShot);
            return;
        }

    };
    mReconnectTimer.timeout().connect([this, connectToScheduler](Timer*) {
            // exponential backoff, max 5 minutes
            mReconnectTimeout = std::min(mReconnectTimeout * 2, 5 * 60 * 1000);
            connectToScheduler();
        });
    connectToScheduler();

    mRescheduleTimer.timeout().connect([this](Timer*) {
            //error() << "checking for reschedule!!!";
            const uint64_t now = Rct::monoMs();
            // reschedule outstanding jobs only, local will get to pending jobs eventually
#warning Should we reschedule pending remote jobs?
            bool done = false;
            auto it = mBuildingByTime.begin();
            while (it != mBuildingByTime.end() && !done) {
                bool del = false;
                const uint64_t started = it->first;
                auto sub = it->second.begin();
                while (sub != it->second.end()) {
                    assert(mBuildingById.contains((*sub)->jobid));
                    const int timeout = mRescheduleTimeout * std::max<uint32_t>(1, (*sub)->serial);
                    warning() << "considering" << now << started << (now - started) << timeout;
                    if (now - started < timeout) {
                        done = true;
                        break;
                    }
                    error() << "job has expired" << (*sub)->jobid;
                    // reschedule
                    Job::SharedPtr job = (*sub)->job.lock();
                    if (job) {
                        assert(job->id() == (*sub)->jobid);
                        error() << "job still exists" << job->status() << job->id();
                        if (job->status() != Job::RemotePending) {
#warning should we reschedule jobs we have partially received in case the connection times out?
                            // can only reschedule remotepending jobs
                            ++sub;
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
                        del = true;
                        break;
                    }
                }
                if (del)
                    mBuildingByTime.erase(it++);
                else
                    ++it;
            }
            if (!mPendingBuild.isEmpty()) {
                //mConnection->send(HasJobsMessage(mPendingBuild.size(), Daemon::instance()->options().localPort));
                for (const auto& p : mPendingBuild) {
                    mConnection->send(HasJobsMessage(p.first.type, p.first.major, p.first.target, p.second.size(),
                                                    Daemon::instance()->options().localPort));
                }
            }
        });
}

void Remote::handleJobMessage(const JobMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
{
    error() << "handle job message!" << msg->id() << "serial" << msg->serial();
    // let's make a job out of this
    Job::SharedPtr job = Job::create(msg->path(), msg->args(), Job::RemoteJob, msg->remoteName(),
                                     msg->id(), msg->preprocessed(), msg->serial(),
                                     msg->compilerType(), msg->compilerMajor(), msg->compilerTarget());
    std::weak_ptr<Connection> weakConn = conn;
    job->statusChanged().connect([weakConn](Job* job, Job::Status status) {
            const std::shared_ptr<Connection> conn = weakConn.lock();
            if (!conn) {
                error() << "no connection" << __FILE__ << __LINE__;
                return;
            }
            assert(job->type() == Job::RemoteJob);
            error() << "remote job status changed" << job << "local" << job->id() << "serial" << job->serial() << "remote" << job->remoteId() << status;
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
    job->readyReadStdOut().connect([weakConn](Job* job) {
            const std::shared_ptr<Connection> conn = weakConn.lock();
            if (!conn) {
                error() << "no connection" << __FILE__ << __LINE__;
                return;
            }
            warning() << "remote job ready stdout";
            conn->send(JobResponseMessage(JobResponseMessage::Stdout, job->remoteId(),
                                          job->serial(), job->readAllStdOut()));
        });
    job->readyReadStdErr().connect([weakConn](Job* job) {
            const std::shared_ptr<Connection> conn = weakConn.lock();
            if (!conn) {
                error() << "no connection" << __FILE__ << __LINE__;
                return;
            }
            warning() << "remote job ready stderr";
            conn->send(JobResponseMessage(JobResponseMessage::Stderr, job->remoteId(),
                                          job->serial(), job->readAllStdErr()));
        });
    job->start();
}

void Remote::handleRequestJobsMessage(const RequestJobsMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
{
    error() << "handle request jobs message" << msg->count();
    // take count jobs
    const plast::CompilerKey k = { msg->compilerType(), msg->compilerMajor(), msg->compilerTarget() };
    auto p = mPendingBuild.find(k);
    if (p == mPendingBuild.end()) {
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
            std::shared_ptr<Building> b = std::make_shared<Building>(Rct::monoMs(), job->id(), job->serial(), job, conn);
            mBuildingByTime[b->started].append(b);
            mBuildingById[b->jobid] = b;

            assert(job->isPreprocessed());
            // send this job to remote;
            error() << "sending job back" << job->id() << "serial" << job->serial();
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
        mPendingBuild.erase(p);
}

void Remote::handleHasJobsMessage(const HasJobsMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
{
    error() << "handle has jobs message";

    // do we have the compiler in question?
    if (!CompilerVersion::hasCompiler(msg->compilerType(), msg->compilerMajor(), msg->compilerTarget())) {
        error() << "we don't have compiler" << msg->compilerType() << msg->compilerMajor() << msg->compilerTarget();
        return;
    }
    error() << "we have compiler" << msg->compilerType() << msg->compilerMajor() << msg->compilerTarget();

    std::shared_ptr<Connection> remoteConn;
    {
        const Peer key = { msg->peer(), msg->port() };
        auto peer = mPeersByKey.find(key);
        if (peer == mPeersByKey.end()) {
            // make connection
            SocketClient::SharedPtr client = std::make_shared<SocketClient>();
            client->connect(key.peer, key.port);
            std::shared_ptr<Connection> conn = addClient(client);

            mPeersByKey[key] = conn;
            mPeersByConn[conn.get()] = key;
            remoteConn = conn;

            conn->send(HandshakeMessage(Daemon::instance()->options().localPort));
        } else {
            remoteConn = peer->second.lock();
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

void Remote::handleHandshakeMessage(const HandshakeMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
{
    const Peer key = { conn->client()->peerName(), msg->port() };
    if (mPeersByKey.contains(key)) {
        // drop the connection
        assert(!mPeersByConn.contains(conn.get()));
        conn->finish();
    } else {
        mPeersByKey[key] = conn;
        mPeersByConn[conn.get()] = key;
    }
}

void Remote::handleJobResponseMessage(const JobResponseMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
{
    error() << "handle job response" << msg->mode() << msg->id();
    Job::SharedPtr job = Job::job(msg->id());
    if (!job) {
        error() << "job not found for response";
        return;
    }
    if (msg->serial() != job->serial()) {
        error() << "job serial doesn't match, rescheduled remote?";
        error() << msg->serial() << "vs" << job->serial();
        return;
    }
    const Job::Status status = job->status();
    switch (status) {
    case Job::RemotePending:
        job->updateStatus(Job::RemoteReceiving);
        assert(mCurPreprocessed > 0);
        --mCurPreprocessed;
        job->clearPreprocessed();
        preprocessMore();
        // fall through
    case Job::RemoteReceiving:
        // accept the above statuses
        break;
    default:
        error() << "job no longer remote compiling";
        removeJob(job->id());
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

void Remote::handleLastJobMessage(const LastJobMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn)
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
    const uint32_t idle = Daemon::instance()->local().availableCount();
    if (idle > mRequestedCount) {
        const int count = std::min<int>(idle - mRequestedCount, 5);
        error() << "asking for" << count << "since" << mRequestedCount << "<" << idle;
        mRequestedCount += count;
        mRequested[key] = count;
        std::shared_ptr<Connection> conn = key.conn.lock();
        if (!conn) {
            error() << "connection dead" << __FILE__ << __LINE__;
            return;
        }
        conn->send(RequestJobsMessage(key.type, key.major, key.target, count));
    } else {
        error() << "not asking," << mRequestedCount << ">=" << idle;
    }
}

void Remote::preprocessMore()
{
    while (mCurPreprocessed < mMaxPreprocessPending
           && !mPendingPreprocess.isEmpty()) {
        const auto& pre = mPendingPreprocess.front();
        const Job::SharedPtr job = pre.job.lock();
        if (job) {
            const plast::CompilerKey k = pre.key;
            job->statusChanged().connect([this, k](Job* job, Job::Status status) {
                    if (status == Job::Preprocessed) {
                        error() << "preproc size" << job->preprocessed().size();
                        mPendingBuild[k].push_back(job->shared_from_this());
                        // send a HasJobsMessage to the scheduler
                        mConnection->send(HasJobsMessage(k.type, k.major, k.target, mPendingBuild[k].size(),
                                                         Daemon::instance()->options().localPort));
                    }
                });
            ++mCurPreprocessed;
            mPreprocessor.preprocess(job);
        }
        mPendingPreprocess.pop_front();
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
    while (!mPendingBuild.isEmpty()) {
        auto p = mPendingBuild.begin();
        assert(!p->second.isEmpty());
        Job::SharedPtr job = p->second.front().lock();
        p->second.removeFirst();
        if (p->second.isEmpty())
            mPendingBuild.erase(p);
        if (job) {
            assert(mCurPreprocessed > 0);
            --mCurPreprocessed;
            preprocessMore();
            return job;
        }
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
                assert(mCurPreprocessed > 0);
                --mCurPreprocessed;
                preprocessMore();
                return job;
            }
        }
        ++time;
    }
    return Job::SharedPtr();
}

std::shared_ptr<Connection> Remote::addClient(const SocketClient::SharedPtr& client)
{
    error() << "remote client added";
    static Hash<Connection*, std::shared_ptr<Connection> > conns;
    std::shared_ptr<Connection> conn = std::make_shared<Connection>(client);
    std::weak_ptr<Connection> weak = conn;
    conns[conn.get()] = conn;
    conn->newMessage().connect([this, weak](const std::shared_ptr<Message>& msg, Connection*) {
            error() << "Got a message" << msg->messageId() << __LINE__;
            std::shared_ptr<Connection> conn = weak.lock();
            if (!conn) {
                error() << "connection dead";
                return;
            }
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
    conn->disconnected().connect([this](Connection* ptr) {
            auto found = conns.find(ptr);
            assert(found != conns.end());
            std::shared_ptr<Connection> conn = found->second;

            conn->disconnected().disconnect();

            auto ck = mRequested.begin();
            while (ck != mRequested.end()) {
                if (ck->first.conn.lock() == conn) {
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
                        if ((*b)->conn.lock() == conn) {
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

            auto itc = mPeersByConn.find(conn.get());
            if (itc == mPeersByConn.end())
                return;
            const Peer key = itc->second;
            mPeersByConn.erase(itc);
            assert(mPeersByKey.contains(key));
            mPeersByKey.erase(key);

            requestMore();

            conns.erase(found);
        });
    return conn;
}

void Remote::compilingLocally(const Job::SharedPtr& job)
{
    assert(job->isPreprocessed());
    assert(mCurPreprocessed > 0);
    --mCurPreprocessed;
    job->clearPreprocessed();
    preprocessMore();
}

void Remote::post(const Job::SharedPtr& job)
{
    error() << "remote post";
    // queue for preprocess if not already done
    const plast::CompilerKey k = { job->compilerType(), job->compilerMajor(), job->compilerTarget() };
    if (!job->isPreprocessed()) {
        if (mCurPreprocessed >= mMaxPreprocessPending) {
            mPendingPreprocess.push_back({ k, job });
            return;
        }
        job->statusChanged().connect([this, k](Job* job, Job::Status status) {
                if (status == Job::Preprocessed) {
                    error() << "preproc size" << job->preprocessed().size();
                    mPendingBuild[k].push_back(job->shared_from_this());
                    // send a HasJobsMessage to the scheduler
                    mConnection->send(HasJobsMessage(k.type, k.major, k.target, mPendingBuild[k].size(),
                                                    Daemon::instance()->options().localPort));
                }
            });
        ++mCurPreprocessed;
        mPreprocessor.preprocess(job);
    } else {
        mPendingBuild[k].push_back(job);
        // send a HasJobsMessage to the scheduler
        mConnection->send(HasJobsMessage(k.type, k.major, k.target, mPendingBuild[k].size(),
                                        Daemon::instance()->options().localPort));
    }
}
