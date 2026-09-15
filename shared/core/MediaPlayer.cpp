#include "MediaPlayer.h"

namespace tv {

const char* playbackStateName(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle: return "Idle";
        case PlaybackState::Opening: return "Opening";
        case PlaybackState::Buffering: return "Buffering";
        case PlaybackState::Playing: return "Playing";
        case PlaybackState::Paused: return "Paused";
        case PlaybackState::Stopped: return "Stopped";
        case PlaybackState::Error: return "Error";
    }
    return "Unknown";
}

}  // namespace tv
