#include "Scheduler.h"
#include <json11.hpp>
#include <rct/Log.h>
#include <string.h>

using namespace json11;

Scheduler::WeakPtr Scheduler::sInstance;

static inline const char* guessMime(const Path& file)
{
    const char* ext = file.extension();
    if (!ext)
        return "text/plain";
    if (!strcmp(ext, "html"))
        return "text/html";
    if (!strcmp(ext, "txt"))
        return "text/plain";
    if (!strcmp(ext, "js"))
        return "text/javascript";
    if (!strcmp(ext, "css"))
        return "text/css";
    if (!strcmp(ext, "jpg"))
        return "image/jpg";
    if (!strcmp(ext, "png"))
        return "image/png";
    return "text/plain";
}

Scheduler::Scheduler(const Options& opts)
    : mOpts(opts)
{
    mServer.newConnection().connect([this](SocketServer* server) {
            SocketClient::SharedPtr client;
            for (;;) {
                client = server->nextConnection();
                if (!client)
                    return;
                addPeer(std::make_shared<Peer>(client));
            }
        });
    error() << "listening on" << mOpts.port;
    if (!mServer.listen(mOpts.port)) {
        error() << "couldn't tcp listen";
        abort();
    }

    mHttpServer.listen(8089);
    mHttpServer.request().connect([this](const HttpServer::Request::SharedPtr& req) {
            error() << "got request" << req->protocol() << req->method() << req->path();
            if (req->method() == HttpServer::Request::Get) {
                if (req->headers().has("Upgrade")) {
                    error() << "upgrade?";
                    HttpServer::Response response;
                    if (WebSocket::response(*req, response)) {
                        req->write(response);
                        WebSocket::SharedPtr websocket = std::make_shared<WebSocket>(req->takeSocket());
                        mWebSockets[websocket.get()] = websocket;
                        websocket->message().connect([this](WebSocket* websocket, const WebSocket::Message& msg) {
                                error() << "got message" << msg.opcode() << msg.message();
                                // if (msg.opcode() == WebSocket::Message::TextFrame) {
                                //     websocket->write(msg);
                                //     mWebSockets.erase(websocket);
                                // }
                            });
                        websocket->error().connect([this](WebSocket* websocket) {
                                mWebSockets.erase(websocket);
                            });
                        websocket->disconnected().connect([this](WebSocket* websocket) {
                                mWebSockets.erase(websocket);
                            });
                        sendAllPeers(websocket);
                        return;
                    }
                }

                String file = req->path();
                if (file == "/")
                    file = "stats.html";
                static Path base = Rct::executablePath().parentDir().ensureTrailingSlash() + "stats/";
                const Path path = Path(base + file).resolved();
                if (!path.startsWith(base)) {
                    // no
                    error() << "Don't want to serve" << path;
                    const String data = "No.";
                    HttpServer::Response response(req->protocol(), 404);
                    response.headers().add("Content-Length", String::number(data.size()));
                    response.headers().add("Content-Type", "text/plain");
                    response.headers().add("Connection", "close");
                    response.setBody(data);
                    req->write(response, HttpServer::Response::Incomplete);
                    req->close();
                } else {
                    // serve file
                    if (path.isFile()) {
                        const String data = path.readAll();
                        HttpServer::Response response(req->protocol(), 200);
                        response.headers().add("Content-Length", String::number(data.size()));
                        response.headers().add("Content-Type", guessMime(file));
                        response.headers().add("Connection", "keep-alive");
                        response.setBody(data);
                        req->write(response);
                    } else {
                        const String data = "Unable to open " + file;
                        HttpServer::Response response(req->protocol(), 404);
                        response.headers().add("Content-Length", String::number(data.size()));
                        response.headers().add("Content-Type", "text/plain");
                        response.headers().add("Connection", "keep-alive");
                        response.setBody(data);
                        req->write(response);
                    }
                }
            }
        });
}

Scheduler::~Scheduler()
{
}

void Scheduler::sendToAll(const WebSocket::Message& msg)
{
    for (auto socket : mWebSockets) {
        socket.second->write(msg);
    }
}

void Scheduler::sendToAll(const String& msg)
{
    const WebSocket::Message wmsg(WebSocket::Message::TextFrame, msg);
    sendToAll(wmsg);
}

void Scheduler::sendAllPeers(const WebSocket::SharedPtr& socket)
{
    for (const Peer::SharedPtr& peer : mPeers) {
        const Json peerj = Json::object({
                { "type", "peer" },
                { "id", peer->id() },
                { "name", peer->name().ref() }
            });
        const WebSocket::Message msg(WebSocket::Message::TextFrame, peerj.dump());
        socket->write(msg);
    }
}

void Scheduler::addPeer(const Peer::SharedPtr& peer)
{
    mPeers.insert(peer);
    peer->event().connect([this](const Peer::SharedPtr& peer, Peer::Event event, const Json& json) {
            switch (event) {
            case Peer::JobsAvailable: {
                HasJobsMessage msg(json["count"].number_value(),
                                   json["port"].number_value());
                msg.setPeer(json["peer"].string_value());
                for (const Peer::SharedPtr& other : mPeers) {
                    if (other != peer) {
                        other->connection()->send(msg);
                    }
                }
                break; }
            case Peer::NameChanged: {
                const Json peerj = Json::object({
                        { "type", "peer" },
                        { "id", peer->id() },
                        { "name", peer->name().ref() }
                    });
                const WebSocket::Message msg(WebSocket::Message::TextFrame, peerj.dump());
                sendToAll(msg);
                break; }
            case Peer::Disconnected: {
                const Json peerj = Json::object({
                        { "type", "peer" },
                        { "id", peer->id() },
                        { "delete", true }
                    });
                const WebSocket::Message msg(WebSocket::Message::TextFrame, peerj.dump());
                sendToAll(msg);
                mPeers.erase(peer);
                break; }
            case Peer::Websocket: {
                const WebSocket::Message msg(WebSocket::Message::TextFrame, json.dump());
                sendToAll(msg);
                break; }
            }
        });
}

void Scheduler::init()
{
    sInstance = shared_from_this();
    messages::init();
}
