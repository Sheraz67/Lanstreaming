#pragma once

#include "core/types.h"
#include <cstdint>
#include <optional>

namespace lancast {

class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual bool init(uint32_t sample_rate, uint16_t channels) = 0;
    virtual std::optional<RawAudioFrame> capture_frame() = 0;
    virtual void shutdown() = 0;
};

} // namespace lancast
