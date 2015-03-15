#ifndef PEER_H
#define PEER_H

#include <rct/Connection.h>
#include <rct/SocketClient.h>
#include <rct/SignalSlot.h>
#include <json.hpp>
#include <memory>

class Peer : public std::enable_shared_from_this<Peer>
{
public:
    typedef std::shared_ptr<Peer> SharedPtr;
    typedef std::weak_ptr<Peer> WeakPtr;

    Peer(const SocketClient::SharedPtr& client);
    ~Peer();

    std::shared_ptr<Connection> connection() { return mConnection; }

    String ip() const { return mConnection->client()->peerName(); }
    String name() const { return mName; }
    uint32_t jobs() const { return mJobs; }
    int id() const { return mId; }

    enum Event {
        Websocket,
        PeerChanged,
        Disconnected,
        JobsAvailable
    };
    Signal<std::function<void(const Peer::SharedPtr&, Event, const nlohmann::json&)> >& event() { return mEvent; }

private:
    int mId;
    std::shared_ptr<Connection> mConnection;
    String mName;
    uint32_t mJobs;
    Signal<std::function<void(const Peer::SharedPtr&, Event, const nlohmann::json&)> > mEvent;

    static int sId;
};

#endif
