#include <gtest/gtest.h>
#include "net/protocol.h"
#include "net/packet_assembler.h"
#include "net/client.h"

using namespace lancast;

// --- PingPayload tests ---

TEST(PingPayload, SizeIs8Bytes) {
    EXPECT_EQ(sizeof(PingPayload), 8u);
}

TEST(PingPayload, Roundtrip) {
    PingPayload pp;
    pp.timestamp_us = 123456789012ULL;

    uint8_t buf[sizeof(PingPayload)];
    std::memcpy(buf, &pp, sizeof(PingPayload));

    PingPayload pp2;
    std::memcpy(&pp2, buf, sizeof(PingPayload));
    EXPECT_EQ(pp2.timestamp_us, 123456789012ULL);
}

// --- NackPayload tests ---

TEST(NackPayload, SizeIs4Bytes) {
    EXPECT_EQ(sizeof(NackPayload), 4u);
}

TEST(NackPayload, Roundtrip) {
    NackPayload np;
    np.frame_id = 42;
    np.num_missing = 3;

    uint8_t buf[sizeof(NackPayload)];
    std::memcpy(buf, &np, sizeof(NackPayload));

    NackPayload np2;
    std::memcpy(&np2, buf, sizeof(NackPayload));
    EXPECT_EQ(np2.frame_id, 42);
    EXPECT_EQ(np2.num_missing, 3);
}

TEST(NackPayload, WithMissingIndices) {
    // Simulate a NACK packet with missing fragment indices appended
    NackPayload np;
    np.frame_id = 10;
    np.num_missing = 3;

    std::vector<uint16_t> missing = {0, 5, 12};

    std::vector<uint8_t> payload(sizeof(NackPayload) + missing.size() * sizeof(uint16_t));
    std::memcpy(payload.data(), &np, sizeof(NackPayload));
    std::memcpy(payload.data() + sizeof(NackPayload), missing.data(), missing.size() * sizeof(uint16_t));

    // Parse it back
    NackPayload np2;
    std::memcpy(&np2, payload.data(), sizeof(NackPayload));
    EXPECT_EQ(np2.frame_id, 10);
    EXPECT_EQ(np2.num_missing, 3);

    for (uint16_t i = 0; i < np2.num_missing; ++i) {
        uint16_t idx;
        std::memcpy(&idx, payload.data() + sizeof(NackPayload) + i * sizeof(uint16_t), sizeof(uint16_t));
        EXPECT_EQ(idx, missing[i]);
    }
}

// --- PacketAssembler incomplete keyframe detection ---

TEST(PacketAssembler, DetectsIncompleteKeyframe) {
    PacketAssembler assembler;

    // Send 2 out of 3 fragments of a keyframe
    for (uint16_t i = 0; i < 2; ++i) {
        Packet pkt;
        pkt.header.magic = PROTOCOL_MAGIC;
        pkt.header.version = PROTOCOL_VERSION;
        pkt.header.type = static_cast<uint8_t>(PacketType::VIDEO_DATA);
        pkt.header.flags = FLAG_KEYFRAME;
        pkt.header.frame_id = 1;
        pkt.header.frag_idx = i;
        pkt.header.frag_total = 3;
        pkt.payload = {0x00, 0x01, 0x02};
        assembler.feed(pkt);
    }

    // Immediately should not report (too young)
    auto incomplete = assembler.check_incomplete_keyframes(100);
    EXPECT_TRUE(incomplete.empty());

    // Wait and check again (use 0ms threshold for test)
    incomplete = assembler.check_incomplete_keyframes(0);
    ASSERT_EQ(incomplete.size(), 1u);
    EXPECT_EQ(incomplete[0].frame_id, 1);
    EXPECT_EQ(incomplete[0].frag_total, 3);
    EXPECT_EQ(incomplete[0].missing_indices.size(), 1u);
    EXPECT_EQ(incomplete[0].missing_indices[0], 2); // Fragment 2 is missing

    // Should not report again (nack_sent = true)
    auto incomplete2 = assembler.check_incomplete_keyframes(0);
    EXPECT_TRUE(incomplete2.empty());
}

TEST(PacketAssembler, PurgesStaleFrames) {
    PacketAssembler assembler;

    // Send 1 out of 5 fragments
    Packet pkt;
    pkt.header.magic = PROTOCOL_MAGIC;
    pkt.header.version = PROTOCOL_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::VIDEO_DATA);
    pkt.header.flags = 0;
    pkt.header.frame_id = 99;
    pkt.header.frag_idx = 0;
    pkt.header.frag_total = 5;
    pkt.payload = {0xFF};
    assembler.feed(pkt);

    // With 0ms threshold, should purge immediately
    assembler.purge_stale(0);

    // Now send the remaining fragments — should not complete (frame was purged)
    for (uint16_t i = 1; i < 5; ++i) {
        pkt.header.frag_idx = i;
        auto result = assembler.feed(pkt);
        if (i < 4) {
            EXPECT_FALSE(result.has_value());
        }
    }
    // The last fragment creates a new frame entry but still needs all fragments
    // since the original was purged
}

// --- ConnectionState enum ---

TEST(ConnectionState, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::Disconnected), 0);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::Connecting), 1);
    EXPECT_EQ(static_cast<uint8_t>(ConnectionState::Connected), 2);
}
