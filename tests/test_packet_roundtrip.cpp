#include <gtest/gtest.h>
#include "net/packet_fragmenter.h"
#include "net/packet_assembler.h"
#include "core/types.h"
#include <numeric>

using namespace lancast;

TEST(PacketRoundtripTest, SmallPacketSingleFragment) {
    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 1;
    original.pts_us = 100000;
    original.type = FrameType::VideoPFrame;
    original.data = {0x00, 0x01, 0x02, 0x03, 0x04};

    uint32_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);

    ASSERT_EQ(fragments.size(), 1u);
    EXPECT_EQ(fragments[0].header.frag_idx, 0u);
    EXPECT_EQ(fragments[0].header.frag_total, 1u);
    EXPECT_TRUE(fragments[0].header.flags & FLAG_FIRST);
    EXPECT_TRUE(fragments[0].header.flags & FLAG_LAST);

    auto result = assembler.feed(fragments[0]);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, original.data);
    EXPECT_EQ(result->frame_id, original.frame_id);
    EXPECT_EQ(result->type, FrameType::VideoPFrame);
}

TEST(PacketRoundtripTest, LargePacketMultipleFragments) {
    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 42;
    original.pts_us = 200000;
    original.type = FrameType::VideoKeyframe;

    // Create data larger than MAX_FRAGMENT_DATA
    original.data.resize(MAX_FRAGMENT_DATA * 3 + 500);
    std::iota(original.data.begin(), original.data.end(), 0);

    uint32_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);

    EXPECT_EQ(fragments.size(), 4u);

    // Feed all fragments
    std::optional<EncodedPacket> result;
    for (size_t i = 0; i < fragments.size(); ++i) {
        EXPECT_EQ(fragments[i].header.frag_idx, i);
        EXPECT_EQ(fragments[i].header.frag_total, 4u);
        EXPECT_EQ(fragments[i].header.frame_id, 42u);
        EXPECT_TRUE(fragments[i].header.flags & FLAG_KEYFRAME);

        result = assembler.feed(fragments[i]);
        if (i < fragments.size() - 1) {
            EXPECT_FALSE(result.has_value());
        }
    }

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data.size(), original.data.size());
    EXPECT_EQ(result->data, original.data);
    EXPECT_EQ(result->type, FrameType::VideoKeyframe);
}

TEST(PacketRoundtripTest, OutOfOrderFragments) {
    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 10;
    original.pts_us = 300000;
    original.type = FrameType::VideoPFrame;
    original.data.resize(MAX_FRAGMENT_DATA * 2 + 100);
    std::iota(original.data.begin(), original.data.end(), 0);

    uint32_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);
    ASSERT_EQ(fragments.size(), 3u);

    // Feed in reverse order
    EXPECT_FALSE(assembler.feed(fragments[2]).has_value());
    EXPECT_FALSE(assembler.feed(fragments[0]).has_value());
    auto result = assembler.feed(fragments[1]);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, original.data);
}

TEST(PacketRoundtripTest, DuplicateFragment) {
    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 5;
    original.pts_us = 400000;
    original.type = FrameType::Audio;
    original.data.resize(MAX_FRAGMENT_DATA + 100);
    std::iota(original.data.begin(), original.data.end(), 0);

    uint32_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);
    ASSERT_EQ(fragments.size(), 2u);

    // Feed first fragment twice
    EXPECT_FALSE(assembler.feed(fragments[0]).has_value());
    EXPECT_FALSE(assembler.feed(fragments[0]).has_value()); // duplicate, ignored

    auto result = assembler.feed(fragments[1]);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, original.data);
    EXPECT_EQ(result->type, FrameType::Audio);
}

TEST(PacketRoundtripTest, ExactlyOneFragment) {
    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    EncodedPacket original;
    original.frame_id = 99;
    original.pts_us = 500000;
    original.type = FrameType::VideoKeyframe;
    original.data.resize(MAX_FRAGMENT_DATA); // Exactly one fragment
    std::iota(original.data.begin(), original.data.end(), 0);

    uint32_t seq = 0;
    auto fragments = fragmenter.fragment(original, seq);
    ASSERT_EQ(fragments.size(), 1u);

    auto result = assembler.feed(fragments[0]);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data, original.data);
}

TEST(PacketRoundtripTest, EmptyPacket) {
    PacketFragmenter fragmenter;

    EncodedPacket empty;
    empty.frame_id = 1;
    empty.data.clear();

    uint32_t seq = 0;
    auto fragments = fragmenter.fragment(empty, seq);
    EXPECT_TRUE(fragments.empty());
}

TEST(PacketRoundtripTest, SequenceNumberIncrement) {
    PacketFragmenter fragmenter;

    EncodedPacket p;
    p.frame_id = 1;
    p.type = FrameType::VideoPFrame;
    p.data.resize(MAX_FRAGMENT_DATA * 2 + 1); // 3 fragments

    uint32_t seq = 100;
    auto fragments = fragmenter.fragment(p, seq);

    EXPECT_EQ(fragments[0].header.sequence, 100u);
    EXPECT_EQ(fragments[1].header.sequence, 101u);
    EXPECT_EQ(fragments[2].header.sequence, 102u);
    EXPECT_EQ(seq, 103u);
}

TEST(PacketRoundtripTest, MultipleFramesInterleaved) {
    PacketFragmenter fragmenter;
    PacketAssembler assembler;

    // Create two frames
    EncodedPacket frame1;
    frame1.frame_id = 1;
    frame1.type = FrameType::VideoPFrame;
    frame1.data.resize(MAX_FRAGMENT_DATA + 100);
    std::fill(frame1.data.begin(), frame1.data.end(), 0xAA);

    EncodedPacket frame2;
    frame2.frame_id = 2;
    frame2.type = FrameType::VideoKeyframe;
    frame2.data.resize(MAX_FRAGMENT_DATA + 200);
    std::fill(frame2.data.begin(), frame2.data.end(), 0xBB);

    uint32_t seq = 0;
    auto frags1 = fragmenter.fragment(frame1, seq);
    auto frags2 = fragmenter.fragment(frame2, seq);

    // Interleave fragments
    EXPECT_FALSE(assembler.feed(frags1[0]).has_value());
    EXPECT_FALSE(assembler.feed(frags2[0]).has_value());

    auto r1 = assembler.feed(frags1[1]);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->data, frame1.data);

    auto r2 = assembler.feed(frags2[1]);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->data, frame2.data);
}
