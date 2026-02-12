#include "encode/video_encoder.h"
#include "decode/video_decoder.h"
#include "core/types.h"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace lancast;

// Create a synthetic YUV420p frame with a gradient pattern
static RawVideoFrame make_test_frame(uint32_t w, uint32_t h) {
    RawVideoFrame frame;
    frame.width = w;
    frame.height = h;
    frame.pts_us = 0;

    uint32_t y_size = w * h;
    uint32_t uv_size = (w / 2) * (h / 2);
    frame.data.resize(y_size + 2 * uv_size);

    // Y plane: horizontal gradient
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            frame.data[row * w + col] = static_cast<uint8_t>((col * 255) / w);
        }
    }
    // U plane: vertical gradient
    for (uint32_t row = 0; row < h / 2; ++row) {
        for (uint32_t col = 0; col < w / 2; ++col) {
            frame.data[y_size + row * (w / 2) + col] = static_cast<uint8_t>((row * 255) / (h / 2));
        }
    }
    // V plane: constant 128
    std::memset(frame.data.data() + y_size + uv_size, 128, uv_size);

    return frame;
}

TEST(VideoCodec, EncodeDecodeRoundtrip) {
    const uint32_t w = 320, h = 240, fps = 30, bitrate = 1000000;

    VideoEncoder encoder;
    ASSERT_TRUE(encoder.init(w, h, fps, bitrate));
    ASSERT_FALSE(encoder.extradata().empty()) << "Encoder should produce SPS/PPS extradata";

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.init(w, h, encoder.extradata()));

    auto test_frame = make_test_frame(w, h);

    // Encode
    auto encoded = encoder.encode(test_frame);
    ASSERT_TRUE(encoded.has_value()) << "Encoder should produce output";
    EXPECT_FALSE(encoded->data.empty()) << "Encoded data should not be empty";
    EXPECT_EQ(encoded->type, FrameType::VideoKeyframe)
        << "First frame should be a keyframe";

    // Decode
    auto decoded = decoder.decode(*encoded);
    ASSERT_TRUE(decoded.has_value()) << "Decoder should produce output";
    EXPECT_EQ(decoded->width, w);
    EXPECT_EQ(decoded->height, h);

    uint32_t expected_size = w * h + 2 * (w / 2) * (h / 2);
    EXPECT_EQ(decoded->data.size(), expected_size)
        << "Decoded frame should be correct YUV420p size";

    encoder.shutdown();
    decoder.shutdown();
}

TEST(VideoCodec, KeyframeRequest) {
    const uint32_t w = 320, h = 240, fps = 30, bitrate = 1000000;

    VideoEncoder encoder;
    ASSERT_TRUE(encoder.init(w, h, fps, bitrate));

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.init(w, h, encoder.extradata()));

    auto test_frame = make_test_frame(w, h);

    // Encode first frame (keyframe)
    auto encoded1 = encoder.encode(test_frame);
    ASSERT_TRUE(encoded1.has_value());
    EXPECT_EQ(encoded1->type, FrameType::VideoKeyframe);

    // Decode first frame so decoder has reference
    auto dec1 = decoder.decode(*encoded1);
    ASSERT_TRUE(dec1.has_value());

    // Encode second frame (should be P-frame)
    test_frame.pts_us = 33333;
    auto encoded2 = encoder.encode(test_frame);
    ASSERT_TRUE(encoded2.has_value());
    EXPECT_EQ(encoded2->type, FrameType::VideoPFrame)
        << "Second frame should be a P-frame";

    // Decode second frame
    auto dec2 = decoder.decode(*encoded2);
    ASSERT_TRUE(dec2.has_value());

    // Request keyframe, then encode third frame
    encoder.request_keyframe();
    test_frame.pts_us = 66666;
    auto encoded3 = encoder.encode(test_frame);
    ASSERT_TRUE(encoded3.has_value());
    EXPECT_EQ(encoded3->type, FrameType::VideoKeyframe)
        << "Frame after request_keyframe should be a keyframe";

    encoder.shutdown();
    decoder.shutdown();
}

TEST(VideoCodec, MultipleFrames) {
    const uint32_t w = 320, h = 240, fps = 30, bitrate = 1000000;

    VideoEncoder encoder;
    ASSERT_TRUE(encoder.init(w, h, fps, bitrate));

    VideoDecoder decoder;
    ASSERT_TRUE(decoder.init(w, h, encoder.extradata()));

    auto test_frame = make_test_frame(w, h);
    int decoded_count = 0;

    for (int i = 0; i < 10; ++i) {
        test_frame.pts_us = i * 33333;
        auto encoded = encoder.encode(test_frame);
        ASSERT_TRUE(encoded.has_value()) << "Frame " << i << " should encode";

        auto decoded = decoder.decode(*encoded);
        if (decoded) {
            EXPECT_EQ(decoded->width, w);
            EXPECT_EQ(decoded->height, h);
            decoded_count++;
        }
    }

    EXPECT_GT(decoded_count, 0) << "Should decode at least some frames";

    encoder.shutdown();
    decoder.shutdown();
}
