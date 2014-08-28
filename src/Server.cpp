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
#include "Plast.h"

Server::Server()
{

}

Server::~Server()
{

}
bool Server::init(int port)
{
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

    mServer.newConnection().connect([this](SocketServer*) {
            while (true) {
                auto socket = mServer.nextConnection();
                if (!socket)
                    break;
                Connection *conn = new Connection(socket);
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
    case HandshakeMessage::MessageId:
        Host *&host = mConnections[connection];
        delete host;
        HandshakeMessage *handShake = static_cast<HandshakeMessage*>(message);
        host = new Host({ handShake->hostName(), handShake->capacity() });
        error() << "Got handshake from" << handShake->hostName();
        break;
    }
}

void Server::onConnectionDisconnected(Connection *connection)
{
    EventLoop::deleteLater(connection);
    delete mConnections.take(connection);
}
