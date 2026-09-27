// libVLC 0.9 backend, for Mac OS X 10.4 on PowerPC.
//
// 0.9.10 is the last VLC that runs on Tiger, and its libvlc is a different API
// from 1.1 onwards: every call takes a libvlc_exception_t, volume and mute live
// on the instance rather than the player, and the video surface is a
// libvlc_drawable_t - an int, which holds an NSView* only because this platform
// is 32-bit. It also has no HLS demuxer at all, so channels reach it through
// the relay's plain MPEG-TS mode (see HlsRelay.h) and it never sees a playlist.
#include <vlc/vlc.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MediaPlayer.h"

namespace tv {
namespace {

// The picture size the vmem output is told to produce. vmem is configured once,
// when the instance is created, so this cannot follow the window; the front end
// letterboxes whatever it gets into the view. VLC scales the stream into it and
// keeps the stream's aspect, so a 4:3 channel gets pillars rather than stretch.
const unsigned kVideoWidth = 640;
const unsigned kVideoHeight = 360;
// RV24 on a big-endian machine is plain R, G, B bytes, which is what an
// NSBitmapImageRep takes without any shuffling.
const unsigned kVideoBytesPerPixel = 3;

// Every 0.9 entry point takes one of these and there is nothing useful to do
// with most failures, so it is initialised and cleared in one place.
class Ex {
public:
    Ex() { libvlc_exception_init(&value_); }
    ~Ex() { libvlc_exception_clear(&value_); }
    libvlc_exception_t* get() { return &value_; }
    bool raised() { return libvlc_exception_raised(&value_) != 0; }
    std::string message() {
        const char* text = libvlc_exception_get_message(&value_);
        return text ? text : "libvlc error";
    }

private:
    libvlc_exception_t value_;
};

class VlcLegacyMediaPlayer : public MediaPlayer {
public:
    ~VlcLegacyMediaPlayer() noexcept override {
        {
            std::lock_guard<std::mutex> lock(watchdogMutex_);
            quit_ = true;
        }
        watchdogCv_.notify_all();
        if (watchdog_.joinable()) watchdog_.join();
        if (player_) {
            Ex ex;
            libvlc_media_player_stop(player_, ex.get());
            libvlc_media_player_release(player_);
        }
        if (instance_) libvlc_release(instance_);
    }

    bool start(const MediaPlayerConfig& config, std::string* errorOut) {
        if (!config.pluginPath.empty()) setenv("VLC_PLUGIN_PATH", config.pluginPath.c_str(), 1);

        // 0.9's option set is not 3.x's: there is no --network-caching (the
        // access modules each have their own) and no --no-osd. Passing one it
        // does not know makes libvlc_new fail outright rather than warn.
        std::vector<std::string> args;
        args.push_back("--no-video-title-show");
        args.push_back("--no-snapshot-preview");
        args.push_back("--no-stats");
        args.push_back("--http-caching=1500");
        args.push_back("--intf=dummy");
        args.push_back("--vout=vmem");

        // H.264 is hard work for a G4, so the deblocking filter is skipped on
        // the frames nothing refers back to. That is worth roughly a fifth of
        // the decode and costs almost nothing to look at.
        //
        // Deliberately not --ffmpeg-hurry-up or --skip-frames: both let the
        // decoder abandon reference frames, and the picture then falls apart
        // into blocks that persist until the next key frame. Dropping a late
        // picture at the output instead keeps the sound in step without
        // damaging what has already been decoded.
        args.push_back("--ffmpeg-skiploopfilter=1");
        args.push_back("--drop-late-frames");
        args.push_back(config.verbose ? "--verbose=2" : "--quiet");

        // Software video output. This hardware has no Quartz Extreme, and
        // libVLC's OpenGL output refuses to start without it, so the picture is
        // written into a buffer the front end draws. vmem reads its settings
        // from the configuration, not from per-media options, so they have to
        // be here rather than at play() time.
        char option[96];
        std::snprintf(option, sizeof option, "--vmem-width=%u", kVideoWidth);
        args.push_back(option);
        std::snprintf(option, sizeof option, "--vmem-height=%u", kVideoHeight);
        args.push_back(option);
        std::snprintf(option, sizeof option, "--vmem-pitch=%u",
                      kVideoWidth * kVideoBytesPerPixel);
        args.push_back(option);
        args.push_back("--vmem-chroma=RV24");
        std::snprintf(option, sizeof option, "--vmem-lock=%lu",
                      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(&vmemLock)));
        args.push_back(option);
        std::snprintf(option, sizeof option, "--vmem-unlock=%lu",
                      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(&vmemUnlock)));
        args.push_back(option);
        std::snprintf(option, sizeof option, "--vmem-data=%lu",
                      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(this)));
        args.push_back(option);

        args.insert(args.end(), config.extraArgs.begin(), config.extraArgs.end());

        std::vector<const char*> argv;
        argv.reserve(args.size());
        for (size_t i = 0; i < args.size(); ++i) argv.push_back(args[i].c_str());

        Ex ex;
        instance_ = libvlc_new(static_cast<int>(argv.size()), &argv[0], ex.get());
        if (!instance_ || ex.raised()) {
            if (errorOut) *errorOut = "libvlc_new failed: " + ex.message();
            return false;
        }
        player_ = libvlc_media_player_new(instance_, ex.get());
        if (!player_ || ex.raised()) {
            if (errorOut) *errorOut = "libvlc_media_player_new failed: " + ex.message();
            return false;
        }

        libvlc_event_manager_t* events = libvlc_media_player_event_manager(player_, ex.get());
        if (events) {
            static const libvlc_event_type_t kWanted[] = {
                libvlc_MediaPlayerOpening,  libvlc_MediaPlayerBuffering,
                libvlc_MediaPlayerPlaying,  libvlc_MediaPlayerPaused,
                libvlc_MediaPlayerStopped,  libvlc_MediaPlayerEndReached,
                libvlc_MediaPlayerEncounteredError,
            };
            for (size_t i = 0; i < sizeof kWanted / sizeof kWanted[0]; ++i) {
                Ex attach;
                libvlc_event_attach(events, kWanted[i], &VlcLegacyMediaPlayer::onEvent, this,
                                    attach.get());
            }
        }

        applyVolume();
        watchdog_ = std::thread(&VlcLegacyMediaPlayer::watch, this);
        return true;
    }

    // The sink owns the picture buffer; vmem writes straight into it. It must
    // agree with the size the instance was created with.
    bool attachVideoSink(VideoFrameSink* sink) override {
        sink_ = sink;
        if (!sink) return true;
        unsigned width = kVideoWidth;
        unsigned height = kVideoHeight;
        const unsigned pitch = sink->setupFormat(width, height);
        return pitch == kVideoWidth * kVideoBytesPerPixel && width == kVideoWidth &&
               height == kVideoHeight;
    }

    // A libvlc_drawable_t is an int. That is enough for an NSView* here and
    // nowhere else, which is why 1.1 replaced it with set_nsobject.
    bool attachVideoView(void* nativeView) override {
        if (!player_ || !nativeView) return false;
        Ex ex;
        libvlc_media_player_set_drawable(
            player_, static_cast<libvlc_drawable_t>(reinterpret_cast<intptr_t>(nativeView)),
            ex.get());
        return !ex.raised();
    }

    void detachVideoView() override {
        if (!player_) return;
        Ex ex;
        libvlc_media_player_set_drawable(player_, 0, ex.get());
    }

    bool play(const Channel& channel) override {
        if (!player_ || channel.url.empty()) return false;
        Ex ex;
        libvlc_media_t* media = libvlc_media_new(instance_, channel.url.c_str(), ex.get());
        if (!media || ex.raised()) {
            notify(PlaybackState::Error, "could not open " + channel.url);
            return false;
        }
        for (size_t i = 0; i < channel.options.size(); ++i) {
            // rtv-relay only tells a modern backend to hold back the live edge;
            // here the relay hands over a plain stream with no live edge at all.
            if (channel.options[i].first == "rtv-relay") continue;
            const std::string option = ":" + channel.options[i].first + "=" + channel.options[i].second;
            Ex add;
            libvlc_media_add_option(media, option.c_str(), add.get());
        }
        Ex set;
        libvlc_media_player_set_media(player_, media, set.get());
        libvlc_media_release(media);

        paused_ = false;
        playing_ = false;
        notify(PlaybackState::Opening, channel.name);
        Ex go;
        libvlc_media_player_play(player_, go.get());
        return !go.raised();
    }

    void stop() override {
        if (!player_) return;
        Ex ex;
        libvlc_media_player_stop(player_, ex.get());
        paused_ = false;
        playing_ = false;
        notify(PlaybackState::Stopped, std::string());
    }

    // 0.9 has no set_pause, only a toggle, so the current state decides.
    void setPaused(bool paused) override {
        if (!player_ || paused == paused_) return;
        Ex can;
        if (paused && libvlc_media_player_can_pause(player_, can.get()) == 0) return;
        Ex ex;
        libvlc_media_player_pause(player_, ex.get());
        if (!ex.raised()) paused_ = paused;
    }

    bool isPaused() const override { return paused_; }

    void setVolume(int percent) override {
        volume_ = percent < 0 ? 0 : (percent > 200 ? 200 : percent);
        applyVolume();
    }

    int volume() const override { return volume_; }

    void setMuted(bool muted) override {
        muted_ = muted;
        if (!instance_) return;
        Ex ex;
        libvlc_audio_set_mute(instance_, muted ? 1 : 0, ex.get());
    }

    bool isMuted() const override { return muted_; }

    PlaybackState state() const override { return state_.load(); }

    void setStateCallback(StateCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback_ = callback;
    }

    std::string backendName() const override {
        return std::string("libVLC ") + libvlc_get_version() + " (legacy API)";
    }

private:
    void applyVolume() {
        if (!instance_) return;
        Ex ex;
        libvlc_audio_set_volume(instance_, volume_, ex.get());
    }

    // vmem hands these two the pointer given as --vmem-data.
    static void* vmemLock(void* opaque) {
        VlcLegacyMediaPlayer* self = static_cast<VlcLegacyMediaPlayer*>(opaque);
        if (!self || !self->sink_) return NULL;
        return self->sink_->lockFrame();
    }

    static void vmemUnlock(void* opaque) {
        VlcLegacyMediaPlayer* self = static_cast<VlcLegacyMediaPlayer*>(opaque);
        if (!self || !self->sink_) return;
        self->sink_->unlockFrame();
        self->sink_->displayFrame();
    }

    static void onEvent(const libvlc_event_t* event, void* opaque) {
        VlcLegacyMediaPlayer* self = static_cast<VlcLegacyMediaPlayer*>(opaque);
        switch (event->type) {
            case libvlc_MediaPlayerOpening: self->notify(PlaybackState::Opening, ""); break;
            // 0.9's buffering event carries no percentage; the relay reports its
            // own start-buffer progress, which is the number worth showing.
            case libvlc_MediaPlayerBuffering: self->notify(PlaybackState::Buffering, ""); break;
            case libvlc_MediaPlayerPlaying:
                self->playing_ = true;
                self->notify(PlaybackState::Playing, "");
                break;
            case libvlc_MediaPlayerPaused: self->notify(PlaybackState::Paused, ""); break;
            case libvlc_MediaPlayerStopped:
                self->playing_ = false;
                self->notify(PlaybackState::Stopped, "");
                break;
            case libvlc_MediaPlayerEndReached:
                self->playing_ = false;
                self->notify(PlaybackState::Stopped, "stream ended");
                break;
            case libvlc_MediaPlayerEncounteredError:
                self->playing_ = false;
                self->notify(PlaybackState::Error, "stream unavailable");
                break;
            default: break;
        }
    }

    // As in the modern backend: an input that ends without an event would
    // otherwise leave the window waiting for ever.
    void watch() {
        int strikes = 0;
        std::unique_lock<std::mutex> lock(watchdogMutex_);
        while (!watchdogCv_.wait_for(lock, std::chrono::milliseconds(500),
                                     [this] { return quit_; })) {
            const PlaybackState ours = state_.load();
            if (ours != PlaybackState::Opening && ours != PlaybackState::Buffering) {
                strikes = 0;
                continue;
            }
            Ex ex;
            const libvlc_state_t theirs = libvlc_media_player_get_state(player_, ex.get());
            if (theirs != libvlc_Ended && theirs != libvlc_Error) {
                strikes = 0;
                continue;
            }
            if (++strikes < 4) continue;
            strikes = 0;
            lock.unlock();
            notify(theirs == libvlc_Error ? PlaybackState::Error : PlaybackState::Stopped,
                   theirs == libvlc_Error ? "stream unavailable" : "stream ended");
            lock.lock();
        }
    }

    void notify(PlaybackState state, const std::string& message) {
        state_.store(state);
        StateCallback callback;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            callback = callback_;
        }
        if (callback) callback(state, message);
    }

    VideoFrameSink* sink_ = nullptr;
    libvlc_instance_t* instance_ = nullptr;
    libvlc_media_player_t* player_ = nullptr;
    std::atomic<PlaybackState> state_{PlaybackState::Idle};
    std::mutex callbackMutex_;
    StateCallback callback_;
    std::thread watchdog_;
    std::mutex watchdogMutex_;
    std::condition_variable watchdogCv_;
    bool quit_ = false;
    int volume_ = 80;
    bool muted_ = false;
    bool paused_ = false;
    std::atomic<bool> playing_{false};
};

}  // namespace

std::unique_ptr<MediaPlayer> makeMediaPlayer(const MediaPlayerConfig& config,
                                             std::string* errorOut) {
    std::unique_ptr<VlcLegacyMediaPlayer> player(new VlcLegacyMediaPlayer());
    if (!player->start(config, errorOut)) return nullptr;
    return std::unique_ptr<MediaPlayer>(player.release());
}

}  // namespace tv
