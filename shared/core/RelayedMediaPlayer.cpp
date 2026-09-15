// MediaPlayer decorator that sends eligible HLS channels through HlsRelay.
//
// Deciding needs the channel's playlist, which is a network round trip, so
// play() returns at once and the decision is made on a probe thread that then
// starts the real backend on either the relay's address or the channel's own.
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

#include "HlsRelay.h"
#include "MediaPlayer.h"

namespace tv {
namespace {

class RelayedMediaPlayer : public MediaPlayer {
public:
    RelayedMediaPlayer(std::unique_ptr<MediaPlayer> inner, std::shared_ptr<HttpClient> http,
                       RelaySettings settings)
        : inner_(std::move(inner)), relay_(new HlsRelay(std::move(http), settings)) {
        relay_->setProgressCallback([this](int percent) {
            // While the backend waits on the relay's first playlist it sits in
            // Opening or, once it has said "Buffering 0%", in Buffering; the
            // relay stops reporting as soon as it hands that playlist over.
            const PlaybackState state = inner_->state();
            if (state == PlaybackState::Opening || state == PlaybackState::Buffering)
                notify(PlaybackState::Buffering, std::to_string(percent) + "%");
        });
    }

    ~RelayedMediaPlayer() override {
        cancelProbe();
        inner_->stop();
        relay_.reset();
    }

    bool attachVideoView(void* nativeView) override { return inner_->attachVideoView(nativeView); }
    void detachVideoView() override { inner_->detachVideoView(); }
    bool attachVideoSink(VideoFrameSink* sink) override { return inner_->attachVideoSink(sink); }
    bool attachAudioSink(AudioSink* sink) override { return inner_->attachAudioSink(sink); }

    bool play(const Channel& channel) override {
        cancelProbe();
        relay_->close();
        if (!HlsRelay::looksLikeHls(channel.url)) return inner_->play(channel);

        // Stop the old stream now, the way a direct play() would. Left running it
        // keeps asking the closed relay session and reports errors meanwhile.
        inner_->stop();
        const uint64_t generation = ++generation_;
        notify(PlaybackState::Opening, channel.name);
        probe_ = std::thread([this, channel, generation]() {
            auto cancelled = [this, generation]() { return generation_.load() != generation; };
            bool relayed = false;
            const std::string url = relay_->open(channel, cancelled, &relayed);
            if (cancelled()) return;  // whoever cancelled also closes the relay
            Channel target = channel;
            if (relayed) {
                target.url = url;
                // Tells the backend it is reading from the relay (see its play()).
                target.options.emplace_back("rtv-relay", "1");
            }
            inner_->play(target);
        });
        return true;
    }

    void stop() override {
        cancelProbe();
        relay_->close();
        inner_->stop();
    }

    void setPaused(bool paused) override { inner_->setPaused(paused); }
    bool isPaused() const override { return inner_->isPaused(); }
    void setVolume(int percent) override { inner_->setVolume(percent); }
    int volume() const override { return inner_->volume(); }
    void setMuted(bool muted) override { inner_->setMuted(muted); }
    bool isMuted() const override { return inner_->isMuted(); }
    PlaybackState state() const override { return inner_->state(); }

    void setStateCallback(StateCallback callback) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback_ = callback;
        }
        inner_->setStateCallback(std::move(callback));
    }

    std::string backendName() const override { return inner_->backendName() + " + HLS relay"; }

private:
    // Joins the probe thread. Its network calls poll the generation, so a stale
    // probe returns within about a second.
    void cancelProbe() {
        ++generation_;
        if (probe_.joinable()) probe_.join();
    }

    void notify(PlaybackState state, const std::string& message) {
        StateCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
        }
        if (callback) callback(state, message);
    }

    std::unique_ptr<MediaPlayer> inner_;
    std::unique_ptr<HlsRelay> relay_;
    std::thread probe_;
    std::atomic<uint64_t> generation_{0};
    std::mutex mutex_;
    StateCallback callback_;
};

}  // namespace

std::unique_ptr<MediaPlayer> makeRelayedMediaPlayer(std::unique_ptr<MediaPlayer> inner,
                                                    std::shared_ptr<HttpClient> http,
                                                    RelaySettings settings) {
    if (!inner) return inner;
    return std::unique_ptr<MediaPlayer>(
        new RelayedMediaPlayer(std::move(inner), std::move(http), settings));
}

}  // namespace tv
