#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <rct/SocketClient.h>
#include <rct/SocketServer.h>
#include <rct/String.h>
#include <rct/List.h>
#include <rct/LinkedList.h>
#include <rct/Buffer.h>
#include <rct/Hash.h>

class HttpServer : private SocketServer
{
public:
    typedef std::shared_ptr<HttpServer> SharedPtr;
    typedef std::weak_ptr<HttpServer> WeakPtr;

    HttpServer();
    ~HttpServer();

    using SocketServer::Mode;
    enum Protocol { Http10, Http11 };

    bool listen(uint16_t port, Protocol proto = Http11, Mode mode = IPv4);

    class Headers
    {
    public:
        void add(const String& key, const String& value);
        void set(const String& key, const List<String>& values);

        String value(const String& key) const;
        List<String> values(const String& key) const;
        Hash<String, List<String> > headers() const;

    private:
        Hash<String, List<String> > mHeaders;
    };

    class Request;

    class Response
    {
    public:
        Response();
        Response(Protocol proto, int status, const Headers& headers, const String& body = String());

        void setStatus(int status);
        void setHeaders(const Headers& headers);
        void setBody(const String& body);

    private:
        Protocol mProtocol;
        int mStatus;
        Headers mHeaders;
        String mBody;

        friend class Request;
    };

    class Body
    {
    public:
        bool atEnd() const;
        String read(int size = -1);
        String readAll();

    private:
        Body(Request* req);

        Request* mRequest;

        friend class Request;
    };

    class Request
    {
    public:
        typedef std::shared_ptr<Request> SharedPtr;
        typedef std::weak_ptr<Request> WeakPtr;

        const Headers& headers() const { return mHeaders; }
        const Body& body() const { return mBody; }
        Body& body() { return mBody; }

        void write(const Response& response);

    private:
        Request(HttpServer* server, const SocketClient::SharedPtr& client, const Headers& headers);

        SocketClient::WeakPtr mClient;
        Headers mHeaders;
        Body mBody;
        HttpServer* mServer;

        friend class HttpServer;
    };

    enum Error { SocketError };
    Signal<std::function<void(Error)> >& error() { return mError; }
    Signal<std::function<void(const Request::SharedPtr&)> >& request() { return mRequest; }

private:
    void addClient(const SocketClient::SharedPtr& client);
    void makeRequest(const SocketClient::SharedPtr& client, const String& headers);

private:
    struct Data
    {
        uint64_t id;
        uint64_t seq;
        SocketClient::SharedPtr client;
        bool readingBody;
        LinkedList<Buffer> buffers;
    };
    Protocol mProtocol;
    uint64_t mNextId;
    Hash<uint64_t, Data> mData;
    Signal<std::function<void(const Request::SharedPtr&)> > mRequest;
    Signal<std::function<void(Error)> > mError;
    SocketServer mTcpServer;

    friend class Request;
};

inline Hash<String, List<String> > HttpServer::Headers::headers() const
{
    return mHeaders;
}

inline HttpServer::Response::Response()
{
}

inline HttpServer::Response::Response(Protocol proto, int status,
                                      const Headers& headers, const String& body)
    : mProtocol(proto), mStatus(status), mHeaders(headers), mBody(body)
{
}

inline void HttpServer::Response::setHeaders(const Headers& headers)
{
    mHeaders = headers;
}

inline void HttpServer::Response::setBody(const String& body)
{
    mBody = body;
}

#endif
