// Playback backend for targets where no media library is available yet.
//
// Built instead of VlcMediaPlayer.cpp when a platform has no libVLC package -
// Haiku/arm64 today. Everything except playback (channel list, search,
// favorites, offline cache) works exactly the same; play() reports why.
#include <atomic>
#include <mutex>
#include <utility>

#include "MediaPlayer.h"

namespace tv {
namespace {

class NullMediaPlayer : public MediaPlayer {
public:
    bool attachVideoView(void*) override { return false; }
    void detachVideoView() override {}
    bool attachVideoSink(VideoFrameSink*) override { return false; }

    bool play(const Channel& channel) override {
        notify(PlaybackState::Error, "no media backend on this build (libVLC unavailable)");
        return false;
    }

    void stop() override { notify(PlaybackState::Stopped, std::string()); }
    void setPaused(bool paused) override { paused_ = paused; }
    bool isPaused() const override { return paused_; }

    void setVolume(int percent) override { volume_ = percent; }
    int volume() const override { return volume_; }
    void setMuted(bool muted) override { muted_ = muted; }
    bool isMuted() const override { return muted_; }

    PlaybackState state() const override { return state_.load(); }

    void setStateCallback(StateCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    std::string backendName() const override { return "none"; }

private:
    void notify(PlaybackState state, const std::string& message) {
        state_.store(state);
        StateCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
        }
        if (callback) callback(state, message);
    }

    std::atomic<PlaybackState> state_{PlaybackState::Idle};
    std::mutex mutex_;
    StateCallback callback_;
    int volume_ = 80;
    bool muted_ = false;
    bool paused_ = false;
};

}  // namespace

std::unique_ptr<MediaPlayer> makeMediaPlayer(const MediaPlayerConfig&, std::string* errorOut) {
    if (errorOut) errorOut->clear();
    return std::unique_ptr<MediaPlayer>(new NullMediaPlayer());
}

}  // namespace tv
