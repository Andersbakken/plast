#include "Remote.h"
#include "Daemon.h"
#include <rct/Log.h>
#include <unistd.h>

#define RESCHEDULETIMEOUT 15000
#define RESCHEDULECHECK   5000

Remote::Remote()
    : mNextId(0), mRescheduleTimer(RESCHEDULECHECK), mRequestedCount(0)
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
            const uint64_t now = Rct::monoMs();
            // reschedule outstanding jobs only, local will get to pending jobs eventually
#warning Should we reschedule pending remote jobs?
            auto it = mBuildingByTime.begin();
            while (it != mBuildingByTime.end()) {
                const uint64_t started = it->first;
                if (now - started < RESCHEDULETIMEOUT)
                    break;
                // reschedule
                Job::SharedPtr job = it->second->job.lock();
                if (job) {
                    if (job->status() != Job::RemotePending) {
                        // can only reschedule remotepending jobs
                        ++it;
                        continue;
                    }
                    error() << "rescheduling" << job->id() << "now" << now << "started" << started;
                    job->updateStatus(Job::Idle);
                    job->increaseSerial();
                    job->start();
                }
                mBuildingById.erase(it->second->jobid);
                mBuildingByTime.erase(it++);
            }
            if (!mPending.isEmpty()) {
                mConnection.send(HasJobsMessage(mPending.size(), Daemon::instance()->options().localPort));
            }
        });
}

void Remote::handleJobMessage(const JobMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle job message!";
    // let's make a job out of this
    Job::SharedPtr job = Job::create(msg->path(), msg->args(), Job::RemoteJob, msg->remoteName(),
                                     msg->id(), msg->preprocessed(), msg->serial());
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
    error() << "handle request jobs message";
    // take count jobs
    int rem = msg->count();
    for (;;) {
        if (mPending.empty())
            break;
        Job::SharedPtr job = mPending.front().lock();
        mPending.removeFirst();
        if (job) {
            // add job to building map
            std::shared_ptr<Building> b = std::make_shared<Building>(Rct::monoMs(), job->id(), job);
            mBuildingByTime[b->started] = b;
            mBuildingById[b->jobid] = b;

            assert(job->isPreprocessed());
            // send this job to remote;
            error() << "sending job back";
            job->updateStatus(Job::RemotePending);
            conn->send(JobMessage(job->path(), job->args(), job->id(), job->preprocessed(),
                                  job->serial(), job->remoteName()));
            if (!--rem)
                break;
        }
    }
    conn->send(LastJobMessage(msg->count() - rem));

}

void Remote::handleHasJobsMessage(const HasJobsMessage::SharedPtr& msg, Connection* conn)
{
    error() << "handle has jobs message";
    // assume we have room for the job for now, make a connection and ask for jobs
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

    if (mRequested.contains(remoteConn)) {
        // we already asked this host for jobs, wait until it gets back to us
        return;
    }

    const unsigned int idle = Daemon::instance()->local().availableCount();
    if (idle > mRequestedCount) {
        const int count = std::min<int>(idle - mRequestedCount, 5);
        mRequestedCount += count;
        mRequested[remoteConn] = count;
        remoteConn->send(RequestJobsMessage(idle));
    }
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
        removeJob(job->id());
        job->writeFile(msg->data());
        job->updateStatus(Job::Compiled);
        Job::finish(job.get());
        break;
    }
}

void Remote::handleLastJobMessage(const LastJobMessage::SharedPtr& msg, Connection* conn)
{
    auto it = mRequested.find(conn);
    assert(it != mRequested.end());
    assert(it->second >= msg->count());
    mRequestedCount -= msg->count();
    it->second -= msg->count();
    if (!it->second)
        mRequested.erase(it);
}

void Remote::removeJob(uint64_t id)
{
    Hash<uint64_t, std::shared_ptr<Building> >::iterator idit = mBuildingById.find(id);
    if (idit != mBuildingById.end())
        return;
    assert(idit->second.use_count() == 2);
    Map<uint64_t, std::shared_ptr<Building> >::iterator tit = mBuildingByTime.lower_bound(idit->second->started);
    assert(tit != mBuildingByTime.end());
    mBuildingById.erase(idit);
    while (tit->second->jobid != id) {
        ++tit;
        assert(tit != mBuildingByTime.end());
    }
    mBuildingByTime.erase(tit);
}

Job::SharedPtr Remote::take()
{
    // prefer jobs that are not sent out
    while (!mPending.isEmpty()) {
        Job::SharedPtr job = mPending.front().lock();
        mPending.removeFirst();
        if (job)
            return job;
    }
    // take oldest pending jobs first
    for (auto cand : mBuildingByTime) {
        Job::SharedPtr job = cand.second->job.lock();
        if (job && job->status() == Job::RemotePending) {
            // we can take this job since we haven't received any data for it yet
            job->increaseSerial();
            job->updateStatus(Job::Idle);
            const uint64_t id = cand.second->jobid;
            removeJob(id);
            return job;
        }
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

            auto itr = mRequested.find(conn);
            if (itr != mRequested.end()) {
                mRequestedCount -= itr->second;
                mRequested.erase(itr);
            }

            auto itc = mPeersByConn.find(conn);
            if (itc == mPeersByConn.end())
                return;
            const Peer key = itc->second;
            mPeersByConn.erase(itc);
            assert(mPeersByKey.contains(key));
            mPeersByKey.erase(key);
        });
    return conn;
}

void Remote::post(const Job::SharedPtr& job)
{
    error() << "local post";
    // queue for preprocess if not already done
    if (!job->isPreprocessed()) {
        job->statusChanged().connect([this](Job* job, Job::Status status) {
                if (status == Job::Preprocessed) {
                    error() << "preproc size" << job->preprocessed().size();
                    mPending.push_back(job->shared_from_this());
                    // send a HasJobsMessage to the scheduler
                    mConnection.send(HasJobsMessage(mPending.size(), Daemon::instance()->options().localPort));
                }
            });
        mPreprocessor.preprocess(job);
    } else {
        mPending.push_back(job);
        // send a HasJobsMessage to the scheduler
        mConnection.send(HasJobsMessage(mPending.size(), Daemon::instance()->options().localPort));
    }
}
