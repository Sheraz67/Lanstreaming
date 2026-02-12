#include "encode/audio_encoder.h"
#include "decode/audio_decoder.h"
#include "core/types.h"

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace lancast;

// Create a synthetic 48kHz stereo float32 sine wave frame (20ms = 960 samples)
static RawAudioFrame make_test_audio_frame(uint32_t sample_rate, uint16_t channels,
                                            uint32_t num_samples, float frequency = 440.0f) {
    RawAudioFrame frame;
    frame.sample_rate = sample_rate;
    frame.channels = channels;
    frame.num_samples = num_samples;
    frame.pts_us = 0;
    frame.samples.resize(static_cast<size_t>(num_samples) * channels);

    for (uint32_t i = 0; i < num_samples; ++i) {
        float sample = std::sin(2.0f * static_cast<float>(M_PI) * frequency *
                                static_cast<float>(i) / static_cast<float>(sample_rate));
        for (uint16_t ch = 0; ch < channels; ++ch) {
            frame.samples[static_cast<size_t>(i) * channels + ch] = sample * 0.5f;
        }
    }

    return frame;
}

TEST(AudioCodec, EncodeDecodeRoundtrip) {
    const uint32_t sample_rate = 48000;
    const uint16_t channels = 2;
    const uint32_t bitrate = 128000;

    AudioEncoder encoder;
    ASSERT_TRUE(encoder.init(sample_rate, channels, bitrate));

    AudioDecoder decoder;
    ASSERT_TRUE(decoder.init(sample_rate, channels));

    // 960 samples = 20ms at 48kHz (Opus frame size)
    auto test_frame = make_test_audio_frame(sample_rate, channels, 960);

    // Encode
    auto encoded = encoder.encode(test_frame);
    ASSERT_TRUE(encoded.has_value()) << "Encoder should produce output";
    EXPECT_FALSE(encoded->data.empty()) << "Encoded data should not be empty";
    EXPECT_EQ(encoded->type, FrameType::Audio);

    // Decode
    auto decoded = decoder.decode(*encoded);
    ASSERT_TRUE(decoded.has_value()) << "Decoder should produce output";
    EXPECT_EQ(decoded->sample_rate, sample_rate);
    EXPECT_EQ(decoded->channels, channels);
    EXPECT_GT(decoded->num_samples, 0u) << "Decoded frame should have samples";
    EXPECT_FALSE(decoded->samples.empty()) << "Decoded samples should not be empty";

    encoder.shutdown();
    decoder.shutdown();
}

TEST(AudioCodec, MultipleFrames) {
    const uint32_t sample_rate = 48000;
    const uint16_t channels = 2;
    const uint32_t bitrate = 128000;

    AudioEncoder encoder;
    ASSERT_TRUE(encoder.init(sample_rate, channels, bitrate));

    AudioDecoder decoder;
    ASSERT_TRUE(decoder.init(sample_rate, channels));

    int decoded_count = 0;

    for (int i = 0; i < 10; ++i) {
        auto test_frame = make_test_audio_frame(sample_rate, channels, 960,
                                                 440.0f + static_cast<float>(i) * 50.0f);
        test_frame.pts_us = i * 20000; // 20ms per frame

        auto encoded = encoder.encode(test_frame);
        ASSERT_TRUE(encoded.has_value()) << "Frame " << i << " should encode";
        EXPECT_EQ(encoded->type, FrameType::Audio);

        auto decoded = decoder.decode(*encoded);
        if (decoded) {
            EXPECT_EQ(decoded->sample_rate, sample_rate);
            EXPECT_EQ(decoded->channels, channels);
            EXPECT_GT(decoded->num_samples, 0u);
            decoded_count++;
        }
    }

    EXPECT_GT(decoded_count, 0) << "Should decode at least some frames";

    encoder.shutdown();
    decoder.shutdown();
}

TEST(AudioCodec, EncodedDataIsCompressed) {
    const uint32_t sample_rate = 48000;
    const uint16_t channels = 2;
    const uint32_t bitrate = 128000;

    AudioEncoder encoder;
    ASSERT_TRUE(encoder.init(sample_rate, channels, bitrate));

    auto test_frame = make_test_audio_frame(sample_rate, channels, 960);

    auto encoded = encoder.encode(test_frame);
    ASSERT_TRUE(encoded.has_value());

    // Raw size: 960 samples * 2 channels * 4 bytes = 7680 bytes
    // Opus at 128kbps should compress to much less
    size_t raw_size = 960 * 2 * sizeof(float);
    EXPECT_LT(encoded->data.size(), raw_size)
        << "Encoded data should be smaller than raw PCM";

    encoder.shutdown();
}
