#include <gtest/gtest.h>
#include "net/packet_fragmenter.h"
#include "net/packet_assembler.h"
#include "core/types.h"
#include <numeric>

using namespace lancast;

TEST(LargeFrameRoundtripTest, FullHD_YUV420p_Frame) {
    // A 1920x1080 YUV420p frame is 1920*1080*3/2 = 3,110,400 bytes
    // This requires 3,110,400 / 1184 = 2628 fragments (exceeds old uint8_t limit of 255)
    constexpr size_t frame_size = 1920 * 1080 * 3 / 2;

    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 1;
    original.pts_us = 1000000;
    original.type = FrameType::VideoKeyframe;
    original.data.resize(frame_size);

    // Fill with a recognizable pattern
    for (size_t i = 0; i < frame_size; ++i) {
        original.data[i] = static_cast<uint8_t>(i % 251); // Prime modulus for pattern
    }

    uint16_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);

    // Verify we have more than 255 fragments (the old limit)
    ASSERT_GT(fragments.size(), 255u);

    size_t expected_frags = (frame_size + MAX_FRAGMENT_DATA - 1) / MAX_FRAGMENT_DATA;
    ASSERT_EQ(fragments.size(), expected_frags);

    // Verify header fields use uint16_t range
    EXPECT_EQ(fragments.back().header.frag_idx, static_cast<uint16_t>(expected_frags - 1));
    EXPECT_EQ(fragments.back().header.frag_total, static_cast<uint16_t>(expected_frags));

    // Feed all fragments to assembler and verify reconstruction
    std::optional<EncodedPacket> result;
    for (size_t i = 0; i < fragments.size(); ++i) {
        result = assembler.feed(fragments[i]);
        if (i < fragments.size() - 1) {
            EXPECT_FALSE(result.has_value()) << "Unexpected complete frame at fragment " << i;
        }
    }

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data.size(), original.data.size());
    EXPECT_EQ(result->data, original.data);
    EXPECT_EQ(result->frame_id, original.frame_id);
    EXPECT_EQ(result->type, FrameType::VideoKeyframe);
}

TEST(LargeFrameRoundtripTest, LargeFrame_OutOfOrder) {
    // Test with >255 fragments fed in reverse order
    constexpr size_t frame_size = 400000; // ~338 fragments

    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 42;
    original.pts_us = 500000;
    original.type = FrameType::VideoPFrame;
    original.data.resize(frame_size);
    std::iota(original.data.begin(), original.data.end(), 0);

    uint16_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);

    ASSERT_GT(fragments.size(), 255u);

    // Feed in reverse order
    std::optional<EncodedPacket> result;
    for (size_t i = fragments.size(); i > 0; --i) {
        result = assembler.feed(fragments[i - 1]);
        if (i > 1) {
            EXPECT_FALSE(result.has_value());
        }
    }

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data.size(), original.data.size());
    EXPECT_EQ(result->data, original.data);
}

TEST(LargeFrameRoundtripTest, SequenceWraparound) {
    // Test that uint16_t sequence wraps correctly near 65535
    PacketFragmenter fragmenter;

    EncodedPacket packet;
    packet.frame_id = 1;
    packet.type = FrameType::VideoPFrame;
    packet.data.resize(MAX_FRAGMENT_DATA * 2 + 1); // 3 fragments

    uint16_t seq = 65534; // Near max uint16_t
    auto fragments = fragmenter.fragment(packet, seq);

    ASSERT_EQ(fragments.size(), 3u);
    EXPECT_EQ(fragments[0].header.sequence, 65534u);
    EXPECT_EQ(fragments[1].header.sequence, 65535u);
    EXPECT_EQ(fragments[2].header.sequence, 0u); // Wrapped
    EXPECT_EQ(seq, 1u); // Counter after wrap
}
