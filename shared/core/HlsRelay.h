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

    // Hand the player one continuous MPEG-TS body instead of a playlist, for a
    // backend with no HLS demuxer of its own - libVLC 0.9 on Mac OS X 10.4.
    // The relay then also decrypts AES-128 segments, which such a player cannot
    // do, and holds back much less video: writing blocks until the next segment
    // is ready, so the stream paces itself.
    bool plainTransportStream = false;

    // RTV_RELAY_CONNECTIONS, RTV_RELAY_BUFFER (seconds), RTV_RELAY_TS and
    // RTV_VERBOSE.
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
    // Otherwise returns the URL the player should open directly: the channel's
    // own, or where that redirected to if the playlist was fetched (some
    // backends stall on a redirect that the probe already followed).
    // `cancelled` is polled while the network is busy. Any previous session
    // ends first.
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

// Serves channels to a player that cannot read HLS itself.
//
// VLC 0.9 (the newest that runs on Mac OS X 10.4 PowerPC) and older set-top
// boxes have no HLS demuxer at all: they can play an MPEG-TS stream over HTTP
// and nothing more. The relay already does the part they are missing - it
// parses the playlist and fetches the segments itself - so the tuner simply
// writes those segments out back to back as one continuous MPEG-TS body. The
// player only ever sees a plain stream.
//
//   GET /tune?u=<percent-encoded channel URL>   the channel, as MPEG-TS
//
// One player at a time: a new request ends the session before it.
//
// Two kinds of channel cannot be served this way and are refused with a reason:
// AES-128 encrypted streams (the segments would have to be decrypted first) and
// fragmented MP4 streams (not MPEG-TS, so concatenating them means nothing).
// Where a channel offers several renditions the smallest is chosen, which is
// also what the machines this exists for can actually decode.
class HlsTuner {
public:
    HlsTuner(std::shared_ptr<HttpClient> http, RelaySettings settings);
    ~HlsTuner();

    // port 0 picks a free one. `lan` binds every interface rather than only
    // loopback, so another machine can reach it.
    bool start(int port, bool lan, std::string* error);
    int port() const;

    // Serves until stop() is called from another thread.
    void run();
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tv
