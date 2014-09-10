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
#include "QuitMessage.h"
#include "HandshakeMessage.h"
#include "MonitorMessage.h"
#include "DaemonListMessage.h"

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
    const uint16_t httpPort = Config::value<int>("http-port");
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
    error() << "listening on" << port << discoveryPort << httpPort;

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

    if (httpPort && !mHttpServer.listen(httpPort)) {
        error() << "Can't seem to listen on" << httpPort;
        return false;
    }

    mHttpServer.newConnection().connect([this](SocketServer*) {
            while (std::shared_ptr<SocketClient> client = mHttpServer.nextConnection()) {
                mHttpClients[client] = HttpConnection({ String(), false });
                EventLoop::eventLoop()->callLater([client, this] {
                        if (!client->buffer().isEmpty()) {
                            onHttpClientReadyRead(client, std::forward<Buffer>(client->takeBuffer()));
                        }
                    });

                client->disconnected().connect([this](const std::shared_ptr<SocketClient> &client) {
                        mHttpClients.remove(client);
                    });
                client->readyRead().connect(std::bind(&Server::onHttpClientReadyRead, this, std::placeholders::_1, std::placeholders::_2));
            }
        });

    mServer.newConnection().connect([this](SocketServer*) {
            while (true) {
                auto socket = mServer.nextConnection();
                if (!socket)
                    break;
                std::shared_ptr<Connection> conn = std::make_shared<Connection>(socket);
                conn->disconnected().connect(std::bind(&Server::onConnectionDisconnected, this, std::placeholders::_1));
                conn->newMessage().connect(std::bind(&Server::onNewMessage, this, std::placeholders::_1, std::placeholders::_2));
                mNodes[conn] = new Node;
            }
        });

    mDocRoot = Config::value<String>("doc-root");
    return true;
}

void Server::onNewMessage(Message *message, Connection *connection)
{
    switch (message->messageId()) {
    case Plast::QuitMessageId:
        EventLoop::eventLoop()->quit();
        break;
    case Plast::HandshakeMessageId: {
        Node *&node = mNodes[connection->shared_from_this()];
        delete node;
        node = 0;
        Set<Host> nodes;
        for (const auto it : mNodes) {
            if (it.second) {
                nodes.insert(it.second->host);
            }
        }
        HandshakeMessage *handShake = static_cast<HandshakeMessage*>(message);
        String peerName = connection->client()->peerName();
        if (peerName == "127.0.0.1")
            peerName = handShake->friendlyName();
        node = new Node({ Host({ peerName, handShake->port(), handShake->friendlyName()}), handShake->capacity(), 0, 0 });
        connection->send(DaemonListMessage(nodes));
        error() << "Got handshake from" << connection->client()->peerName() << handShake->friendlyName() << nodes.size();
        break; }
    case Plast::MonitorMessageId:
        if (!mHttpClients.isEmpty()) {
            MonitorMessage *monitor = static_cast<MonitorMessage*>(message);
            const String &msg = monitor->message();
            for (const auto &client : mHttpClients) {
                if (client.second.parsed) { // /events
                    error() << "WRITING SHIT" << msg;
                    static const unsigned char *header = reinterpret_cast<const unsigned char*>("data:");
                    static const unsigned char *crlf = reinterpret_cast<const unsigned char*>("\r\n");
                    client.first->write(header, 5);
                    client.first->write(reinterpret_cast<const unsigned char *>(msg.constData()), msg.size());
                    client.first->write(crlf, 2);
                    client.first->write(crlf, 2);
                }
            }
        }
        break;
    }
}

void Server::onConnectionDisconnected(Connection *connection)
{
    Node *node = mNodes.take(connection->shared_from_this());
    error() << "Lost connection to" << (node ? node->host.toString().constData() : "someone");
    delete node;
}

void Server::handleConsoleCommand(const String &string)
{
    String str = string;
    while (str.endsWith(' '))
        str.chop(1);
    if (str == "hosts") {
        for (const auto &it : mNodes) {
            printf("Node: %s Capacity: %d Jobs sent: %d Jobs received: %d\n",
                   it.second->host.toString().constData(), it.second->capacity,
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

void Server::onHttpClientReadyRead(const std::shared_ptr<SocketClient> &socket, Buffer &&buf)
{
    Buffer buffer = std::forward<Buffer>(buf);
    auto &conn = mHttpClients[socket];
    if (conn.parsed)
        return;
    conn.buffer.append(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    if (!socket->buffer().isEmpty()) {
        buffer = std::move(socket->takeBuffer());
        conn.buffer.append(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    const List<String> lines = conn.buffer.split("\r\n");
    error() << lines;
    const int blank = lines.indexOf(String());
    if (blank != -1 && blank + 1 < lines.size() && lines.at(blank + 1).isEmpty()) {
        conn.parsed = true;
        const String &first = lines.first();
        if (!first.startsWith("GET /") || !first.endsWith(" HTTP/1.1")) {
            socket->write("HTTP/1.1 400 Bad Request\r\n\r\n");
            socket->close();
            return;
        }

        String path = first.mid(4, first.size() - 9 - 4);
        List<String> search;
        const int q = path.indexOf('?');
        if (q != -1) {
            search = path.mid(q + 1).split('&');
            path.resize(q);
        }
        warning() << "path is" << path << search;
        if (path == "/events") {
            socket->write("HTTP/1.1 200 OK\r\n"
                          "Cache: no-cache\r\n"
                          "Cache-Control: private\r\n"
                          "Pragma: no-cache\r\n"
                          "Content-Type: text/event-stream\r\n\r\n");
        } else if (path.contains("../")) {
            socket->write("HTTP/1.1 403 Forbidden\r\n\r\n");
            socket->close();
        } else {
            Path req = mDocRoot + path;
            String contents;
            warning() << req << Path::pwd();
            if (Rct::readFile(req, contents)) {
                socket->write(String::format<256>("HTTP/1.1 200 OK\r\n"
                                                  "Cache: no-cache\r\n"
                                                  "Cache-Control: private\r\n"
                                                  "Pragma: no-cache\r\n"
                                                  "Content-Length: %d\r\n"
                                                  "Connection: Close\r\n"
                                                  "Content-Type: text/html\r\n\r\n", contents.size()));
                socket->write(contents);
                socket->close();
            } else {
                socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
                socket->close();
            }
        }
    }
}
