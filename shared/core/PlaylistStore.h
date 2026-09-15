// Offline-first playlist storage.
//
// Policy: the app must keep working when the remote playlist disappears.
//   1. A seed snapshot ships with the app and is used on a cold first run.
//   2. Every successful download is written atomically to the cache dir, with
//      the previous good copy kept as a .bak.
//   3. A failed, empty or unparseable download never replaces the cache.
#pragma once

#include <ctime>
#include <string>

#include "Channel.h"
#include "HttpClient.h"

namespace tv {

enum class PlaylistSource { None, Seed, Cache, Network };

struct PlaylistSnapshot {
    ChannelList channels;
    PlaylistSource source = PlaylistSource::None;
    std::time_t fetchedAt = 0;
};

struct RefreshResult {
    bool succeeded = false;   // network transaction completed
    bool updated = false;     // cache actually replaced with new content
    bool notModified = false; // server said 304, cache is current
    std::string error;
    size_t channelCount = 0;
};

class PlaylistStore {
public:
    PlaylistStore(std::string url, std::string dataDir, std::string seedPath);

    // Reads cache, falling back to the bundled seed. Never hits the network.
    bool loadLocal(PlaylistSnapshot& out) const;

    // Downloads and, on success, replaces the cache. Safe to call on a worker
    // thread; touches only files owned by this store.
    RefreshResult refresh(HttpClient& http, PlaylistSnapshot& out,
                          const std::function<void(double)>& onProgress = nullptr);

    std::string cachePath() const;
    std::string backupPath() const;
    std::string metaPath() const;
    std::time_t cachedAt() const;
    const std::string& url() const { return url_; }

private:
    struct Meta {
        std::string etag;
        std::string lastModified;
        std::time_t fetchedAt = 0;
        size_t channelCount = 0;
    };

    Meta readMeta() const;
    void writeMeta(const Meta& meta) const;

    std::string url_;
    std::string dataDir_;
    std::string seedPath_;
};

}  // namespace tv
