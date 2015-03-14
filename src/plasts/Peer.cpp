#include "Peer.h"
#include <Messages.h>

using nlohmann::json;

int Peer::sId = 0;

Peer::Peer(const SocketClient::SharedPtr& client)
    : mId(++sId), mConnection(Connection::create())
{
    mConnection->connect(client);
    mConnection->newMessage().connect([this](const std::shared_ptr<Message>& msg, const std::shared_ptr<Connection> &conn) {
            switch (msg->messageId()) {
            case HasJobsMessage::MessageId: {
                const HasJobsMessage::SharedPtr jobsmsg = std::static_pointer_cast<HasJobsMessage>(msg);

                const json obj = {
                    { "port", jobsmsg->port() },
                    { "type", static_cast<int>(jobsmsg->compilerType()) },
                    { "major", jobsmsg->compilerMajor() },
                    { "target", jobsmsg->compilerTarget().ref() },
                    { "count", jobsmsg->count() },
                    { "peer", conn->client()->peerName().ref() }
                };
                mEvent(shared_from_this(), JobsAvailable, obj);
                break; }
            case PeerMessage::MessageId: {
                const PeerMessage::SharedPtr peermsg = std::static_pointer_cast<PeerMessage>(msg);
                mName = peermsg->name();
                const json obj = mName.ref();
                mEvent(shared_from_this(), NameChanged, obj);
                break; }
            case BuildingMessage::MessageId: {
                const BuildingMessage::SharedPtr bmsg = std::static_pointer_cast<BuildingMessage>(msg);
                const json obj = {
                    { "type", "build" },
                    { "id", mId },
                    { "local", mName.ref() },
                    { "peer", bmsg->peer().ref() },
                    { "file", bmsg->file().ref() },
                    { "start", (bmsg->type() == BuildingMessage::Start) },
                    { "jobid", bmsg->id() }
                };
                mEvent(shared_from_this(), Websocket, obj);
                break; }
            default:
                error() << "Unexpected message Scheduler" << msg->messageId();
                conn->finish(1);
                break;
            }
        });
    mConnection->disconnected().connect([this](const std::shared_ptr<Connection> &conn) {
            conn->disconnected().disconnect();
            mEvent(shared_from_this(), Disconnected, json());
        });
}

Peer::~Peer()
{
}
