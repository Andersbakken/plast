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
#include <rct/String.h>
#include <memory>
#include <rct/Value.h>
#include <JsonUtils.h>

class Scheduler : public std::enable_shared_from_this<Scheduler>
{
public:
    typedef std::shared_ptr<Scheduler> SharedPtr;
    typedef std::weak_ptr<Scheduler> WeakPtr;

    struct Options
    {
        uint16_t port;
        Path compilers;
    };

    Scheduler(const Options& opts);
    ~Scheduler();

    void init();

    const Options& options() { return mOpts; }

    static SharedPtr instance();

private:
    struct Compiler {
        String target, host, link;
        enum Type { Clang, GCC } type;
        uint16_t majorVersion, minorVersion;

        Compiler()
            : type(Clang), majorVersion(0), minorVersion(0)
        {}

        nlohmann::json object() const
        {
            return (JsonObject()
                    << "target" << target
                    << "host" << host
                    << "link" << link
                    << "major" << majorVersion
                    << "minor" << minorVersion
                    << "type" << (type == Clang ? "clang" : "gcc")).object();
        }
        bool isValid() const
        {
            return (!target.isEmpty()
                    && !host.isEmpty()
                    && !link.isEmpty()
                    && majorVersion >= 0
                    && minorVersion >= 0
                    && (majorVersion > 0 || minorVersion > 0));
        }
    };
    List<Compiler> loadCompilers() const;
    void addPeer(const Peer::SharedPtr& peer);
    void sendAllPeers(const WebSocket::SharedPtr& socket);
    void sendToAll(const WebSocket::Message& msg);
    void sendToAll(const String& msg);
    void handleWebsocket(const HttpServer::Request::SharedPtr &req);
    void handleQuery(const HttpServer::Request::SharedPtr &req);

    void readSettings();
    void writeSettings();

    Set<Peer::SharedPtr> findPeers(const String& name);

private:
    SocketServer mServer;
    HttpServer mHttpServer;
    Set<Peer::SharedPtr> mPeers;
    Options mOpts;
    Hash<WebSocket*, WebSocket::SharedPtr> mWebSockets;
    Set<String> mBlackList, mWhiteList;

private:
    static WeakPtr sInstance;
};

inline Scheduler::SharedPtr Scheduler::instance()
{
    return sInstance.lock();
}

#endif
