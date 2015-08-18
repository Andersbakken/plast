#include "Http.h"
#include <curl/curl.h>

#define C(op)                                                           \
    code = op;                                                          \
    if (code != CURLE_OK) {                                             \
        response.error = String::format<1024>("Curl error: %s at for %s => %d (%s)", \
                                              #op, request.url.constData(), code, curl_easy_strerror(code)); \
        goto exit;                                                      \
    }


Http::Response Http::get(const Request &request)
{
    typedef size_t (*Callback)(void *, size_t, size_t, void*);
    CURL *easy = curl_easy_init();
    assert(easy);
    Response response;
    curl_slist *headers = 0;
    CURLcode code = CURLE_OK;

    C(curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1));
    C(curl_easy_setopt(easy, CURLOPT_URL, request.url.constData()));
    for (const auto &header : request.headers) {
        char formatted[1024];
        snprintf(formatted, sizeof(formatted), "%s: %s", header.first.constData(), header.second.constData());
        headers = curl_slist_append(headers, formatted);
    }
    if (headers)
        C(curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers));

    if (request.output) {
        C(curl_easy_setopt(easy, CURLOPT_WRITEDATA, request.output));
    } else {
        C(curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response));
        C(curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, static_cast<Callback>([](void *data, size_t size, size_t nmemb, void *userData) {
                        reinterpret_cast<Response*>(userData)->contents.append(reinterpret_cast<const char*>(data), nmemb * size);
                        return size * nmemb;
                    })));
    }
    C(curl_easy_setopt(easy, CURLOPT_HEADERDATA, &response));
    C(curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, static_cast<Callback>([](void *data, size_t size, size_t nmemb, void *userData) {
                    Response *response = reinterpret_cast<Response*>(userData);
                    String header(reinterpret_cast<const char*>(data), size * nmemb);
                    if (header.endsWith("\r\n"))
                        header.chop(2);
                    int colon = header.indexOf(':');
                    if (colon != -1) {
                        String &value = response->responseHeaders[header.left(colon)];
                        do {
                            ++colon;
                        } while (colon < header.size() && isspace(header[colon]));
                        if (colon < header.size())
                            value = header.mid(colon);
                    }
                    return size * nmemb;
                })));

    C(curl_easy_perform(easy));
    {
        long httpStatusCode;
        C(curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpStatusCode));
        response.statusCode = httpStatusCode;
    }
exit:
    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(easy);
    return response;
}

