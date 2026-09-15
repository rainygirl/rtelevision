// libcurl-backed HttpClient, used on macOS and Linux.
#include <curl/curl.h>

#include <cstdlib>
#include <cstring>

#include "HttpClient.h"
#include "StringUtil.h"

namespace tv {
namespace {

// One easy handle per thread, reset between requests. curl keeps a handle's
// connection cache across curl_easy_reset(), so a thread that fetches segment
// after segment from the same server reuses its connection instead of paying
// for a new handshake and TCP slow start every time.
struct ThreadHandle {
    CURL* curl = nullptr;
    ~ThreadHandle() {
        if (curl) curl_easy_cleanup(curl);
    }
};

CURL* threadHandle() {
    thread_local ThreadHandle handle;
    if (handle.curl) curl_easy_reset(handle.curl);
    else handle.curl = curl_easy_init();
    return handle.curl;
}

struct HeaderState {
    HttpResponse* response;
    int64_t rangeTotal = -1;
};

size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t writeHeader(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* state = static_cast<HeaderState*>(userdata);
    const size_t len = size * nmemb;
    std::string line(ptr, len);
    // Each response of a redirect chain delivers its own headers; keep the last.
    if (startsWith(line, "HTTP/")) {
        state->response->etag.clear();
        state->response->lastModified.clear();
        state->rangeTotal = -1;
        return len;
    }
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = toLower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (key == "etag") {
            state->response->etag = value;
        } else if (key == "last-modified") {
            state->response->lastModified = value;
        } else if (key == "content-range") {
            // "bytes 0-262143/5685120"; the total may be "*" when unknown.
            size_t slash = value.rfind('/');
            if (slash != std::string::npos && slash + 1 < value.size() && value[slash + 1] != '*')
                state->rangeTotal = std::strtoll(value.c_str() + slash + 1, nullptr, 10);
        }
    }
    return len;
}

int progressCallback(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    const auto* request = static_cast<const HttpRequest*>(userdata);
    if (request->shouldAbort && request->shouldAbort()) return 1;  // aborts the transfer
    if (request->onProgress)
        request->onProgress(dltotal > 0 ? static_cast<double>(dlnow) / static_cast<double>(dltotal)
                                        : -1.0);
    return 0;
}

class CurlHttpClient : public HttpClient {
public:
    CurlHttpClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }

    HttpResponse get(const HttpRequest& request) override {
        HttpResponse resp;
        CURL* curl = threadHandle();
        if (!curl) {
            resp.error = "curl_easy_init failed";
            return resp;
        }

        struct curl_slist* headers = nullptr;
        if (!request.etag.empty())
            headers = curl_slist_append(headers, ("If-None-Match: " + request.etag).c_str());
        if (!request.lastModified.empty())
            headers = curl_slist_append(headers,
                                        ("If-Modified-Since: " + request.lastModified).c_str());

        std::string range;
        if (request.rangeFirst >= 0) {
            range = std::to_string(request.rangeFirst) + "-";
            if (request.rangeLast >= 0) range += std::to_string(request.rangeLast);
        }

        HeaderState headerState;
        headerState.response = &resp;

        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        // Several threads use curl at once; timeouts must not rely on signals.
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        // Compression only for whole documents: a byte range has to map onto
        // the file as stored, not onto a gzip stream.
        if (range.empty()) curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        else curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         request.userAgent.empty() ? "RTelevision/1.0" : request.userAgent.c_str());
        if (!request.referrer.empty()) curl_easy_setopt(curl, CURLOPT_REFERER, request.referrer.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerState);
        if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (request.onProgress || request.shouldAbort) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<HttpRequest*>(&request));
        }

        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_ABORTED_BY_CALLBACK) resp.error = "aborted";
        else if (rc != CURLE_OK) resp.error = curl_easy_strerror(rc);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
        char* effective = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
        resp.effectiveUrl = effective ? effective : request.url;
        resp.notModified = (resp.status == 304);
        if (resp.status == 206) resp.totalLength = headerState.rangeTotal;
        else if (resp.status == 200 && rc == CURLE_OK)
            resp.totalLength = static_cast<int64_t>(resp.body.size());

        // The handle outlives this call; do not leave it pointing at freed headers.
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
            curl_slist_free_all(headers);
        }

        if (resp.error.empty() && resp.status != 200 && resp.status != 206 && resp.status != 304)
            resp.error = "HTTP " + std::to_string(resp.status);
        return resp;
    }
};

}  // namespace

std::unique_ptr<HttpClient> makeHttpClient() {
    return std::unique_ptr<HttpClient>(new CurlHttpClient());
}

}  // namespace tv
