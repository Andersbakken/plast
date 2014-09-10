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
                mNodes[conn] = new Node;
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


commit 44f5ee2793c70d0640c71d6d8fbbe06c41d488fa
Author: Anders Bakken <agbakken@gmail.com>
Date:   Mon Jan 13 00:48:25 2014 -0800

    Simple HTTP server for statistics.

diff --git a/src/Project.cpp b/src/Project.cpp
index 20b5699..fc03251 100644
--- a/src/Project.cpp
+++ b/src/Project.cpp
@@ -340,7 +340,10 @@ void Project::onJobFinished(const std::shared_ptr<IndexData> &indexData)
     JobData *jobData = &it->second;
     assert(jobData->job);
     const bool success = jobData->job->flags & (IndexerJob::CompleteLocal|IndexerJob::CompleteRemote);
-    assert(!success || !(jobData->job->flags & (IndexerJob::Crashed|IndexerJob::Aborted)));
+    if (success && jobData->job->flags & (IndexerJob::Crashed|IndexerJob::Aborted)) {
+        error() << "Could die" << String::format<8>("0x%x", jobData->job->flags);
+    }
+    // assert(!success || !(jobData->job->flags & (IndexerJob::Crashed|IndexerJob::Aborted)));
     if (jobData->job->flags & IndexerJob::Crashed) {
         ++jobData->crashCount;
     } else {
diff --git a/src/Server.cpp b/src/Server.cpp
index a8af113..48d1659 100644
--- a/src/Server.cpp
+++ b/src/Server.cpp
@@ -61,11 +61,49 @@
 #include <arpa/inet.h>
 #include <limits>

+class HttpLogObject : public LogOutput
+{
+public:
+    HttpLogObject(const SocketClient::SharedPtr &socket)
+        : LogOutput(Error), mSocket(socket)
+    {}
+
+    // virtual bool testLog(int level) const
+    // {
+    //     return
+    // }
+    virtual void log(const char *msg, int len)
+    {
+        if (!EventLoop::isMainThread()) {
+            String message(msg, len);
+            SocketClient::WeakPtr weak = mSocket;
+
+            EventLoop::eventLoop()->callLater(std::bind([message,weak] {
+                        if (SocketClient::SharedPtr socket = weak.lock()) {
+                            send(message.constData(), message.size(), socket);
+                        }
+                    }));
+        } else {
+            send(msg, len, mSocket);
+        }
+    }
+    static void send(const char *msg, int len, const SocketClient::SharedPtr &socket)
+    {
+        static const unsigned char *header = reinterpret_cast<const unsigned char*>("data:");
+        static const unsigned char *crlf = reinterpret_cast<const unsigned char*>("\r\n");
+        socket->write(header, 5);
+        socket->write(reinterpret_cast<const unsigned char *>(msg), len);
+        socket->write(crlf, 2);
+    }
+private:
+    SocketClient::SharedPtr mSocket;
+};
+
 static const bool debugMulti = getenv("RDM_DEBUG_MULTI");

 Server *Server::sInstance = 0;
 Server::Server()
-    : mVerbose(false), mCurrentFileId(0), mThreadPool(0), mRemotePending(0), mCompletionThread(0), mWebServer(0)
+    : mVerbose(false), mCurrentFileId(0), mThreadPool(0), mRemotePending(0), mCompletionThread(0)
 {
     Messages::registerMessage<JobRequestMessage>();
     Messages::registerMessage<JobResponseMessage>();
@@ -100,11 +138,6 @@ void Server::clear()
     mThreadPool = 0;
 }

-static int mongooseStatistics(struct mg_connection *conn)
-{
-    return Server::instance()->mongooseStatistics(conn);
-}
-
 bool Server::init(const Options &options)
 {
     RTags::initMessages();
@@ -218,8 +251,24 @@ bool Server::init(const Options &options)
     }
     mThreadPool = new ThreadPool(mOptions.jobCount);
     if (mOptions.httpPort) {
-        mWebServer = mg_create_server(this);
-        mg_add_uri_handler(mWebServer, "/stats", ::mongooseStatistics);
+        mHttpServer.reset(new SocketServer);
+        if (!mHttpServer->listen(mOptions.httpPort)) {
+            error() << "Unable to listen on port" << mOptions.httpPort;
+            return false;
+        }
+
+        mHttpServer->newConnection().connect(std::bind([this](){
+                    int foo = 0;
+                    while (SocketClient::SharedPtr client = mHttpServer->nextConnection()) {
+                        ++foo;
+                        mHttpClients[client] = 0;
+                        client->disconnected().connect(std::bind([this,client] { mHttpClients.remove(client); }));
+                        client->readyRead().connect(std::bind(&Server::onHttpClientReadyRead, this, std::placeholders::_1));
+                    }
+                    error() << foo;
+
+                }));
+
     }
     return true;
 }
@@ -1785,11 +1834,8 @@ void Server::stopServers()
     Path::rm(mOptions.socketFile);
     mUnixServer.reset();
     mTcpServer.reset();
+    mHttpServer.reset();
     mProjects.clear();
-    if (mWebServer) {
-        mg_destroy_server(&mWebServer);
-        assert(!mWebServer);
-    }
 }

 static inline uint64_t connectTime(uint64_t lastAttempt, int failures)
@@ -1881,7 +1927,26 @@ int Server::startPreprocessJobs()
     return ret;
 }

-int Server::mongooseStatistics(struct mg_connection *conn)
-{
-    return 0;
+void Server::onHttpClientReadyRead(const std::shared_ptr<SocketClient> &socket)
+{
+    error() << "Balls" << socket->buffer().size();
+    auto &log = mHttpClients[socket];
+    if (!log) {
+        static const char *requestLine = "GET /stats HTTP/1.1\r\n";
+        static const size_t len = strlen(requestLine);
+        if (socket->buffer().size() >= len) {
+            if (!memcmp(socket->buffer().data(), requestLine, len)) {
+                static const char *response = ("HTTP/1.1 200 OK\r\n"
+                                               "Cache: no-cache\r\n"
+                                               "Cache-Control: private\r\n"
+                                               "Pragma: no-cache\r\n"
+                                               "Content-Type: text/event-stream\r\n\r\n");
+                static const int responseLen = strlen(response);
+                socket->write(reinterpret_cast<const unsigned char*>(response), responseLen);
+                log.reset(new HttpLogObject(socket));
+            } else {
+                socket->close();
+            }
+        }
+    }
}
