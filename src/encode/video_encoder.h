#pragma once

#include "core/types.h"
#include "core/ffmpeg_ptrs.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>
#include <cstdint>

namespace lancast {

class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder();

    bool init(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate);
    std::optional<EncodedPacket> encode(const RawVideoFrame& frame);
    void request_keyframe();
    bool set_bitrate(uint32_t bitrate);
    uint32_t current_bitrate() const { return bitrate_; }
    const std::vector<uint8_t>& extradata() const { return extradata_; }
    void shutdown();

private:
    AVCodecContextPtr ctx_;
    AVFramePtr av_frame_;
    AVPacketPtr av_packet_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fps_ = 0;
    uint32_t bitrate_ = 0;
    int64_t pts_ = 0;
    uint16_t frame_id_ = 0;
    std::atomic<bool> force_keyframe_{false};
    std::vector<uint8_t> extradata_;
    bool initialized_ = false;
    std::mutex mutex_;
};

} // namespace lancast
