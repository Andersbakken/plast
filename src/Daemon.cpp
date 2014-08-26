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
    : mNextJobId(1), mExplicitServer(false)
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
    mServerConnection.newMessage().connect(std::bind(&Daemon::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
    mServerConnection.disconnected().connect([this](Connection*) { restartServerTimer(); });
    mServerConnection.error().connect([this](Connection*) { restartServerTimer(); });

    mServerTimer.timeout().connect([this](Timer *) { reconnectToServer(); });

    mDiscoverySocket.readyReadFrom().connect([this](const SocketClient::SharedPtr &, const String &, uint16_t, Buffer &&data) {
            onDiscoverySocketReadyRead(std::forward<Buffer>(data));
        });
}

bool Daemon::init(const Options &options)
{
    if (!mLocalServer.listen(options.socketFile)) {
        error() << "Can't seem to listen on" << mOptions.socketFile;
        return false;
    }

    if (!mRemoteServer.listen(mOptions.port)) {
        error() << "Can't seem to listen on" << mOptions.port;
        return false;
    }

    mDiscoverySocket.addMembership(options.discoveryAddress);
    mOptions = options;
    mExplicitServer = !mOptions.serverHost.isEmpty();
    reconnectToServer();

    return true;
}

void Daemon::onNewMessage(Message *message, Connection *connection)
{
    switch (message->messageId()) {
    case LocalJobMessage::MessageId:
        handleLocalJobMessage(static_cast<LocalJobMessage*>(message), connection);
        break;
    default:
        error() << "Unexpected message" << message->messageId();
        break;
    }
}

void Daemon::handleLocalJobMessage(LocalJobMessage *msg, Connection *conn)
{
    if (!mServerConnection.isConnected()) {
        conn->send(LocalJobResponseMessage());
        // we're not connected so we can't schedule anything
        return;
    }
    mLocalConnections[conn] = mNextJobId++;
    // ### should we tell the server that we're no longer interested if we get disconnected?
    conn->disconnected().connect([this](Connection *c) { mLocalConnections.remove(c); });
}

void Daemon::reconnectToServer()
{
    if (mServerConnection.isConnected())
        return;

    if (mOptions.serverHost.isEmpty()) {
        mDiscoverySocket.writeTo(mOptions.discoveryAddress, mOptions.discoveryPort, "?");
        restartServerTimer();
        return;
    }

    if (!mServerConnection.connectTcp(mOptions.serverHost, mOptions.serverPort)) {
        restartServerTimer();
        return;
    }
}

void Daemon::onDiscoverySocketReadyRead(Buffer &&data)
{
    Buffer buf = std::forward<Buffer>(data);
    Deserializer deserializer(reinterpret_cast<const char*>(buf.data()), buf.size());
    char command;
    deserializer >> command;
    switch (command) {
    case 's':
        if (!mExplicitServer && !mServerConnection.isConnected()) {
            deserializer >> mOptions.serverHost >> mOptions.serverPort;
            reconnectToServer();
        }
        break;
    case '?':
        if (mServerConnection.isConnected()) {
            String packet;
            {
                Serializer serializer(packet);
                serializer << 's' << mOptions.serverHost << mOptions.serverPort;
            }
            mDiscoverySocket.writeTo(mOptions.discoveryAddress, mOptions.discoveryPort, packet);
        }
        break;
    }
}

void Daemon::restartServerTimer()
{
    mServerTimer.restart(1000, Timer::SingleShot);
}
