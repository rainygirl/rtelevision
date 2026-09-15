// Backend-neutral playback interface.
//
// macOS and Haiku both get libVLC today (VlcMediaPlayer). If a future Haiku
// build has to fall back to media_kit, only a sibling implementation of this
// interface is needed - no UI or core change.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Channel.h"

namespace tv {

enum class PlaybackState { Idle, Opening, Buffering, Playing, Paused, Stopped, Error };

// Audio output. The backend decodes to interleaved 16-bit samples and hands
// them over; the front end owns the device. Used by the FFmpeg backend, which
// has no audio output of its own.
class AudioSink {
public:
    virtual ~AudioSink() = default;

    // Negotiates the format. rate/channels start at the stream's values and may
    // be changed by the sink. Returns false when audio cannot be played.
    virtual bool setupAudio(unsigned& sampleRate, unsigned& channels) = 0;

    // Queues interleaved signed 16-bit samples. Blocks while the queue is full,
    // which is what paces the decoder.
    virtual void queueAudio(const void* samples, size_t bytes) = 0;

    // Microseconds of audio the device has actually played. The backend uses
    // this as the master clock so video follows the sound card, not wall time.
    virtual int64_t playedMicros() const = 0;

    virtual void setAudioVolume(float gain) {}
    virtual void flushAudio() {}
    virtual void cleanupAudio() {}
};

// Software video output. Used by platforms whose native view handle libVLC
// cannot take directly (Haiku's BView); the backend decodes into a buffer the
// front end owns and blits itself.
class VideoFrameSink {
public:
    virtual ~VideoFrameSink() = default;

    // Negotiates the buffer. width/height start at the stream's size and may be
    // narrowed by the sink. Returns the row stride in bytes, 0 to refuse.
    virtual unsigned setupFormat(unsigned& width, unsigned& height) = 0;
    virtual void* lockFrame() = 0;   // returns the start of the BGRA buffer
    virtual void unlockFrame() = 0;
    virtual void displayFrame() = 0; // a complete frame is in the buffer
    virtual void cleanupFormat() {}
};

const char* playbackStateName(PlaybackState state);

struct MediaPlayerConfig {
    // Directory holding the VLC plugin tree. Empty means "let libVLC decide".
    std::string pluginPath;
    std::vector<std::string> extraArgs;
    bool verbose = false;
};

class MediaPlayer {
public:
    // State callbacks arrive on a backend thread. Front ends must hop to their
    // own UI thread before touching widgets.
    using StateCallback = std::function<void(PlaybackState state, const std::string& message)>;

    virtual ~MediaPlayer() = default;

    // nativeView is an NSView* on macOS and an HWND on Windows. Returns false
    // on platforms where libVLC cannot render into a native handle.
    virtual bool attachVideoView(void* nativeView) = 0;
    virtual void detachVideoView() = 0;

    // Alternative output path for those platforms. The sink must outlive the
    // player or be detached with attachVideoSink(nullptr) first.
    virtual bool attachVideoSink(VideoFrameSink* sink) { (void)sink; return false; }

    // Only the backends without their own audio output implement this.
    virtual bool attachAudioSink(AudioSink* sink) { (void)sink; return false; }

    virtual bool play(const Channel& channel) = 0;
    virtual void stop() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual bool isPaused() const = 0;

    virtual void setVolume(int percent) = 0;
    virtual int volume() const = 0;
    virtual void setMuted(bool muted) = 0;
    virtual bool isMuted() const = 0;

    virtual PlaybackState state() const = 0;
    virtual void setStateCallback(StateCallback callback) = 0;
    virtual std::string backendName() const = 0;
};

// Returns nullptr when no backend could be started (message filled in).
std::unique_ptr<MediaPlayer> makeMediaPlayer(const MediaPlayerConfig& config,
                                             std::string* errorOut = nullptr);

}  // namespace tv
