#pragma once

#include "core/types.h"
#include "core/ffmpeg_ptrs.h"
#include <optional>
#include <vector>
#include <cstdint>

namespace lancast {

class AudioEncoder {
public:
    AudioEncoder() = default;
    ~AudioEncoder();

    bool init(uint32_t sample_rate, uint16_t channels, uint32_t bitrate);
    std::optional<EncodedPacket> encode(const RawAudioFrame& frame);
    const std::vector<uint8_t>& extradata() const { return extradata_; }
    void shutdown();

private:
    AVCodecContextPtr ctx_;
    AVFramePtr av_frame_;
    AVPacketPtr av_packet_;

    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    int64_t pts_ = 0;
    uint16_t frame_id_ = 0;
    std::vector<uint8_t> extradata_;
    bool initialized_ = false;
};

} // namespace lancast
