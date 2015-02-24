#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Peer.h"
#include <Messages.h>
#include <HttpServer.h>
#include <WebSocket.h>
#include <rct/SocketClient.h>
#include <rct/SocketServer.h>
#include <rct/Hash.h>
#include <rct/Set.h>
#include <memory>

class Scheduler : public std::enable_shared_from_this<Scheduler>
{
public:
    typedef std::shared_ptr<Scheduler> SharedPtr;
    typedef std::weak_ptr<Scheduler> WeakPtr;

    struct Options
    {
        uint16_t port;
    };

    Scheduler(const Options& opts);
    ~Scheduler();

    void init();

    const Options& options() { return mOpts; }

    static SharedPtr instance();

private:
    void addPeer(const Peer::SharedPtr& peer);
    void sendAllPeers(const WebSocket::SharedPtr& socket);
    void sendToAll(const WebSocket::Message& msg);
    void sendToAll(const String& msg);

private:
    SocketServer mServer;
    HttpServer mHttpServer;
    Set<Peer::SharedPtr> mPeers;
    Options mOpts;
    Hash<WebSocket*, WebSocket::SharedPtr> mWebSockets;

private:
    static WeakPtr sInstance;
};

inline Scheduler::SharedPtr Scheduler::instance()
{
    return sInstance.lock();
}

#endif
