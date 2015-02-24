#ifndef REMOTE_H
#define REMOTE_H

#include "Job.h"
#include "Preprocessor.h"
#include <rct/Hash.h>
#include <rct/Map.h>
#include <rct/List.h>
#include <rct/Connection.h>
#include <rct/SocketClient.h>
#include <rct/SocketServer.h>
#include <rct/Timer.h>
#include <Messages.h>
#include <memory>
#include <cstdint>

class Remote
{
public:
    Remote();
    ~Remote();

    void init();

    void post(const Job::SharedPtr& job);
    Job::SharedPtr take();

    Connection& scheduler() { return mConnection; }

private:
    Connection* addClient(const SocketClient::SharedPtr& client);
    void handleJobMessage(const JobMessage::SharedPtr& msg, Connection* conn);
    void handleHasJobsMessage(const HasJobsMessage::SharedPtr& msg, Connection* conn);
    void handleRequestJobsMessage(const RequestJobsMessage::SharedPtr& msg, Connection* conn);
    void handleHandshakeMessage(const HandshakeMessage::SharedPtr& msg, Connection* conn);
    void handleJobResponseMessage(const JobResponseMessage::SharedPtr& msg, Connection* conn);
    void handleLastJobMessage(const LastJobMessage::SharedPtr& msg, Connection* conn);
    void removeJob(uint64_t id);

private:
    SocketServer mServer;
    Connection mConnection;
    Preprocessor mPreprocessor;
    unsigned int mNextId;
    Timer mRescheduleTimer;

    List<Job::WeakPtr> mPending;
    struct Building
    {
        Building()
            : started(0), jobid(0)
        {
        }
        Building(uint64_t s, uint64_t id, const Job::SharedPtr& j)
            : started(s), jobid(id), job(j)
        {
        }

        uint64_t started;
        uint64_t jobid;
        Job::WeakPtr job;
    };
    Map<uint64_t, std::shared_ptr<Building> > mBuildingByTime;
    Hash<uint64_t, std::shared_ptr<Building> > mBuildingById;
    Hash<Connection*, int> mRequested;
    int mRequestedCount;

    struct Peer
    {
        String peer;
        uint16_t port;

        bool operator<(const Peer& other) const
        {
            if (peer < other.peer)
                return true;
            else if (peer > other.peer)
                return false;
            return port < other.port;
        }
    };
    Map<Peer, Connection*> mPeersByKey;
    Hash<Connection*, Peer> mPeersByConn;
};

#endif
