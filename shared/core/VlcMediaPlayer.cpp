#include <vlc/vlc.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "MediaPlayer.h"
#include "Paths.h"

namespace tv {

namespace {

class VlcMediaPlayer : public MediaPlayer {
public:
    ~VlcMediaPlayer() override {
        {
            std::lock_guard<std::mutex> lock(watchdogMutex_);
            quit_ = true;
        }
        watchdogCv_.notify_all();
        if (watchdog_.joinable()) watchdog_.join();
        if (player_) {
            libvlc_media_player_stop(player_);
            libvlc_media_player_release(player_);
        }
        if (instance_) libvlc_release(instance_);
    }

    bool start(const MediaPlayerConfig& config, std::string* errorOut) {
        if (!config.pluginPath.empty()) {
            // libVLC 3 resolves its module tree through this variable; it must be
            // set before libvlc_new(). Lets the app bundle ship its own plugins.
            setenv("VLC_PLUGIN_PATH", config.pluginPath.c_str(), 1);
        }

        std::vector<std::string> args = {
            "--no-video-title-show",
            "--network-caching=1500",
            "--no-snapshot-preview",
            "--no-osd",
            "--no-stats",
            "--intf=dummy",
            config.verbose ? "--verbose=2" : "--quiet",
        };
        args.insert(args.end(), config.extraArgs.begin(), config.extraArgs.end());

        std::vector<const char*> argv;
        argv.reserve(args.size());
        for (const std::string& a : args) argv.push_back(a.c_str());

        instance_ = libvlc_new(static_cast<int>(argv.size()), argv.data());
        if (!instance_) {
            if (errorOut) *errorOut = "libvlc_new failed (plugin path: " + config.pluginPath + ")";
            return false;
        }

        player_ = libvlc_media_player_new(instance_);
        if (!player_) {
            if (errorOut) *errorOut = "libvlc_media_player_new failed";
            return false;
        }

        libvlc_event_manager_t* em = libvlc_media_player_event_manager(player_);
        const libvlc_event_e events[] = {
            libvlc_MediaPlayerOpening,   libvlc_MediaPlayerBuffering,
            libvlc_MediaPlayerPlaying,   libvlc_MediaPlayerPaused,
            libvlc_MediaPlayerStopped,   libvlc_MediaPlayerEncounteredError,
            libvlc_MediaPlayerEndReached,
        };
        for (libvlc_event_e e : events) libvlc_event_attach(em, e, &VlcMediaPlayer::onEvent, this);

        libvlc_audio_set_volume(player_, volume_);
        watchdog_ = std::thread([this] { watch(); });
        return true;
    }

    bool attachVideoView(void* nativeView) override {
        if (!player_ || !nativeView) return false;
#if defined(__APPLE__)
        libvlc_media_player_set_nsobject(player_, nativeView);
        return true;
#elif defined(_WIN32)
        libvlc_media_player_set_hwnd(player_, nativeView);
        return true;
#elif defined(__HAIKU__)
        // libVLC 3 exposes no BView handle setter; the Haiku front end supplies a
        // backend that renders through media_kit or a vmem callback instead.
        (void)nativeView;
        return false;
#else
        libvlc_media_player_set_xwindow(player_, static_cast<uint32_t>(
                                                     reinterpret_cast<uintptr_t>(nativeView)));
        return true;
#endif
    }

    bool attachVideoSink(VideoFrameSink* sink) override {
        if (!player_) return false;
        sink_ = sink;
        if (!sink) {
            libvlc_video_set_callbacks(player_, nullptr, nullptr, nullptr, nullptr);
            return true;
        }
        libvlc_video_set_format_callbacks(player_, &VlcMediaPlayer::formatCb,
                                          &VlcMediaPlayer::cleanupCb);
        libvlc_video_set_callbacks(player_, &VlcMediaPlayer::lockCb, &VlcMediaPlayer::unlockCb,
                                   &VlcMediaPlayer::displayCb, this);
        return true;
    }

    void detachVideoView() override {
        if (!player_) return;
#if defined(__APPLE__)
        libvlc_media_player_set_nsobject(player_, nullptr);
#elif defined(_WIN32)
        libvlc_media_player_set_hwnd(player_, nullptr);
#elif !defined(__HAIKU__)
        libvlc_media_player_set_xwindow(player_, 0);
#endif
    }

    bool play(const Channel& channel) override {
        if (!player_ || channel.url.empty()) return false;
        libvlc_media_t* media = libvlc_media_new_location(instance_, channel.url.c_str());
        if (!media) {
            notify(PlaybackState::Error, "could not open " + channel.url);
            return false;
        }
        for (const auto& kv : channel.options) {
            if (kv.first == "rtv-relay") {
                // The local relay lists only segments it already holds, the lead
                // it built up first. VLC normally starts a live playlist near its
                // end, which would skip straight past that lead.
                libvlc_media_add_option(media, ":adaptive-livedelay=120000");
                continue;
            }
            std::string opt = ":" + kv.first + "=" + kv.second;
            libvlc_media_add_option(media, opt.c_str());
        }
        libvlc_media_player_set_media(player_, media);
        libvlc_media_release(media);

        paused_ = false;
        playing_ = false;
        notify(PlaybackState::Opening, channel.name);
        return libvlc_media_player_play(player_) == 0;
    }

    void stop() override {
        if (!player_) return;
        libvlc_media_player_stop(player_);
        paused_ = false;
        playing_ = false;
        notify(PlaybackState::Stopped, std::string());
    }

    void setPaused(bool paused) override {
        if (!player_) return;
        if (!libvlc_media_player_can_pause(player_) && paused) return;
        libvlc_media_player_set_pause(player_, paused ? 1 : 0);
        paused_ = paused;
    }

    bool isPaused() const override { return paused_; }

    void setVolume(int percent) override {
        volume_ = percent < 0 ? 0 : (percent > 200 ? 200 : percent);
        if (player_) libvlc_audio_set_volume(player_, volume_);
    }

    int volume() const override { return volume_; }

    void setMuted(bool muted) override {
        muted_ = muted;
        if (player_) libvlc_audio_set_mute(player_, muted ? 1 : 0);
    }

    bool isMuted() const override { return muted_; }

    PlaybackState state() const override { return state_.load(); }

    void setStateCallback(StateCallback callback) override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback_ = std::move(callback);
    }

    std::string backendName() const override {
        return std::string("libVLC ") + libvlc_get_version();
    }

private:
    static unsigned formatCb(void** opaque, char* chroma, unsigned* width, unsigned* height,
                             unsigned* pitches, unsigned* lines) {
        auto* self = static_cast<VlcMediaPlayer*>(*opaque);
        if (!self->sink_) return 0;
        unsigned w = *width, h = *height;
        unsigned pitch = self->sink_->setupFormat(w, h);
        if (pitch == 0) return 0;
        std::memcpy(chroma, "RV32", 4);  // little-endian BGRA, what BBitmap wants
        *width = w;
        *height = h;
        pitches[0] = pitch;
        lines[0] = h;
        return 1;
    }

    static void cleanupCb(void* opaque) {
        auto* self = static_cast<VlcMediaPlayer*>(opaque);
        if (self->sink_) self->sink_->cleanupFormat();
    }

    static void* lockCb(void* opaque, void** planes) {
        auto* self = static_cast<VlcMediaPlayer*>(opaque);
        planes[0] = self->sink_ ? self->sink_->lockFrame() : nullptr;
        return nullptr;
    }

    static void unlockCb(void* opaque, void* picture, void* const* planes) {
        (void)picture;
        (void)planes;
        auto* self = static_cast<VlcMediaPlayer*>(opaque);
        if (self->sink_) self->sink_->unlockFrame();
    }

    static void displayCb(void* opaque, void* picture) {
        (void)picture;
        auto* self = static_cast<VlcMediaPlayer*>(opaque);
        if (self->sink_) self->sink_->displayFrame();
    }

    static void onEvent(const libvlc_event_t* event, void* opaque) {
        auto* self = static_cast<VlcMediaPlayer*>(opaque);
        switch (event->type) {
            case libvlc_MediaPlayerOpening: self->notify(PlaybackState::Opening, ""); break;
            case libvlc_MediaPlayerBuffering: {
                // While a stream plays, libVLC keeps reporting its cache level and
                // sends no Playing event after a refill; a full cache then means
                // playing, not "still buffering".
                const int cache = static_cast<int>(event->u.media_player_buffering.new_cache);
                if (self->playing_ && cache >= 100)
                    self->notify(PlaybackState::Playing, "");
                else
                    self->notify(PlaybackState::Buffering, std::to_string(cache) + "%");
                break;
            }
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

    // libVLC's input thread can end without a Stopped, EndReached or Error
    // event (seen on Haiku after an HTTP redirect). The player then reports
    // Ended or Stopped while this object still says Opening or Buffering, and
    // the front end would wait forever. A few consecutive polls in that
    // condition turn it into an error report.
    void watch() {
        int strikes = 0;
        std::unique_lock<std::mutex> lock(watchdogMutex_);
        while (!watchdogCv_.wait_for(lock, std::chrono::milliseconds(500), [this] { return quit_; })) {
            const PlaybackState ours = state_.load();
            if (ours != PlaybackState::Opening && ours != PlaybackState::Buffering) {
                strikes = 0;
                continue;
            }
            const libvlc_state_t theirs = libvlc_media_player_get_state(player_);
            const bool dead = theirs == libvlc_Ended || theirs == libvlc_Error;
            if (!dead) {
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

    libvlc_instance_t* instance_ = nullptr;
    libvlc_media_player_t* player_ = nullptr;
    std::atomic<PlaybackState> state_{PlaybackState::Idle};
    std::mutex callbackMutex_;
    StateCallback callback_;
    VideoFrameSink* sink_ = nullptr;
    std::thread watchdog_;
    std::mutex watchdogMutex_;
    std::condition_variable watchdogCv_;
    bool quit_ = false;
    int volume_ = 80;
    bool muted_ = false;
    bool paused_ = false;
    std::atomic<bool> playing_{false};  // a Playing event arrived for the current stream
};

}  // namespace

std::unique_ptr<MediaPlayer> makeMediaPlayer(const MediaPlayerConfig& config,
                                             std::string* errorOut) {
    std::unique_ptr<VlcMediaPlayer> player(new VlcMediaPlayer());
    if (!player->start(config, errorOut)) return nullptr;
    return std::unique_ptr<MediaPlayer>(player.release());
}

}  // namespace tv
