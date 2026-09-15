// Ties the playlist store, channel index, favorites and player together so a
// front end only has to render and forward user intent.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ChannelIndex.h"
#include "Favorites.h"
#include "HttpClient.h"
#include "MediaPlayer.h"
#include "PlaylistStore.h"

namespace tv {

// Default source. Overridable at runtime via the RTV_PLAYLIST_URL env var.
extern const char* const kDefaultPlaylistUrl;

struct AppPaths {
    std::string dataDir;    // cache + settings
    std::string seedPath;   // bundled fallback playlist, may be empty
    std::string pluginPath; // VLC plugin tree, may be empty
};

class AppController {
public:
    // Fired from a worker thread; front ends must marshal to their UI thread.
    using RefreshCallback = std::function<void(const RefreshResult&)>;

    explicit AppController(AppPaths paths);
    ~AppController();

    // Loads cache or seed synchronously. Returns false when nothing is available.
    bool loadLocalPlaylist();

    // Downloads in the background. Only one refresh runs at a time.
    void refreshAsync(RefreshCallback done);
    bool refreshing() const { return refreshing_; }

    bool startPlayer(std::string* errorOut);
    MediaPlayer* player() { return player_.get(); }

    ChannelIndex& index() { return index_; }
    Favorites& favorites() { return favorites_; }
    PlaylistStore& store() { return store_; }
    PlaylistSource playlistSource() const { return source_; }
    std::time_t playlistFetchedAt() const { return fetchedAt_; }

    void toggleFavorite(const std::string& url);

private:
    void applySnapshot(const PlaylistSnapshot& snapshot);

    AppPaths paths_;
    PlaylistStore store_;
    Favorites favorites_;
    ChannelIndex index_;
    std::unique_ptr<MediaPlayer> player_;
    std::unique_ptr<HttpClient> http_;

    std::thread worker_;
    std::mutex mutex_;
    bool refreshing_ = false;
    PlaylistSource source_ = PlaylistSource::None;
    std::time_t fetchedAt_ = 0;
};

}  // namespace tv
