#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>

namespace lancast {

// 16-byte UDP packet header
// | Magic(1) | Version(1) | Type(1) | Flags(1) | Sequence(4) | Timestamp_us(4) | FrameID(2) | FragIdx(1) | FragTotal(1) |
static constexpr uint8_t  PROTOCOL_MAGIC   = 0xAA;
static constexpr uint8_t  PROTOCOL_VERSION = 1;
static constexpr uint16_t DEFAULT_PORT     = 7878;
static constexpr size_t   MAX_UDP_PAYLOAD  = 1200;   // Safe for most MTUs
static constexpr size_t   HEADER_SIZE      = 16;
static constexpr size_t   MAX_FRAGMENT_DATA = MAX_UDP_PAYLOAD - HEADER_SIZE; // 1184 bytes

enum class PacketType : uint8_t {
    VIDEO_DATA    = 0x01,
    AUDIO_DATA    = 0x02,
    HELLO         = 0x10,
    WELCOME       = 0x11,
    ACK           = 0x12,
    NACK          = 0x13,
    KEYFRAME_REQ  = 0x14,
    PING          = 0x20,
    PONG          = 0x21,
    BYE           = 0x30,
    STREAM_CONFIG = 0x40,
};

enum PacketFlags : uint8_t {
    FLAG_NONE     = 0x00,
    FLAG_KEYFRAME = 0x01,
    FLAG_FIRST    = 0x02, // First fragment of frame
    FLAG_LAST     = 0x04, // Last fragment of frame
};

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  magic    = 0;
    uint8_t  version  = 0;
    uint8_t  type     = 0;
    uint8_t  flags    = 0;
    uint32_t sequence = 0;
    uint32_t timestamp_us = 0;
    uint16_t frame_id = 0;
    uint8_t  frag_idx = 0;
    uint8_t  frag_total = 0;

    void to_network(uint8_t* buf) const {
        std::memcpy(buf, this, HEADER_SIZE);
    }

    static PacketHeader from_network(const uint8_t* buf) {
        PacketHeader h;
        std::memcpy(&h, buf, HEADER_SIZE);
        return h;
    }

    bool is_valid() const {
        return magic == 0xAA && version == PROTOCOL_VERSION;
    }
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be exactly 16 bytes");

// Control message payloads

struct HelloPayload {
    uint32_t client_id = 0; // Random client identifier
};

struct WelcomePayload {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    uint32_t video_bitrate = 0;
    uint32_t audio_sample_rate = 0;
    uint16_t audio_channels = 0;
};

// A complete UDP packet (header + payload data)
struct Packet {
    PacketHeader header;
    std::vector<uint8_t> payload;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf(HEADER_SIZE + payload.size());
        header.to_network(buf.data());
        if (!payload.empty()) {
            std::memcpy(buf.data() + HEADER_SIZE, payload.data(), payload.size());
        }
        return buf;
    }

    static Packet deserialize(const uint8_t* data, size_t len) {
        Packet p;
        if (len < HEADER_SIZE) return p;
        p.header = PacketHeader::from_network(data);
        if (len > HEADER_SIZE) {
            p.payload.assign(data + HEADER_SIZE, data + len);
        }
        return p;
    }
};

} // namespace lancast
