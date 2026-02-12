#include <gtest/gtest.h>
#include "net/protocol.h"

using namespace lancast;

TEST(ProtocolTest, HeaderSize) {
    EXPECT_EQ(sizeof(PacketHeader), 16u);
    EXPECT_EQ(HEADER_SIZE, 16u);
}

TEST(ProtocolTest, HeaderSerializeRoundtrip) {
    PacketHeader h;
    h.magic = PROTOCOL_MAGIC;
    h.version = PROTOCOL_VERSION;
    h.type = static_cast<uint8_t>(PacketType::VIDEO_DATA);
    h.flags = FLAG_KEYFRAME | FLAG_FIRST;
    h.sequence = 12345;
    h.timestamp_us = 67890;
    h.frame_id = 42;
    h.frag_idx = 3;
    h.frag_total = 5;

    uint8_t buf[16];
    h.to_network(buf);

    PacketHeader h2 = PacketHeader::from_network(buf);

    EXPECT_EQ(h2.magic, PROTOCOL_MAGIC);
    EXPECT_EQ(h2.version, PROTOCOL_VERSION);
    EXPECT_EQ(h2.type, static_cast<uint8_t>(PacketType::VIDEO_DATA));
    EXPECT_EQ(h2.flags, FLAG_KEYFRAME | FLAG_FIRST);
    EXPECT_EQ(h2.sequence, 12345u);
    EXPECT_EQ(h2.timestamp_us, 67890u);
    EXPECT_EQ(h2.frame_id, 42u);
    EXPECT_EQ(h2.frag_idx, 3u);
    EXPECT_EQ(h2.frag_total, 5u);
}

TEST(ProtocolTest, HeaderValidation) {
    PacketHeader valid;
    valid.magic = PROTOCOL_MAGIC;
    valid.version = PROTOCOL_VERSION;
    EXPECT_TRUE(valid.is_valid());

    PacketHeader bad_magic;
    bad_magic.magic = 0x00;
    bad_magic.version = PROTOCOL_VERSION;
    EXPECT_FALSE(bad_magic.is_valid());

    PacketHeader bad_version;
    bad_version.magic = PROTOCOL_MAGIC;
    bad_version.version = 99;
    EXPECT_FALSE(bad_version.is_valid());
}

TEST(ProtocolTest, PacketSerializeRoundtrip) {
    Packet p;
    p.header.magic = PROTOCOL_MAGIC;
    p.header.version = PROTOCOL_VERSION;
    p.header.type = static_cast<uint8_t>(PacketType::HELLO);
    p.header.sequence = 1;
    p.payload = {0xDE, 0xAD, 0xBE, 0xEF};

    auto serialized = p.serialize();
    EXPECT_EQ(serialized.size(), HEADER_SIZE + 4);

    Packet p2 = Packet::deserialize(serialized.data(), serialized.size());
    EXPECT_TRUE(p2.header.is_valid());
    EXPECT_EQ(p2.header.type, static_cast<uint8_t>(PacketType::HELLO));
    EXPECT_EQ(p2.header.sequence, 1u);
    ASSERT_EQ(p2.payload.size(), 4u);
    EXPECT_EQ(p2.payload[0], 0xDE);
    EXPECT_EQ(p2.payload[3], 0xEF);
}

TEST(ProtocolTest, EmptyPacket) {
    Packet p;
    p.header.magic = PROTOCOL_MAGIC;
    p.header.version = PROTOCOL_VERSION;
    p.header.type = static_cast<uint8_t>(PacketType::PING);

    auto serialized = p.serialize();
    EXPECT_EQ(serialized.size(), HEADER_SIZE);

    Packet p2 = Packet::deserialize(serialized.data(), serialized.size());
    EXPECT_TRUE(p2.header.is_valid());
    EXPECT_TRUE(p2.payload.empty());
}

TEST(ProtocolTest, TooShortPacket) {
    uint8_t buf[8] = {};
    Packet p = Packet::deserialize(buf, 8);
    EXPECT_FALSE(p.header.is_valid()); // magic/version not set
}

TEST(ProtocolTest, MaxFragmentData) {
    EXPECT_EQ(MAX_FRAGMENT_DATA, MAX_UDP_PAYLOAD - HEADER_SIZE);
    EXPECT_EQ(MAX_FRAGMENT_DATA, 1184u);
}
