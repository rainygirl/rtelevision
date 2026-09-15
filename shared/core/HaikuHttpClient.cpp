// HttpClient backed by Haiku's own network services (libnetservices).
//
// Built instead of CurlHttpClient.cpp where curl_devel is not installed - which
// is the situation on Haiku/arm64, whose HaikuPorts repository currently
// publishes no curl package.
#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <OS.h>
#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "HttpClient.h"

namespace tv {
namespace {

using BPrivate::Network::BHttpHeaders;
using BPrivate::Network::BHttpRequest;
using BPrivate::Network::BHttpResult;
using BPrivate::Network::BUrlProtocolRoster;
using BPrivate::Network::BUrlRequest;

class HaikuHttpClient : public HttpClient {
public:
    HttpResponse get(const HttpRequest& request) override {
        HttpResponse resp;
        resp.effectiveUrl = request.url;

        // Two BUrl(const char*) overloads exist; name the encode flag to pick one.
        BUrl url(request.url.c_str(), false);
        if (!url.IsValid()) {
            resp.error = "invalid URL: " + request.url;
            return resp;
        }

        BMallocIO output;
        BUrlRequest* raw = BUrlProtocolRoster::MakeRequest(url, &output, NULL, NULL);
        if (raw == NULL) {
            resp.error = "no protocol handler for " + request.url;
            return resp;
        }
        std::unique_ptr<BUrlRequest> owned(raw);

        BHttpRequest* http = dynamic_cast<BHttpRequest*>(raw);
        if (http != NULL) {
            http->SetFollowLocation(true);
            BHttpHeaders headers;
            headers.AddHeader("User-Agent", request.userAgent.empty()
                                                ? "RTelevision/1.0"
                                                : request.userAgent.c_str());
            if (!request.referrer.empty()) headers.AddHeader("Referer", request.referrer.c_str());
            if (request.rangeFirst >= 0) {
                std::string range = "bytes=" + std::to_string(request.rangeFirst) + "-";
                if (request.rangeLast >= 0) range += std::to_string(request.rangeLast);
                headers.AddHeader("Range", range.c_str());
            }
            if (!request.etag.empty())
                headers.AddHeader("If-None-Match", request.etag.c_str());
            if (!request.lastModified.empty())
                headers.AddHeader("If-Modified-Since", request.lastModified.c_str());
            http->SetHeaders(headers);
        }
        raw->SetTimeout(static_cast<bigtime_t>(request.timeoutSeconds) * 1000000LL);

        thread_id thread = raw->Run();
        if (thread < 0) {
            resp.error = "could not start the request";
            return resp;
        }
        // Poll rather than block, so a caller that no longer wants the data -
        // the relay moving on to another channel - is not held up by a slow server.
        bool aborted = false;
        while (raw->IsRunning()) {
            if (request.shouldAbort && request.shouldAbort()) {
                raw->Stop();
                aborted = true;
                break;
            }
            snooze(50000);
        }
        status_t exitValue = B_OK;
        wait_for_thread(thread, &exitValue);

        if (http != NULL) {
            const BHttpResult& result = dynamic_cast<const BHttpResult&>(raw->Result());
            resp.status = result.StatusCode();
            const BHttpHeaders& responseHeaders = result.Headers();
            const char* etag = responseHeaders["ETag"];
            if (etag != NULL) resp.etag = etag;
            const char* lastModified = responseHeaders["Last-Modified"];
            if (lastModified != NULL) resp.lastModified = lastModified;
            const char* contentRange = responseHeaders["Content-Range"];
            if (resp.status == 206 && contentRange != NULL) {
                const char* slash = std::strrchr(contentRange, '/');
                if (slash != NULL && slash[1] != '\0' && slash[1] != '*')
                    resp.totalLength = std::strtoll(slash + 1, NULL, 10);
            }
        } else {
            resp.status = 200;  // file:// and friends carry no status line
        }

        resp.notModified = (resp.status == 304);
        if (output.BufferLength() > 0) {
            resp.body.assign(static_cast<const char*>(output.Buffer()),
                             static_cast<size_t>(output.BufferLength()));
        }
        if (resp.status == 200 && !aborted) resp.totalLength = static_cast<int64_t>(resp.body.size());

        if (aborted)
            resp.error = "aborted";
        else if (raw->Status() != B_OK && resp.status == 0)
            resp.error = "transfer failed (" + std::to_string(raw->Status()) + ")";
        else if (resp.status != 200 && resp.status != 206 && resp.status != 304)
            resp.error = "HTTP " + std::to_string(resp.status);
        return resp;
    }
};

}  // namespace

std::unique_ptr<HttpClient> makeHttpClient() {
    return std::unique_ptr<HttpClient>(new HaikuHttpClient());
}

}  // namespace tv
