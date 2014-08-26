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

#include "Daemon.h"

Daemon::Daemon()
{
    auto onNewConnection = [this](SocketServer *server) {
        while (true) {
            auto socket = server->nextConnection();
            if (!socket)
                break;
            Connection *conn = new Connection(socket);
            conn->disconnected().connect([](Connection *conn) { EventLoop::deleteLater(conn); });
            conn->newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
        }
    };
    mLocalServer.newConnection().connect(onNewConnection);
    mRemoteServer.newConnection().connect(onNewConnection);
}

bool Daemon::init(const Path &socketFile, int port)
{
    if (!mLocalServer.listen(socketFile)) {
        error() << "Can't seem to listen on" << socketFile;
        return false;
    }

    if (!mRemoteServer.listen(port)) {
        error() << "Can't seem to listen on" << port;
        return false;
    }
    return true;
}

void Daemon::onNewMessage(Message *message, Connection *connection)
{

}
