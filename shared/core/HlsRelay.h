// Local HLS relay for streams whose server is slower than their own bitrate.
//
// Some channels are a single fixed-quality stream behind a distant server: one
// connection delivers each segment slower than it plays, so the player stalls
// however much it buffers. The relay sits between the player and that server on
// 127.0.0.1. It fetches every segment over several connections at once (HTTP
// Range requests), keeps a lead of finished segments, and hands the player a
// playlist listing only segments it already holds, so the player never waits
// on the network in the middle of one.
//
// Only live media playlists with a single rendition are relayed. Adaptive
// streams already cope by dropping to a lower quality, and VOD has no live edge
// to fall behind, so both reach the player untouched.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "Channel.h"
#include "HttpClient.h"
#include "MediaPlayer.h"

namespace tv {

struct RelaySettings {
    int connections = 4;               // parallel range requests per segment
    double startBufferSeconds = 20;    // video held back before the player starts
    double maxStartWaitSeconds = 40;   // ...unless the server is too slow to get there
    bool verbose = false;

    // RTV_RELAY_CONNECTIONS, RTV_RELAY_BUFFER (seconds) and RTV_VERBOSE.
    static RelaySettings fromEnvironment();
};

class RelayServer;

class HlsRelay {
public:
    // How much of the start buffer is filled, 0-100. Called on a relay thread.
    using ProgressCallback = std::function<void(int percent)>;

    HlsRelay(std::shared_ptr<HttpClient> http, RelaySettings settings);
    ~HlsRelay();

    // Cheap test on the address alone: is this worth probing at all?
    static bool looksLikeHls(const std::string& url);

    // Blocking. Fetches the channel's playlist and, if it can be relayed, starts
    // a session for it and returns the 127.0.0.1 address the player should open.
    // Otherwise returns the channel's own URL. `cancelled` is polled while the
    // network is busy. Any previous session ends first.
    std::string open(const Channel& channel, const std::function<bool()>& cancelled,
                     bool* relayed);

    // Ends the current session. Its downloads wind down in the background.
    void close();

    void setProgressCallback(ProgressCallback callback);

private:
    std::shared_ptr<HttpClient> http_;
    RelaySettings settings_;
    std::mutex mutex_;
    std::unique_ptr<RelayServer> server_;
    ProgressCallback progress_;
};

// Wraps a backend so that play() goes through the relay first. Front ends keep
// talking to an ordinary MediaPlayer.
std::unique_ptr<MediaPlayer> makeRelayedMediaPlayer(std::unique_ptr<MediaPlayer> inner,
                                                    std::shared_ptr<HttpClient> http,
                                                    RelaySettings settings);

}  // namespace tv
