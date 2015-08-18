#ifndef Http_h
#define Http_h

#include <rct/String.h>
#include <rct/Map.h>
#include <rct/Path.h>

class Http
{
public:
    struct Request
    {
        Request()
            : output(0)
        {}
        String url;
        FILE *output;
        Map<String, String> headers;
    };
    struct Response
    {
        Response()
            : statusCode(0)
        {}
        int statusCode;
        String error;
        String contents;
        Map<String, String> responseHeaders;
    };
    static Response get(const Request &request);
};

#endif
