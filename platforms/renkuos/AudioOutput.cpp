#include "AudioOutput.h"

#include <cstring>

namespace tv {
namespace haiku {
namespace {

// About a second of stereo 48 kHz; enough to ride out decode hiccups without
// adding noticeable latency when switching channels.
const size_t kRingSeconds = 1;

}  // namespace

AudioOutput::AudioOutput()
    : fPlayer(NULL),
      fHead(0),
      fFill(0),
      fFramesPlayed(0),
      fRate(0),
      fChannels(0),
      fStopping(false) {}

AudioOutput::~AudioOutput() {
    cleanupAudio();
}

bool AudioOutput::setupAudio(unsigned& sampleRate, unsigned& channels) {
    cleanupAudio();
    if (sampleRate == 0 || channels == 0) return false;
    if (channels > 2) channels = 2;  // the mixer is happiest with stereo

    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = static_cast<float>(sampleRate);
    format.channel_count = channels;
    format.format = media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order = B_MEDIA_LITTLE_ENDIAN;
    format.buffer_size = 4096;

    BSoundPlayer* player = new BSoundPlayer(&format, "R Television", &AudioOutput::Feed,
                                            NULL, this);
    if (player->InitCheck() != B_OK) {
        delete player;
        return false;
    }

    // The media server may have picked something else; follow whatever it took.
    const media_raw_audio_format& actual = player->Format();
    if (actual.frame_rate > 0) sampleRate = static_cast<unsigned>(actual.frame_rate);
    if (actual.channel_count > 0) channels = actual.channel_count;

    {
        std::lock_guard<std::mutex> lock(fLock);
        fRate = sampleRate;
        fChannels = channels;
        fRing.assign(kRingSeconds * fRate * fChannels * sizeof(int16_t), 0);
        fHead = 0;
        fFill = 0;
        fFramesPlayed = 0;
        fStopping = false;
        fPlayer = player;
    }

    player->SetHasData(true);
    player->Start();
    return true;
}

void AudioOutput::queueAudio(const void* samples, size_t bytes) {
    const uint8_t* in = static_cast<const uint8_t*>(samples);
    std::unique_lock<std::mutex> lock(fLock);
    if (fPlayer == NULL || fRing.empty()) return;

    while (bytes > 0 && !fStopping) {
        if (fFill == fRing.size()) {
            // Full: wait for the callback to drain some. The timeout keeps a
            // stopped device from wedging the decoder thread.
            if (fSpace.wait_for(lock, std::chrono::milliseconds(200)) ==
                    std::cv_status::timeout &&
                fFill == fRing.size()) {
                return;
            }
            continue;
        }

        const size_t tail = (fHead + fFill) % fRing.size();
        const size_t room = fRing.size() - fFill;
        size_t chunk = bytes < room ? bytes : room;
        if (tail + chunk > fRing.size()) chunk = fRing.size() - tail;

        std::memcpy(&fRing[tail], in, chunk);
        in += chunk;
        bytes -= chunk;
        fFill += chunk;
    }
}

int64_t AudioOutput::playedMicros() const {
    std::lock_guard<std::mutex> lock(fLock);
    if (fRate == 0) return 0;
    return static_cast<int64_t>(fFramesPlayed * 1000000ULL / fRate);
}

void AudioOutput::setAudioVolume(float gain) {
    std::lock_guard<std::mutex> lock(fLock);
    if (fPlayer != NULL) fPlayer->SetVolume(gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain));
}

void AudioOutput::flushAudio() {
    std::lock_guard<std::mutex> lock(fLock);
    fHead = 0;
    fFill = 0;
    fSpace.notify_all();
}

void AudioOutput::cleanupAudio() {
    BSoundPlayer* player = NULL;
    {
        std::lock_guard<std::mutex> lock(fLock);
        player = fPlayer;
        fPlayer = NULL;
        fStopping = true;
        fFill = 0;
        fHead = 0;
    }
    fSpace.notify_all();
    if (player != NULL) {
        player->Stop(true, true);
        delete player;
    }
}

void AudioOutput::Feed(void* cookie, void* buffer, size_t size,
                       const media_raw_audio_format&) {
    static_cast<AudioOutput*>(cookie)->Fill(static_cast<uint8_t*>(buffer), size);
}

void AudioOutput::Fill(uint8_t* out, size_t bytes) {
    std::unique_lock<std::mutex> lock(fLock);
    size_t written = 0;

    while (written < bytes && fFill > 0) {
        size_t chunk = bytes - written;
        if (chunk > fFill) chunk = fFill;
        if (fHead + chunk > fRing.size()) chunk = fRing.size() - fHead;

        std::memcpy(out + written, &fRing[fHead], chunk);
        fHead = (fHead + chunk) % fRing.size();
        fFill -= chunk;
        written += chunk;
    }

    // Underrun: silence rather than a click, and do not advance the clock for
    // samples that were never there.
    if (written < bytes) std::memset(out + written, 0, bytes - written);

    if (fChannels > 0) fFramesPlayed += written / (fChannels * sizeof(int16_t));
    lock.unlock();
    fSpace.notify_all();
}

}  // namespace haiku
}  // namespace tv
