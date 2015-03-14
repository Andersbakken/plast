#ifndef REMOTE_H
#define REMOTE_H

#include "Job.h"
#include "Preprocessor.h"
#include <rct/Hash.h>
#include <rct/Map.h>
#include <rct/Set.h>
#include <rct/List.h>
#include <rct/LinkedList.h>
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
    void compilingLocally(const Job::SharedPtr& job);

    void requestMore();

    std::shared_ptr<Connection> scheduler() { return mConnection; }

private:
    std::shared_ptr<Connection> addClient(const SocketClient::SharedPtr& client);
    void handleJobMessage(const JobMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn);
    void handleHasJobsMessage(const HasJobsMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn);
    void handleRequestJobsMessage(const RequestJobsMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn);
    void handleHandshakeMessage(const HandshakeMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn);
    void handleJobResponseMessage(const JobResponseMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn);
    void handleLastJobMessage(const LastJobMessage::SharedPtr& msg, const std::shared_ptr<Connection>& conn);
    void removeJob(uint64_t id);
    void preprocessMore();

    struct ConnectionKey
    {
        std::weak_ptr<Connection> conn;
        plast::CompilerType type;
        int major;
        String target;

        bool operator<(const ConnectionKey& other) const
        {
            if (conn.owner_before(other.conn))
                return true;
            if (other.conn.owner_before(conn))
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
    std::shared_ptr<Connection> mConnection;
    Preprocessor mPreprocessor;
    uint32_t mNextId;
    Timer mRescheduleTimer, mReconnectTimer;

    struct Building
    {
        Building()
            : started(0), jobid(0), serial(0)
        {
        }
        Building(uint64_t s, uint64_t id, uint32_t ser, const Job::SharedPtr& j, const std::shared_ptr<Connection> &c)
            : started(s), jobid(id), serial(ser), job(j), conn(c)
        {
        }

        uint64_t started;
        uint64_t jobid;
        uint32_t serial;
        Job::WeakPtr job;
        std::weak_ptr<Connection> conn;
    };
    Map<plast::CompilerKey, List<Job::WeakPtr> > mPendingBuild;
    struct PendingPreprocess
    {
        plast::CompilerKey key;
        Job::WeakPtr job;
    };
    LinkedList<PendingPreprocess> mPendingPreprocess;
    Map<uint64_t, List<std::shared_ptr<Building> > > mBuildingByTime;
    Hash<uint64_t, std::shared_ptr<Building> > mBuildingById;
    Map<ConnectionKey, int> mRequested;
    Set<ConnectionKey> mHasMore;
    int mRequestedCount;
    int mRescheduleTimeout, mReconnectTimeout;
    int mMaxPreprocessPending, mCurPreprocessed;
    bool mConnectionError;

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
    Map<Peer, std::weak_ptr<Connection> > mPeersByKey;
    Hash<std::shared_ptr<Connection>, Peer> mPeersByConn;
};

#endif
