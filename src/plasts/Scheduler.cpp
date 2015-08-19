#include "Scheduler.h"
#include <json.hpp>
#include <JsonUtils.h>
#include <rct/Log.h>
#include <string.h>
#include <regex>

using nlohmann::json;

Scheduler::WeakPtr Scheduler::sInstance;

typedef std::function<void(WebSocket*, const List<json>&)> CmdHandler;

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
    readSettings();
    loadCompilers();
    auto maybeLoadCompilers = [this](const Path &path) {
        if (path == mOpts.compilers)
            loadCompilers();
    };
    mWatcher.added().connect(maybeLoadCompilers);
    mWatcher.removed().connect(maybeLoadCompilers);
    mWatcher.modified().connect(maybeLoadCompilers);
    mWatcher.watch(mOpts.compilers.parentDir());

    mServer.newConnection().connect([this](SocketServer* server) {
            SocketClient::SharedPtr client;
            for (;;) {
                client = server->nextConnection();
                if (!client)
                    return;
                const String& ip = client->peerName();
                if (mWhiteList.contains(ip) || !mBlackList.contains(ip))
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
            if (req->method() == HttpServer::Request::Post) {
                if (req->path() == "/query") {
                    handleQuery(req);
                }
            } else if (req->method() == HttpServer::Request::Get) {
                if (req->headers().has("Upgrade")) {
                    error() << "upgrade?";
                    handleWebsocket(req);
                    return;
                }

                String file = req->path();
                // serve files
                if (file == "/") {
                    file = "stats.html";
                }
                static Path base = Path(Rct::executablePath().parentDir().ensureTrailingSlash() + "stats/").resolved();
                const Path path = Path(base + file).resolved();
                if (!path.startsWith(base)) {
                    // no
                    error() << "Don't want to serve" << path;
                    const String data = "No.";
                    HttpServer::Response response(req->protocol(), 404);
                    response.headers() = HttpServer::Headers::StringMap {
                        { "Content-Length", String::number(data.size()) },
                        { "Content-Type", "text/plain" },
                        { "Connection", "close" }
                    };
                    response.setBody(data);
                    req->write(response, HttpServer::Response::Incomplete);
                    req->close();
                } else {
                    // serve file
                    if (path.isFile()) {
                        const String data = path.readAll();
                        HttpServer::Response response(req->protocol(), 200);
                        response.headers() = HttpServer::Headers::StringMap {
                            { "Content-Length", String::number(data.size()) },
                            { "Content-Type", guessMime(file) },
                            { "Connection", "keep-alive" }
                        };
                        response.setBody(data);
                        req->write(response);
                    } else {
                        const String data = "Unable to open " + file;
                        HttpServer::Response response(req->protocol(), 404);
                        response.headers() = HttpServer::Headers::StringMap {
                            { "Content-Length", String::number(data.size()) },
                            { "Content-Type", "text/plain" },
                            { "Connection", "keep-alive" }
                        };
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

Set<Peer::SharedPtr> Scheduler::findPeers(const String& name)
{
    Set<Peer::SharedPtr> ret;

    std::smatch match;
    const std::regex rx(name.constData(), std::regex_constants::ECMAScript | std::regex_constants::icase);
    for (const auto& p : mPeers) {
        if (std::regex_match(p->name().ref(), match, rx)) {
            if (match.position() == 0 && match.length() == p->name().size())
                ret.insert(p);
        } else if (std::regex_match(p->ip().ref(), match, rx)) {
            if (match.position() == 0 && match.length() == p->ip().size())
                ret.insert(p);
        }
    }

    return ret;
}

void Scheduler::readSettings()
{
    auto readSet = [](const Path& fn, Set<String>& set) {
        set.clear();
        set << fn.readAll().split('\n', String::SkipEmpty);
    };
    readSet(Path::home() + ".config/plasts.whitelist", mWhiteList);
    readSet(Path::home() + ".config/plasts.blacklist", mBlackList);
}

void Scheduler::writeSettings()
{
    auto writeSet = [](const Path& fn, const Set<String>& set) {
        String w;
        for (const String& k : set) {
            w += k + "\n";
        }
        fn.write(w);
    };
    writeSet(Path::home() + ".config/plasts.whitelist", mWhiteList);
    writeSet(Path::home() + ".config/plasts.blacklist", mBlackList);
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
        const json peerj = {
            { "type", "peer" },
            { "id", peer->id() },
            { "name", peer->name().ref() },
            { "jobs", peer->jobs() }
        };
        const WebSocket::Message msg(WebSocket::Message::TextFrame, peerj.dump());
        socket->write(msg);
    }
}

void Scheduler::addPeer(const Peer::SharedPtr& peer)
{
    mPeers.insert(peer);
    peer->event().connect([this](const Peer::SharedPtr& peer, Peer::Event event, const json& value) {
            switch (event) {
            case Peer::JobsAvailable: {
                HasJobsMessage msg(static_cast<plast::CompilerType>(value["type"].get<int>()),
                                   value["major"].get<int>(),
                                   value["target"].get<json::string_t>(),
                                   value["count"].get<int>(),
                                   value["port"].get<uint16_t>());
                msg.setPeer(value["peer"].get<std::string>());
                for (const Peer::SharedPtr& other : mPeers) {
                    if (other != peer) {
                        other->connection()->send(msg);
                    }
                }
                break; }
            case Peer::PeerChanged: {
                const json peerj = {
                    { "type", "peer" },
                    { "id", peer->id() },
                    { "name", peer->name().ref() },
                    { "jobs", peer->jobs() }
                };
                const WebSocket::Message msg(WebSocket::Message::TextFrame, peerj.dump());
                sendToAll(msg);
                break; }
            case Peer::Disconnected: {
                const json peerj = {
                    { "type", "peer" },
                    { "id", peer->id() },
                    { "delete", true }
                };
                const WebSocket::Message msg(WebSocket::Message::TextFrame, peerj.dump());
                sendToAll(msg);
                mPeers.erase(peer);
                break; }
            case Peer::Websocket: {
                const WebSocket::Message msg(WebSocket::Message::TextFrame, value.dump());
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

void Scheduler::handleWebsocket(const HttpServer::Request::SharedPtr &req)
{
    HttpServer::Response response;
    if (WebSocket::response(*req, response)) {
        req->write(response);
        WebSocket::SharedPtr websocket = std::make_shared<WebSocket>(req->takeSocket());
        mWebSockets[websocket.get()] = websocket;

        Map<String, CmdHandler> cmds = {
            { "peers", [this](WebSocket* ws, const List<json>& args) {
                    for (const auto& p : mPeers) {
                        ws->write((JsonObject()
                                   << "peer" << p->name()
                                   << "ip" << p->ip()
                                   << "jobs" << p->jobs()).dump());
                    }
                } },
            { "block", [this](WebSocket* ws, const List<json>& args) {
                    if (args.isEmpty()) {
                        // list all blocks
                        json::array_t all;
                        for (const String& b : mBlackList) {
                            all.push_back(b);
                        }
                        ws->write((JsonObject() << "blacklisted" << all).dump());
                        return;
                    }

                    auto block = [this, ws](const String& peer) -> bool {
                        if (mWhiteList.contains(peer)) {
                            // report error
                            ws->write((JsonObject() << "error" << "whitelisted").dump());
                            return false;
                        }
                        mBlackList.insert(peer);
                        writeSettings();
                        ws->write((JsonObject() << "blacklisted" << peer).dump());
                        return true;
                    };

                    // find each peer, get the address
                    for (const json& j : args) {
                        if (j.is_string()) {
                            const String name = j.get<json::string_t>();
                            const Set<Peer::SharedPtr> peers = findPeers(name);
                            if (peers.isEmpty()) {
                                if (Rct::isIP(name))
                                    block(name);
                                else
                                    ws->write((JsonObject() << "error" << ("peer not found: " + name)).dump());
                                return;
                            }
                            for (const auto& p : peers) {
                                if (block(p->ip())) {
                                    mPeers.erase(p);
                                    const json peerj = {
                                        { "type", "peer" },
                                        { "id", p->id() },
                                        { "delete", true }
                                    };
                                    sendToAll(peerj.dump());
                                }
                            }
                        }
                    }
                } },
            { "unblock", [this](WebSocket* ws, const List<json>& args) {
                    for (const json& j : args) {
                        if (j.is_string()) {
                            const String name = j.get<json::string_t>();
                            if (mBlackList.remove(name)) {
                                ws->write((JsonObject() << "unblocked" << name).dump());
                                writeSettings();
                            } else {
                                ws->write((JsonObject() << "error" << (name + " not found in blacklist")).dump());
                            }
                        }
                    }
                } }
        };

        websocket->message().connect([this, cmds](WebSocket* websocket, const WebSocket::Message& msg) {
                if (msg.opcode() == WebSocket::Message::TextFrame) {
                    error() << "got message" << msg.opcode() << msg.message();
                    json j;
                    try {
                        j = json::parse(msg.message());
                    } catch(const std::exception& e) {
                        error() << "exception" << e.what();
                        return;
                    }
                    if (!j.is_object()) {
                        error() << "message not an object";
                        websocket->write((JsonObject() << "error" << "message is not an object").dump());
                        return;
                    }
                    const auto cmdname = j["cmd"];
                    if (!cmdname.is_string()) {
                        error() << "cmd not a string";
                        websocket->write((JsonObject() << "error" << "cmd is not a string").dump());
                        return;
                    }
                    const auto& cmd = cmds.find(cmdname.get<std::string>());
                    if (cmd == cmds.end()) {
                        error() << "cmd" << cmdname.get<std::string>() << "not recognized";
                        websocket->write((JsonObject()
                                          << "cmd" << cmdname.get<std::string>()
                                          << "error" << "not recognized").dump());
                        return;
                    }
                    const auto args = j["args"];
                    if (args.is_array())
                        cmd->second(websocket, args.get<json::array_t>());
                    else
                        cmd->second(websocket, List<json>());
                }
            });
        websocket->error().connect([this](WebSocket* websocket) {
                mWebSockets.erase(websocket);
            });
        websocket->disconnected().connect([this](WebSocket* websocket) {
                mWebSockets.erase(websocket);
            });
        sendAllPeers(websocket);
    }
}

void Scheduler::handleQuery(const HttpServer::Request::SharedPtr &req)
{
    auto go = [req, this]() {
        enum Type {
            Error,
            Success
        };
        auto send = [req](const json &data, Type type) {
            // error() << "socket" << data.dump() << type;
            const bool pretty = req->query().contains("pretty=true");
            String json = data.dump(pretty ? 4 : -1);
            if (pretty)
                json.append('\n');
            HttpServer::Response response(req->protocol(), type == Error ? 404 : 200);
            response.headers() = HttpServer::Headers::StringMap {
                { "Content-Length", String::number(json.size()) },
                { "Content-Type", "text/json" },
                { "Connection", type == Error ? "close" : "keep-alive" }
            };
            response.setBody(json);
            req->write(response, HttpServer::Response::Complete);
            req->close();
        };

        error() << req->body().done();
        const String body = req->body().read();
        SafeJson j;
        try {
            j = json::parse(body);
        } catch(const std::exception &e) {
            send((JsonObject() << "error" << (String("Couldn't parse JSON: ") + e.what())).object(), Error);
            return;
        }

        const String cmd = j["command"].get<String>();
        if (cmd.isEmpty()) {
            send((JsonObject() << "error" << "command is empty").object(), Error);
            return;
        }

        if (cmd == "list-compilers") {
            String target, host;
            Compiler::Type type = Compiler::Unknown;
            std::pair<uint16_t, uint16_t> minVersion, maxVersion;
            auto convertVersion = [](const SafeJson &j) {
                std::pair<uint16_t, uint16_t> version;
                if (j.is_object()) {
                    version.first = j["major"].get<uint16_t>();
                    version.second = j["minor"].get<uint16_t>();
                }
                return version;
            };

            SafeJson parameters = j["parameters"];
            if (parameters.is_object()) {
                target = parameters["target"].get<String>();
                host = parameters["host"].get<String>();
                type = Compiler::stringToType(parameters["type"].get<String>());
                minVersion = convertVersion(parameters["minimumVersion"]);
                maxVersion = convertVersion(parameters["maximumVersion"]);
            }
            std::vector<SafeJson> jc;
            for (const Compiler &compiler : mCompilers) {
                if (!target.isEmpty() && compiler.target != target)
                    continue;
                if (!host.isEmpty() && compiler.host != host)
                    continue;
                if (type != Compiler::Unknown && type != compiler.type)
                    continue;
                if ((minVersion.first || minVersion.second)
                    && (compiler.majorVersion < minVersion.first
                        || (compiler.majorVersion == minVersion.first && compiler.minorVersion < minVersion.second))) {
                    continue;
                }
                if ((maxVersion.first || maxVersion.second)
                    && (compiler.majorVersion > maxVersion.first
                        || (compiler.majorVersion == maxVersion.first && compiler.majorVersion > maxVersion.second))) {
                    continue;
                }
                jc.push_back(compiler.object());
            };

            const json response = {
                { "compilers", jc }
            };
            send(response, Success);
        } else {
            send((JsonObject() << "error" << "unknown command " + cmd).object(), Error);
        }
    };

    if (req->body().done()) {
        go();
    } else {
        req->body().readyRead().connect([go](HttpServer::Body *body) {
                if (body->done())
                    go();
            });
    }
}

void Scheduler::loadCompilers()
{
    mCompilers.clear();
    const String contents = mOpts.compilers.readAll();
    if (contents.isEmpty())
        return;

    SafeJson j;
    try {
        j = json::parse(contents);
    } catch(const std::exception &e) {
        error("Couldn't parse compilers \"%s\": %s",
              mOpts.compilers.constData(), e.what());
        return;
    }

    for (const SafeJson &c : j.get<std::vector<SafeJson> >("compilers")) {
        Compiler cc;
        cc.target = c.get<String>("target");
        cc.host = c.get<String>("host");
        if (cc.host.isEmpty()) {
            cc.host = cc.target;
        } else if (cc.target.isEmpty()) {
            cc.target = cc.host;
        }
        cc.link = c["link"].get<String>();
        cc.type = Compiler::stringToType(c["type"].get<String>());
        cc.majorVersion = c["major"].get<int>();
        cc.minorVersion = c["minor"].get<int>();
        if (!cc.isValid()) {
            error() << "Invalid compiler" << cc.object().dump();
            continue;
        }
        mCompilers.push_back(cc);
    }
}

