// Minimal HTTP GET abstraction so the core never touches a platform API.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tv {

struct HttpResponse {
    long status = 0;
    bool notModified = false;   // server answered 304
    std::string body;
    std::string etag;
    std::string lastModified;
    std::string effectiveUrl;   // after redirects; relative links resolve against it
    int64_t totalLength = -1;   // size of the whole resource, even for a range; -1 unknown
    std::string error;          // non-empty when the transfer itself failed

    bool ok() const { return error.empty() && (status == 200 || status == 206 || notModified); }
};

struct HttpRequest {
    std::string url;
    std::string etag;           // sent as If-None-Match when set
    std::string lastModified;   // sent as If-Modified-Since when set
    std::string userAgent;      // empty for the client's own
    std::string referrer;
    // Inclusive byte range. rangeFirst < 0 asks for the whole resource,
    // rangeLast < 0 for everything from rangeFirst on.
    int64_t rangeFirst = -1;
    int64_t rangeLast = -1;
    long timeoutSeconds = 60;
    std::function<void(double fraction)> onProgress;  // optional, may be called often
    // Optional, polled during the transfer; returning true abandons it.
    std::function<bool()> shouldAbort;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    // Safe to call from several threads at once.
    virtual HttpResponse get(const HttpRequest& request) = 0;
};

// Returns the backend compiled in for this platform.
std::unique_ptr<HttpClient> makeHttpClient();

}  // namespace tv
