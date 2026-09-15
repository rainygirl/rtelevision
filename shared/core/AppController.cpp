#include "AppController.h"

#include <cstdlib>
#include <utility>

#include "HlsRelay.h"

namespace tv {

const char* const kDefaultPlaylistUrl = "https://iptv-org.github.io/iptv/index.m3u";

namespace {

std::string playlistUrl() {
    const char* override = std::getenv("RTV_PLAYLIST_URL");
    return (override && *override) ? std::string(override) : std::string(kDefaultPlaylistUrl);
}

}  // namespace

AppController::AppController(AppPaths paths)
    : paths_(std::move(paths)),
      store_(playlistUrl(), paths_.dataDir, paths_.seedPath),
      favorites_(paths_.dataDir),
      http_(makeHttpClient()) {
    favorites_.load();
    index_.setFavorites(favorites_.urls());
}

AppController::~AppController() {
    if (worker_.joinable()) worker_.join();
}

void AppController::applySnapshot(const PlaylistSnapshot& snapshot) {
    index_.setChannels(snapshot.channels);
    index_.setFavorites(favorites_.urls());
    source_ = snapshot.source;
    fetchedAt_ = snapshot.fetchedAt;
}

bool AppController::loadLocalPlaylist() {
    PlaylistSnapshot snapshot;
    if (!store_.loadLocal(snapshot)) return false;
    applySnapshot(snapshot);
    return true;
}

void AppController::refreshAsync(RefreshCallback done) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (refreshing_) return;
        refreshing_ = true;
    }
    if (worker_.joinable()) worker_.join();

    worker_ = std::thread([this, done]() {
        PlaylistSnapshot snapshot;
        RefreshResult result = store_.refresh(*http_, snapshot);
        if (result.updated) {
            // Publishing the parsed list here is safe: the front end only reads
            // the index from its UI thread after `done` marshals back to it.
            applySnapshot(snapshot);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            refreshing_ = false;
        }
        if (done) done(result);
    });
}

bool AppController::startPlayer(std::string* errorOut) {
    MediaPlayerConfig config;
    config.pluginPath = paths_.pluginPath;
    config.verbose = std::getenv("RTV_VERBOSE") != nullptr;

    // Escape hatch for backend tuning without a rebuild, e.g.
    // RTV_VLC_ARGS="--vout=xcb_x11" to force a non-overlay video output.
    if (const char* extra = std::getenv("RTV_VLC_ARGS")) {
        std::string current;
        for (const char* p = extra;; ++p) {
            if (*p == ' ' || *p == '\0') {
                if (!current.empty()) config.extraArgs.push_back(current);
                current.clear();
                if (*p == '\0') break;
            } else {
                current.push_back(*p);
            }
        }
    }
    player_ = makeMediaPlayer(config, errorOut);
    // Live streams whose server cannot keep up with their own bitrate play
    // through a local relay that fetches ahead over several connections.
    // RTV_NO_HLS_RELAY=1 plays everything directly.
    if (player_ && !std::getenv("RTV_NO_HLS_RELAY")) {
        player_ = makeRelayedMediaPlayer(std::move(player_),
                                         std::shared_ptr<HttpClient>(makeHttpClient()),
                                         RelaySettings::fromEnvironment());
    }
    return player_ != nullptr;
}

void AppController::toggleFavorite(const std::string& url) {
    favorites_.toggle(url);
    index_.setFavorites(favorites_.urls());
}

}  // namespace tv
