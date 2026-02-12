#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace lancast {

struct StreamConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t video_bitrate = 6000000; // 6 Mbps
    uint32_t audio_sample_rate = 48000;
    uint16_t audio_channels = 2;
    std::vector<uint8_t> codec_data;  // SPS/PPS extradata (Annex B)
};

struct RawVideoFrame {
    std::vector<uint8_t> data;    // YUV420P pixel data
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_us = 0;           // Presentation timestamp in microseconds
};

struct RawAudioFrame {
    std::vector<float> samples;   // Interleaved float32 PCM
    uint32_t sample_rate = 48000;
    uint16_t channels = 2;
    uint32_t num_samples = 0;     // Samples per channel
    int64_t pts_us = 0;
};

enum class FrameType : uint8_t {
    VideoKeyframe = 0,
    VideoPFrame   = 1,
    Audio         = 2,
};

struct EncodedPacket {
    std::vector<uint8_t> data;
    FrameType type = FrameType::VideoPFrame;
    int64_t pts_us = 0;
    uint16_t frame_id = 0;
};

} // namespace lancast
