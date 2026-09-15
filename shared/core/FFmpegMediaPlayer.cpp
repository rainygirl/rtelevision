// Playback backend built directly on FFmpeg.
//
// Used where libVLC does not exist - Haiku/arm64, whose package repository has
// neither vlc nor ffmpeg prebuilt, so ffmpeg is cross-compiled for it instead.
// Video frames are converted to BGRA for the front end's VideoFrameSink; audio
// is resampled to interleaved 16-bit and pushed into its AudioSink. When there
// is audio it is the master clock - video waits on how much the sound card has
// actually played, which keeps the two in step.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "MediaPlayer.h"

namespace tv {
namespace {

// Frames are decoded on a worker thread and pushed straight into the sink.
class FFmpegMediaPlayer : public MediaPlayer {
public:
    ~FFmpegMediaPlayer() override { stopWorker(); }

    bool attachVideoView(void*) override { return false; }
    void detachVideoView() override {}

    bool attachVideoSink(VideoFrameSink* sink) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = sink;
        return true;
    }

    bool attachAudioSink(AudioSink* sink) override {
        std::lock_guard<std::mutex> lock(mutex_);
        audioSink_ = sink;
        return true;
    }

    bool play(const Channel& channel) override {
        stopWorker();
        if (channel.url.empty()) return false;

        std::string userAgent;
        std::string referrer;
        bool relayed = false;
        for (const auto& option : channel.options) {
            if (option.first == "http-user-agent") userAgent = option.second;
            else if (option.first == "http-referrer") referrer = option.second;
            else if (option.first == "rtv-relay") relayed = true;
        }

        stop_.store(false);
        paused_.store(false);
        notify(PlaybackState::Opening, channel.name);
        worker_ = std::thread(&FFmpegMediaPlayer::run, this, channel.url, userAgent, referrer,
                              relayed);
        return true;
    }

    void stop() override {
        stopWorker();
        notify(PlaybackState::Stopped, std::string());
    }

    void setPaused(bool paused) override { paused_.store(paused); }
    bool isPaused() const override { return paused_.load(); }

    void setVolume(int percent) override {
        volume_ = percent < 0 ? 0 : (percent > 200 ? 200 : percent);
        applyGain();
    }
    int volume() const override { return volume_; }
    void setMuted(bool muted) override {
        muted_ = muted;
        applyGain();
    }
    bool isMuted() const override { return muted_; }

    PlaybackState state() const override { return state_.load(); }

    void setStateCallback(StateCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    std::string backendName() const override {
        return std::string("FFmpeg ") + av_version_info();
    }

private:
    void stopWorker() {
        stop_.store(true);
        if (worker_.joinable()) worker_.join();
    }

    void notify(PlaybackState state, const std::string& message) {
        state_.store(state);
        StateCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
        }
        if (callback) callback(state, message);
    }

    VideoFrameSink* sink() {
        std::lock_guard<std::mutex> lock(mutex_);
        return sink_;
    }

    AudioSink* audio_() {
        std::lock_guard<std::mutex> lock(mutex_);
        return audioSink_;
    }

    void applyGain() {
        if (AudioSink* target = audio_())
            target->setAudioVolume(muted_ ? 0.0f : static_cast<float>(volume_) / 100.0f);
    }

    // Lets avformat abort a blocking network read when stop() is called.
    static int interrupt(void* opaque) {
        return static_cast<FFmpegMediaPlayer*>(opaque)->stop_.load() ? 1 : 0;
    }

    // Opens one decoder for a stream index; returns nullptr when unavailable.
    AVCodecContext* openDecoder(AVFormatContext* format, int index) {
        if (index < 0) return nullptr;
        AVCodecParameters* params = format->streams[index]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(params->codec_id);
        if (codec == nullptr) return nullptr;

        AVCodecContext* decoder = avcodec_alloc_context3(codec);
        if (decoder == nullptr) return nullptr;
        avcodec_parameters_to_context(decoder, params);
        decoder->thread_count = 0;  // let FFmpeg pick
        if (avcodec_open2(decoder, codec, nullptr) < 0) {
            avcodec_free_context(&decoder);
            return nullptr;
        }
        return decoder;
    }

    void run(std::string url, std::string userAgent, std::string referrer, bool relayed) {
        AVFormatContext* format = avformat_alloc_context();
        if (format == nullptr) {
            notify(PlaybackState::Error, "out of memory");
            return;
        }
        format->interrupt_callback.callback = &FFmpegMediaPlayer::interrupt;
        format->interrupt_callback.opaque = this;

        AVDictionary* options = nullptr;
        av_dict_set(&options, "user_agent",
                    userAgent.empty() ? "RTelevision/1.0" : userAgent.c_str(), 0);
        if (!referrer.empty()) av_dict_set(&options, "referer", referrer.c_str(), 0);
        if (relayed) {
            // The local relay holds the first playlist back while it builds a
            // lead and lists only segments it already has: allow for the wait,
            // and start at the first listed segment instead of near the end.
            av_dict_set(&options, "rw_timeout", "60000000", 0);
            av_dict_set(&options, "live_start_index", "0", 0);
            // The relay answers every request with "Connection: close". Asking the
            // HLS demuxer to reuse that connection for the next segment only earns
            // a failed request and a wait; on loopback a fresh one costs nothing.
            av_dict_set(&options, "http_persistent", "0", 0);
        } else {
            av_dict_set(&options, "rw_timeout", "15000000", 0);   // 15s
        }
        av_dict_set(&options, "reconnect", "1", 0);
        av_dict_set(&options, "reconnect_streamed", "1", 0);

        int rc = avformat_open_input(&format, url.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (rc < 0) {
            avformat_free_context(format);
            notify(PlaybackState::Error, describe(rc));
            return;
        }

        notify(PlaybackState::Buffering, std::string());
        if (avformat_find_stream_info(format, nullptr) < 0) {
            avformat_close_input(&format);
            notify(PlaybackState::Error, "no stream information");
            return;
        }

        videoIndex_ = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audioIndex_ = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, videoIndex_,
                                          nullptr, 0);
        AVCodecContext* video = openDecoder(format, videoIndex_);
        if (video == nullptr) {
            avformat_close_input(&format);
            notify(PlaybackState::Error, "no playable video stream");
            return;
        }
        AVCodecContext* audio = openDecoder(format, audioIndex_);
        if (audio == nullptr) audioIndex_ = -1;

        SwrContext* resampler = audio != nullptr ? openResampler(audio) : nullptr;
        if (resampler == nullptr) audioIndex_ = -1;

        decodeLoop(format, video, audio, resampler);

        if (resampler != nullptr) swr_free(&resampler);
        if (audio != nullptr) avcodec_free_context(&audio);
        avcodec_free_context(&video);
        avformat_close_input(&format);
        if (AudioSink* target = audio_()) target->cleanupAudio();
        if (!stop_.load()) notify(PlaybackState::Stopped, "stream ended");
    }

    // Asks the sink what it can take, then builds a converter to exactly that.
    SwrContext* openResampler(AVCodecContext* decoder) {
        AudioSink* target = audio_();
        if (target == nullptr) return nullptr;

        unsigned rate = static_cast<unsigned>(decoder->sample_rate);
        unsigned channels = static_cast<unsigned>(decoder->ch_layout.nb_channels);
        if (rate == 0 || channels == 0) return nullptr;
        if (!target->setupAudio(rate, channels)) return nullptr;

        audioRate_ = rate;
        audioChannels_ = channels;

        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, static_cast<int>(channels));
        SwrContext* resampler = nullptr;
        const int rc = swr_alloc_set_opts2(&resampler, &outLayout, AV_SAMPLE_FMT_S16,
                                           static_cast<int>(rate), &decoder->ch_layout,
                                           decoder->sample_fmt, decoder->sample_rate, 0,
                                           nullptr);
        av_channel_layout_uninit(&outLayout);
        if (rc < 0 || resampler == nullptr) return nullptr;
        if (swr_init(resampler) < 0) {
            swr_free(&resampler);
            return nullptr;
        }
        applyGain();
        return resampler;
    }

    // Microseconds of content that should be on screen now. With audio this
    // follows the sound card; without it, wall time since the first frame.
    int64_t clockMicros() {
        if (audioIndex_ >= 0 && audioStart_ != AV_NOPTS_VALUE) {
            if (AudioSink* target = audio_()) return audioStart_ + target->playedMicros();
        }
        if (wallStart_ == 0 || videoStart_ == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
        return videoStart_ + (av_gettime_relative() - wallStart_);
    }

    // Video packets wait, still compressed, until the clock nears them; audio is
    // decoded the moment it is read. Streams can be muxed with video a second
    // ahead of its audio (ABC is, by 0.5-1.2 s). Handling video as it is read
    // then means waiting on a clock that only audio further on in the stream can
    // move: the thread that would read that audio blocks itself and playback
    // crawls. Compressed packets are small, so the queue can span seconds.
    static constexpr int64_t kDecodeAheadMicros = 500000;     // covers decoder reordering
    static constexpr int64_t kMaxQueuedVideoMicros = 8000000; // then decode regardless
    static constexpr size_t kMaxDecodedFrames = 12;

    void decodeLoop(AVFormatContext* format, AVCodecContext* video, AVCodecContext* audio,
                    SwrContext* resampler) {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        SwsContext* scaler = nullptr;
        unsigned sinkWidth = 0, sinkHeight = 0, sinkPitch = 0;
        bool started = false;
        bool failed = false;
        std::vector<uint8_t> pcm;
        std::deque<AVPacket*> videoPackets;
        std::deque<AVFrame*> frames;

        videoStart_ = AV_NOPTS_VALUE;
        audioStart_ = AV_NOPTS_VALUE;
        audioQueuedEnd_ = AV_NOPTS_VALUE;
        wallStart_ = 0;

        auto packetMicros = [&](const AVPacket* p) -> int64_t {
            const int64_t ts = p->dts != AV_NOPTS_VALUE ? p->dts : p->pts;
            if (ts == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
            return av_rescale_q(ts, format->streams[videoIndex_]->time_base, AVRational{1, 1000000});
        };

        auto queuedSpan = [&]() -> int64_t {
            if (videoPackets.size() < 2) return 0;
            const int64_t first = packetMicros(videoPackets.front());
            const int64_t last = packetMicros(videoPackets.back());
            return (first == AV_NOPTS_VALUE || last == AV_NOPTS_VALUE) ? 0 : last - first;
        };

        // Decodes queued packets that are near the clock, or any once the queue
        // has grown too long, keeping only a few frames decoded ahead.
        auto pumpVideo = [&](bool all) {
            while (!videoPackets.empty() && frames.size() < kMaxDecodedFrames && !stop_.load()) {
                AVPacket* next = videoPackets.front();
                if (!all) {
                    const int64_t now = clockMicros();
                    const int64_t at = packetMicros(next);
                    const bool near = now == AV_NOPTS_VALUE || at == AV_NOPTS_VALUE ||
                                      at <= now + kDecodeAheadMicros;
                    if (!near && queuedSpan() < kMaxQueuedVideoMicros) break;
                }
                videoPackets.pop_front();
                const int rc = avcodec_send_packet(video, next);
                av_packet_free(&next);
                if (rc < 0) continue;
                while (avcodec_receive_frame(video, frame) == 0) {
                    const int64_t micros = frameMicros(format, videoIndex_, frame);
                    if (videoStart_ == AV_NOPTS_VALUE && micros != AV_NOPTS_VALUE)
                        videoStart_ = micros;
                    if (wallStart_ == 0) wallStart_ = av_gettime_relative();
                    if (AVFrame* copy = av_frame_clone(frame)) frames.push_back(copy);
                    av_frame_unref(frame);
                }
            }
        };

        // Shows the decoded frames that are due. With `wait`, holds on to the
        // oldest until it is due first - used only when nothing else can move.
        auto showFrames = [&](bool wait) {
            while (!frames.empty() && !stop_.load()) {
                AVFrame* next = frames.front();
                const int64_t micros = frameMicros(format, videoIndex_, next);
                const int64_t now = clockMicros();
                if (micros != AV_NOPTS_VALUE && now != AV_NOPTS_VALUE && micros > now) {
                    if (!wait) break;
                    waitForClock(micros);
                    wait = false;
                }
                frames.pop_front();
                const bool ok = renderVideo(next, scaler, sinkWidth, sinkHeight, sinkPitch);
                av_frame_free(&next);
                if (!ok) return false;
                if (!started) {
                    started = true;
                    notify(PlaybackState::Playing, std::string());
                }
            }
            return true;
        };

        while (!stop_.load() && !failed) {
            if (paused_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                wallStart_ = 0;  // resync after a pause
                continue;
            }

            if (av_read_frame(format, packet) < 0) {
                // End of stream: play out what is still queued.
                while (!stop_.load() && !failed && (!videoPackets.empty() || !frames.empty())) {
                    pumpVideo(true);
                    if (!showFrames(true)) failed = true;
                }
                break;
            }

            if (packet->stream_index == videoIndex_) {
                if (AVPacket* copy = av_packet_clone(packet)) videoPackets.push_back(copy);
                av_packet_unref(packet);
            } else if (packet->stream_index == audioIndex_ && audio != nullptr) {
                const int rc = avcodec_send_packet(audio, packet);
                av_packet_unref(packet);
                while (rc >= 0 && !stop_.load() && avcodec_receive_frame(audio, frame) == 0) {
                    const int64_t micros = frameMicros(format, audioIndex_, frame);
                    if (audioStart_ == AV_NOPTS_VALUE && micros != AV_NOPTS_VALUE) {
                        audioStart_ = micros;
                        audioQueuedEnd_ = micros;
                    }
                    alignAudio(micros, pcm);
                    pushAudio(resampler, frame, pcm);
                    av_frame_unref(frame);
                }
            } else {
                av_packet_unref(packet);
            }

            pumpVideo(false);
            if (!showFrames(false)) failed = true;

            // Without audio nothing else paces the reading; a long video queue
            // with a full set of decoded frames waits for the oldest to be due.
            if (!failed && frames.size() >= kMaxDecodedFrames && queuedSpan() >= kMaxQueuedVideoMicros &&
                !showFrames(true))
                failed = true;
        }

        for (AVPacket* queued : videoPackets) av_packet_free(&queued);
        for (AVFrame* queued : frames) av_frame_free(&queued);
        if (scaler != nullptr) sws_freeContext(scaler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        if (VideoFrameSink* target = sink()) target->cleanupFormat();
    }

    static int64_t frameMicros(AVFormatContext* format, int index, AVFrame* frame) {
        const int64_t pts = frame->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
        return av_rescale_q(pts, format->streams[index]->time_base, AVRational{1, 1000000});
    }

    // The clock counts audio the sound card has actually played, so audio that
    // never gets there - frames the decoder rejected, a hole in the stream -
    // leaves it behind the stream for good, and every video frame then waits
    // on it: a damaged AC-3 track played at a quarter of real time. Short holes
    // are filled with silence; a timestamp jump is followed rather than waited out.
    void alignAudio(int64_t micros, std::vector<uint8_t>& pcm) {
        if (micros == AV_NOPTS_VALUE || audioQueuedEnd_ == AV_NOPTS_VALUE || audioRate_ == 0 ||
            audioChannels_ == 0)
            return;
        const int64_t gap = micros - audioQueuedEnd_;
        if (gap > 2000000 || gap < -2000000) {
            audioStart_ += gap;
            audioQueuedEnd_ = micros;
            return;
        }
        if (gap < 20000) return;
        AudioSink* target = audio_();
        if (target == nullptr) return;
        const int64_t samples = gap * static_cast<int64_t>(audioRate_) / 1000000;
        const size_t bytes = static_cast<size_t>(samples) * audioChannels_ * 2;
        if (bytes == 0) return;
        if (pcm.size() < bytes) pcm.resize(bytes);
        std::memset(pcm.data(), 0, bytes);
        target->queueAudio(pcm.data(), bytes);
        audioQueuedEnd_ += samples * 1000000 / static_cast<int64_t>(audioRate_);
    }

    void pushAudio(SwrContext* resampler, AVFrame* frame, std::vector<uint8_t>& pcm) {
        AudioSink* target = audio_();
        if (target == nullptr || resampler == nullptr) return;

        const int maxSamples = swr_get_out_samples(resampler, frame->nb_samples);
        if (maxSamples <= 0) return;
        const size_t needed = static_cast<size_t>(maxSamples) * audioChannels_ * 2;
        if (pcm.size() < needed) pcm.resize(needed);

        uint8_t* planes[1] = {pcm.data()};
        const int written = swr_convert(resampler, planes, maxSamples,
                                        const_cast<const uint8_t**>(frame->data),
                                        frame->nb_samples);
        if (written <= 0) return;
        target->queueAudio(pcm.data(), static_cast<size_t>(written) * audioChannels_ * 2);
        if (audioQueuedEnd_ != AV_NOPTS_VALUE && audioRate_ > 0)
            audioQueuedEnd_ += static_cast<int64_t>(written) * 1000000 / static_cast<int64_t>(audioRate_);
    }

    bool renderVideo(AVFrame* frame, SwsContext*& scaler, unsigned& width, unsigned& height,
                     unsigned& pitch) {
        VideoFrameSink* target = sink();
        if (target == nullptr) return true;

        if (scaler == nullptr || width == 0) {
            unsigned w = static_cast<unsigned>(frame->width);
            unsigned h = static_cast<unsigned>(frame->height);
            pitch = target->setupFormat(w, h);
            if (pitch == 0) return false;
            width = w;
            height = h;
            scaler = sws_getContext(frame->width, frame->height,
                                    static_cast<AVPixelFormat>(frame->format), (int)width,
                                    (int)height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr,
                                    nullptr, nullptr);
            if (scaler == nullptr) return false;
        }

        void* pixels = target->lockFrame();
        if (pixels != nullptr) {
            uint8_t* planes[4] = {static_cast<uint8_t*>(pixels), nullptr, nullptr, nullptr};
            int strides[4] = {(int)pitch, 0, 0, 0};
            sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides);
        }
        target->unlockFrame();
        target->displayFrame();
        return true;
    }

    // Holds a frame back until the master clock reaches its timestamp.
    void waitForClock(int64_t micros) {
        if (micros == AV_NOPTS_VALUE) return;
        for (int guard = 0; guard < 250 && !stop_.load(); ++guard) {
            const int64_t now = clockMicros();
            if (now == AV_NOPTS_VALUE) return;
            int64_t delay = micros - now;
            if (delay <= 0) return;
            if (delay > 1000000) delay = 1000000;   // a bad timestamp must not stall us
            av_usleep(static_cast<unsigned>(delay > 20000 ? 20000 : delay));
        }
    }

    static std::string describe(int rc) {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(rc, buffer, sizeof(buffer));
        return buffer[0] != '\0' ? std::string(buffer) : std::string("could not open the stream");
    }

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<PlaybackState> state_{PlaybackState::Idle};
    std::mutex mutex_;
    StateCallback callback_;
    VideoFrameSink* sink_ = nullptr;
    AudioSink* audioSink_ = nullptr;
    int videoIndex_ = -1;
    int audioIndex_ = -1;
    unsigned audioRate_ = 0;
    unsigned audioChannels_ = 0;
    int64_t videoStart_ = AV_NOPTS_VALUE;
    int64_t audioStart_ = AV_NOPTS_VALUE;
    int64_t audioQueuedEnd_ = AV_NOPTS_VALUE;  // stream time at the end of the audio queued so far
    int64_t wallStart_ = 0;
    int volume_ = 80;
    bool muted_ = false;
};

}  // namespace

std::unique_ptr<MediaPlayer> makeMediaPlayer(const MediaPlayerConfig&, std::string* errorOut) {
    if (errorOut) errorOut->clear();
    avformat_network_init();
    return std::unique_ptr<MediaPlayer>(new FFmpegMediaPlayer());
}

}  // namespace tv
