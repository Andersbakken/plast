#ifndef PEER_H
#define PEER_H

#include <rct/Connection.h>
#include <rct/SocketClient.h>
#include <rct/SignalSlot.h>
#include <json11.hpp>
#include <memory>

class Peer : public std::enable_shared_from_this<Peer>
{
public:
    typedef std::shared_ptr<Peer> SharedPtr;
    typedef std::weak_ptr<Peer> WeakPtr;

    Peer(const SocketClient::SharedPtr& client);
    ~Peer();

    Connection* connection() { return &mConnection; }

    String name() const { return mName; }
    int id() const { return mId; }

    enum Event {
        Websocket,
        NameChanged,
        Disconnected,
        JobsAvailable
    };
    Signal<std::function<void(const Peer::SharedPtr&, Event, const json11::Json&)> >& event() { return mEvent; }

private:
    int mId;
    Connection mConnection;
    String mName;
    Signal<std::function<void(const Peer::SharedPtr&, Event, const json11::Json&)> > mEvent;

    static int sId;
};

#endif
