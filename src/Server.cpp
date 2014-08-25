/* This file is part of Plast.

   RTags is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   RTags is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with RTags.  If not, see <http://www.gnu.org/licenses/>. */

#include "Server.h"
#include <rct/Log.h>

Server::Server()
{

}

Server::~Server()
{

}
bool Server::init(int port)
{
    if (!mSocket.listen(port)) {
        error() << "Failed to listen on port" << port;
        return false;
    }

    mSocket.newConnection().connect([this](SocketServer*) {
            while (true) {
                auto socket = mSocket.nextConnection();
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

}

void Server::onConnectionDisconnected(Connection *connection)
{

}
