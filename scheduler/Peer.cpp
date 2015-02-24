#include "Peer.h"
#include <Messages.h>

using namespace json11;

int Peer::sId = 0;

Peer::Peer(const SocketClient::SharedPtr& client)
    : mId(++sId), mConnection(client)
{
    mConnection.newMessage().connect([this](const std::shared_ptr<Message>& msg, Connection* conn) {
            switch (msg->messageId()) {
            case HasJobsMessage::MessageId: {
                const HasJobsMessage::SharedPtr jobsmsg = std::static_pointer_cast<HasJobsMessage>(msg);

                const Json obj = Json::object({
                        { "port", jobsmsg->port() },
                        { "count", jobsmsg->count() },
                        { "peer", conn->client()->peerName().ref() }
                    });
                mEvent(shared_from_this(), JobsAvailable, obj);
                break; }
            case PeerMessage::MessageId: {
                const PeerMessage::SharedPtr peermsg = std::static_pointer_cast<PeerMessage>(msg);
                mName = peermsg->name();
                const Json obj = mName.ref();
                mEvent(shared_from_this(), NameChanged, obj);
                break; }
            case BuildingMessage::MessageId: {
                const BuildingMessage::SharedPtr bmsg = std::static_pointer_cast<BuildingMessage>(msg);
#warning fixme, BuildingMessage::id() is uint64_t
                const Json obj = Json::object({
                        { "type", "build" },
                        { "peerid", mId },
                        { "local", mName.ref() },
                        { "peer", bmsg->peer().ref() },
                        { "file", bmsg->file().ref() },
                        { "start", (bmsg->type() == BuildingMessage::Start) },
                        { "jobid", static_cast<int>(bmsg->id()) }
                    });
                mEvent(shared_from_this(), Websocket, obj);
                break; }
            default:
                error() << "Unexpected message Scheduler" << msg->messageId();
                conn->finish(1);
                break;
            }
        });
    mConnection.disconnected().connect([this](Connection* conn) {
            conn->disconnected().disconnect();
            mEvent(shared_from_this(), Disconnected, Json());
        });
}

Peer::~Peer()
{
}
