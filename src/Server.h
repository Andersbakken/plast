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

#ifndef Server_h
#define Server_h

#include <rct/SocketServer.h>
#include <rct/Hash.h>
#include <rct/Connection.h>
#include <rct/Message.h>

class Server
{
public:
    Server();
    ~Server();

    bool init(int port);
private:
    void onNewMessage(Message *message, Connection *connection);
    void onConnectionDisconnected(Connection *connection);

    SocketServer mSocket;
    struct Host {
        Host() : capacity(-1) {}
        String name;
        int capacity;
    };
    Hash<Connection *, Host*> mConnections;
};

#endif
