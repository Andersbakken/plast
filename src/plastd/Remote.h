#ifndef REMOTE_H
#define REMOTE_H

#include "Job.h"
#include "Preprocessor.h"
#include <rct/Hash.h>
#include <rct/Map.h>
#include <rct/Set.h>
#include <rct/List.h>
#include <rct/Connection.h>
#include <rct/SocketClient.h>
#include <rct/SocketServer.h>
#include <rct/Timer.h>
#include <Messages.h>
#include <Plast.h>
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

    void requestMore();

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

    struct ConnectionKey
    {
        Connection* conn;
        plast::CompilerType type;
        int major;
        String target;

        bool operator<(const ConnectionKey& other) const
        {
            if (conn < other.conn)
                return true;
            if (conn > other.conn)
                return false;
            if (type < other.type)
                return true;
            if (type > other.type)
                return false;
            if (major < other.major)
                return true;
            if (major > other.major)
                return false;
            return target < other.target;
        }
    };
    void requestMore(const ConnectionKey& conn);

private:
    SocketServer mServer;
    Connection mConnection;
    Preprocessor mPreprocessor;
    unsigned int mNextId;
    Timer mRescheduleTimer;

    struct Building
    {
        Building()
            : started(0), jobid(0), conn(0)
        {
        }
        Building(uint64_t s, uint64_t id, const Job::SharedPtr& j, Connection* c)
            : started(s), jobid(id), job(j), conn(c)
        {
        }

        uint64_t started;
        uint64_t jobid;
        Job::WeakPtr job;
        Connection* conn;
    };
    Map<plast::CompilerKey, List<Job::WeakPtr> > mPending;
    Map<uint64_t, List<std::shared_ptr<Building> > > mBuildingByTime;
    Hash<uint64_t, std::shared_ptr<Building> > mBuildingById;
    Map<ConnectionKey, int> mRequested;
    Set<ConnectionKey> mHasMore;
    int mRequestedCount;
    int mRescheduleTimeout;

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
