// Haiku audio output for the FFmpeg backend.
//
// BSoundPlayer pulls samples through a callback, so the decoder writes into a
// ring buffer and the callback drains it. queueAudio() blocks while the buffer
// is full, which is what paces the decoding thread.
#pragma once

#include <SoundPlayer.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include "core/MediaPlayer.h"

namespace tv {
namespace haiku {

class AudioOutput : public AudioSink {
public:
    AudioOutput();
    ~AudioOutput() override;

    bool setupAudio(unsigned& sampleRate, unsigned& channels) override;
    void queueAudio(const void* samples, size_t bytes) override;
    int64_t playedMicros() const override;
    void setAudioVolume(float gain) override;
    void flushAudio() override;
    void cleanupAudio() override;

private:
    static void Feed(void* cookie, void* buffer, size_t size,
                     const media_raw_audio_format& format);
    void Fill(uint8_t* out, size_t bytes);

    BSoundPlayer* fPlayer;
    mutable std::mutex fLock;
    std::condition_variable fSpace;
    std::vector<uint8_t> fRing;
    size_t fHead;      // read position
    size_t fFill;      // bytes currently queued
    uint64_t fFramesPlayed;
    unsigned fRate;
    unsigned fChannels;
    bool fStopping;
};

}  // namespace haiku
}  // namespace tv
