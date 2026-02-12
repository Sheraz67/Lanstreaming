#pragma once

#include "core/types.h"
#include <cstdint>

struct SDL_AudioStream;

namespace lancast {

class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    bool init(uint32_t sample_rate, uint16_t channels);
    void play_frame(const RawAudioFrame& frame);
    void shutdown();

private:
    SDL_AudioStream* stream_ = nullptr;
    uint32_t device_id_ = 0;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    bool initialized_ = false;
};

} // namespace lancast
