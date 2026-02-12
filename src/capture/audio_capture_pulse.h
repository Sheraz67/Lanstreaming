#pragma once

#include "core/types.h"
#include <cstdint>
#include <optional>

struct pa_simple;

namespace lancast {

class AudioCapturePulse {
public:
    AudioCapturePulse() = default;
    ~AudioCapturePulse();

    bool init(uint32_t sample_rate, uint16_t channels);
    std::optional<RawAudioFrame> capture_frame();
    void shutdown();

private:
    pa_simple* pa_ = nullptr;
    uint32_t sample_rate_ = 48000;
    uint16_t channels_ = 2;
    uint32_t frame_samples_ = 960; // 20ms at 48kHz
    bool initialized_ = false;
};

} // namespace lancast
