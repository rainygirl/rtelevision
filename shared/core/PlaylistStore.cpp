#include "PlaylistStore.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

#include "M3UParser.h"
#include "Paths.h"
#include "StringUtil.h"

namespace tv {
namespace {

// A download must look like a playlist and carry a plausible number of entries
// before it is allowed to overwrite a copy that is known to work.
const size_t kMinAcceptableChannels = 16;

bool looksLikePlaylist(const std::string& body) {
    std::string head = body.substr(0, 64);
    return head.find("#EXTM3U") != std::string::npos ||
           head.find("#EXTINF") != std::string::npos;
}

}  // namespace

PlaylistStore::PlaylistStore(std::string url, std::string dataDir, std::string seedPath)
    : url_(std::move(url)), dataDir_(std::move(dataDir)), seedPath_(std::move(seedPath)) {
    ensureDirectory(dataDir_);
}

std::string PlaylistStore::cachePath() const { return joinPath(dataDir_, "playlist.m3u"); }
std::string PlaylistStore::backupPath() const { return joinPath(dataDir_, "playlist.bak.m3u"); }
std::string PlaylistStore::metaPath() const { return joinPath(dataDir_, "playlist.meta"); }

PlaylistStore::Meta PlaylistStore::readMeta() const {
    Meta meta;
    std::string text;
    if (!readFile(metaPath(), text)) return meta;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key == "etag") meta.etag = value;
        else if (key == "last-modified") meta.lastModified = value;
        else if (key == "fetched-at") meta.fetchedAt = static_cast<std::time_t>(std::strtoll(value.c_str(), nullptr, 10));
        else if (key == "channels") meta.channelCount = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
    }
    return meta;
}

void PlaylistStore::writeMeta(const Meta& meta) const {
    std::ostringstream out;
    out << "url=" << url_ << "\n"
        << "etag=" << meta.etag << "\n"
        << "last-modified=" << meta.lastModified << "\n"
        << "fetched-at=" << static_cast<long long>(meta.fetchedAt) << "\n"
        << "channels=" << meta.channelCount << "\n";
    writeFileAtomic(metaPath(), out.str());
}

std::time_t PlaylistStore::cachedAt() const { return readMeta().fetchedAt; }

bool PlaylistStore::loadLocal(PlaylistSnapshot& out) const {
    struct Candidate {
        std::string path;
        PlaylistSource source;
    };
    const Candidate candidates[] = {
        {cachePath(), PlaylistSource::Cache},
        {backupPath(), PlaylistSource::Cache},
        {seedPath_, PlaylistSource::Seed},
    };

    for (const Candidate& c : candidates) {
        if (c.path.empty() || !fileExists(c.path)) continue;
        std::string text;
        if (!readFile(c.path, text) || text.empty()) continue;
        ParseResult parsed = parseM3U(text);
        if (parsed.channels.empty()) continue;
        out.channels = std::move(parsed.channels);
        out.source = c.source;
        out.fetchedAt = (c.source == PlaylistSource::Cache) ? cachedAt() : 0;
        return true;
    }
    return false;
}

RefreshResult PlaylistStore::refresh(HttpClient& http, PlaylistSnapshot& out,
                                     const std::function<void(double)>& onProgress) {
    RefreshResult result;
    Meta meta = readMeta();

    HttpRequest request;
    request.url = url_;
    request.onProgress = onProgress;
    // Conditional GET only makes sense while a usable cache exists.
    if (fileExists(cachePath())) {
        request.etag = meta.etag;
        request.lastModified = meta.lastModified;
    }

    HttpResponse resp = http.get(request);
    if (!resp.error.empty()) {
        result.error = resp.error;
        return result;
    }
    result.succeeded = true;

    if (resp.notModified) {
        result.notModified = true;
        result.channelCount = meta.channelCount;
        meta.fetchedAt = std::time(nullptr);
        writeMeta(meta);
        return result;
    }

    if (!looksLikePlaylist(resp.body)) {
        result.succeeded = false;
        result.error = "response is not an M3U playlist";
        return result;
    }

    ParseResult parsed = parseM3U(resp.body);
    if (parsed.channels.size() < kMinAcceptableChannels) {
        result.succeeded = false;
        result.error = "playlist has too few channels (" +
                       std::to_string(parsed.channels.size()) + "); keeping cached copy";
        return result;
    }

    // Rotate the previous good copy before overwriting it.
    if (fileExists(cachePath())) {
        std::string previous;
        if (readFile(cachePath(), previous) && !previous.empty())
            writeFileAtomic(backupPath(), previous);
    }
    if (!writeFileAtomic(cachePath(), resp.body)) {
        result.succeeded = false;
        result.error = "could not write cache file";
        return result;
    }

    meta.etag = resp.etag;
    meta.lastModified = resp.lastModified;
    meta.fetchedAt = std::time(nullptr);
    meta.channelCount = parsed.channels.size();
    writeMeta(meta);

    out.channels = std::move(parsed.channels);
    out.source = PlaylistSource::Network;
    out.fetchedAt = meta.fetchedAt;

    result.updated = true;
    result.channelCount = out.channels.size();
    return result;
}

}  // namespace tv
