#pragma once

#include "core/types.h"
#include "core/ffmpeg_ptrs.h"
#include <optional>
#include <cstdint>

namespace lancast {

class AudioDecoder {
public:
    AudioDecoder() = default;
    ~AudioDecoder();

    bool init(uint32_t sample_rate, uint16_t channels);
    std::optional<RawAudioFrame> decode(const EncodedPacket& packet);
    void shutdown();

private:
    AVCodecContextPtr ctx_;
    AVFramePtr av_frame_;
    AVPacketPtr av_packet_;
    SwrContextPtr swr_;

    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    bool initialized_ = false;
    bool swr_initialized_ = false;
};

} // namespace lancast
