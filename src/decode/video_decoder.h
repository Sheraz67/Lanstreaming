#pragma once

#include "core/types.h"
#include "core/ffmpeg_ptrs.h"
#include <optional>
#include <vector>
#include <cstdint>

namespace lancast {

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    bool init(uint32_t width, uint32_t height, const std::vector<uint8_t>& extradata);
    std::optional<RawVideoFrame> decode(const EncodedPacket& packet);
    void shutdown();

private:
    AVCodecContextPtr ctx_;
    AVFramePtr av_frame_;
    AVPacketPtr av_packet_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool initialized_ = false;
};

} // namespace lancast
