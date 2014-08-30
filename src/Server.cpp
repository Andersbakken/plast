/* This file is part of Plast.

   Plast is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Plast is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Plast.  If not, see <http://www.gnu.org/licenses/>. */

#include "Server.h"
#include <rct/Log.h>
#include <rct/Config.h>
#include "Plast.h"

Server::Server()
{
    Console::init("plasts> ",
                  std::bind(&Server::handleConsoleCommand, this, std::placeholders::_1),
                  std::bind(&Server::handleConsoleCompletion, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
}

Server::~Server()
{

}

bool Server::init()
{
    const uint16_t port = Config::value<int>("port");
    const uint16_t discoveryPort = Config::value<int>("discovery-port");
    if (!mServer.listen(port)) {
        enum { Timeout = 1000 };
        Connection connection;
        bool success = false;
        if (connection.connectTcp("127.0.0.1", port, Timeout)) {
            connection.send(QuitMessage());
            connection.disconnected().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
            connection.finished().connect(std::bind([](){ EventLoop::eventLoop()->quit(); }));
            EventLoop::eventLoop()->exec(Timeout);
            sleep(1);
            success = mServer.listen(port);
        }
        if (!success) {
            error() << "Can't seem to listen on" << port;
            return false;
        }
    }
    error() << "listening on" << port;

    if (discoveryPort) {
        mDiscoverySocket.reset(new SocketClient);
        mDiscoverySocket->bind(discoveryPort);
        const String hostName = Rct::hostName();
        mDiscoverySocket->readyReadFrom().connect([port, hostName, discoveryPort](const SocketClient::SharedPtr &socket, const String &ip, uint16_t p, Buffer &&data) {
                if (p == discoveryPort) {
                    const Buffer dat = std::forward<Buffer>(data);
                    if (dat.size() == 1 && dat.data()[0] == '?') {
                        String packet;
                        {
                            Serializer serializer(packet);
                            serializer << 'S' << port;
                        }
                        socket->writeTo(ip, discoveryPort, packet);
                    }
                }
            });
    }


    mServer.newConnection().connect([this](SocketServer*) {
            while (true) {
                auto socket = mServer.nextConnection();
                if (!socket)
                    break;
                std::shared_ptr<Connection> conn = std::make_shared<Connection>(socket);
                conn->disconnected().connect(std::bind(&Server::onConnectionDisconnected, this, std::placeholders::_1));
                conn->newMessage().connect(std::bind(&Server::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
                mConnections[conn] = new Host;
            }
        });

    return true;
}

void Server::onNewMessage(Message *message, Connection *connection)
{
    switch (message->messageId()) {
    case QuitMessage::MessageId:
        EventLoop::eventLoop()->quit();
        break;
    case DaemonJobAnnouncementMessage::MessageId: {
        // DaemonJobAnnouncementMessage *jobAnnouncement = static_cast<DaemonJobAnnouncementMessage*>(message);
        // const ServerJobAnnouncementMessage msg(jobAnnouncement->count(), jobAnnouncement->sha256(),
        //                                        jobAnnouncement->compiler(), connection->client()->peerName(), connection->client()->port());
        // error() << "Got job announcement from" << mConnections.value(connection)->name
        //         << jobAnnouncement->compiler() << jobAnnouncement->count();
        // static const bool returnToSender = Config::isEnabled("return-to-sender");
        // for (auto it : mConnections) {
        //     if (it.first != connection || returnToSender) {
        //         it.first->send(msg);
        //     }
        // }
        break; }
    case HandshakeMessage::MessageId:
        Host *&host = mConnections[connection->shared_from_this()];
        delete host;
        HandshakeMessage *handShake = static_cast<HandshakeMessage*>(message);
        host = new Host({ handShake->hostName(), handShake->capacity() });
        error() << "Got handshake from" << handShake->hostName();
        break;
    }
}

void Server::onConnectionDisconnected(Connection *connection)
{
    Host *host = mConnections.take(connection->shared_from_this());
    error() << "Lost connection to" << (host ? host->name : "someone");
    delete host;
}

void Server::handleConsoleCommand(const String &string)
{
    String str = string;
    while (str.endsWith(' '))
        str.chop(1);
    if (str == "hosts") {
        for (const auto it : mConnections) {
            printf("Host: %s Capacity: %d Jobs sent: %d Jobs received: %d\n",
                   it.second->name.constData(), it.second->capacity,
                   it.second->jobsSent, it.second->jobsReceived);
        }
    } else if (str == "quit") {
        EventLoop::eventLoop()->quit();
    }
}

void Server::handleConsoleCompletion(const String& string, int, int,
                                     String &common, List<String> &candidates)
{
    static const List<String> cands = List<String>() << "hosts" << "quit";
    auto res = Console::tryComplete(string, cands);
    // error() << res.text << res.candidates;
    common = res.text;
    candidates = res.candidates;
}
