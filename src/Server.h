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

#ifndef Server_h
#define Server_h

#include <rct/SocketServer.h>
#include <rct/Hash.h>
#include <rct/Connection.h>
#include <rct/Message.h>
#include "Plast.h"
#include "Console.h"
#include "Host.h"u

class Server
{
public:
    Server();
    ~Server();

    bool init();
private:
    void handleConsoleCommand(const String &string);
    void handleConsoleCompletion(const String& string, int start, int end, String& common, List<String>& candidates);

    void onNewMessage(Message *message, Connection *connection);
    void onConnectionDisconnected(Connection *connection);
    void onHttpClientReadyRead(const std::shared_ptr<SocketClient> &socket);


    SocketServer mServer, mEventSourceServer;
    struct Node {
        Host host;
        int capacity, jobsSent, jobsReceived;
    };
    Hash<std::shared_ptr<Connection>, Node*> mNodes;
    std::shared_ptr<SocketClient> mDiscoverySocket;
};

#endif
