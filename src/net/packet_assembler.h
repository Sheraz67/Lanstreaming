#pragma once

#include "net/protocol.h"
#include "core/types.h"
#include <unordered_map>
#include <vector>
#include <optional>
#include <chrono>

namespace lancast {

struct IncompleteKeyframe {
    uint16_t frame_id = 0;
    uint16_t frag_total = 0;
    std::vector<uint16_t> missing_indices;
};

class PacketAssembler {
public:
    // Feed a received packet. Returns a complete EncodedPacket when all fragments arrive.
    std::optional<EncodedPacket> feed(const Packet& packet);

    // Check for incomplete keyframes older than age_ms. Returns info for NACKing.
    // Each frame is only reported once (marks nack_sent).
    std::vector<IncompleteKeyframe> check_incomplete_keyframes(int64_t age_ms = 100);

    // Purge stale incomplete frames older than timeout_ms
    void purge_stale(int64_t timeout_ms = 500);

private:
    struct FrameState {
        uint16_t frame_id = 0;
        uint16_t frag_total = 0;
        uint16_t frags_received = 0;
        PacketType type = PacketType::VIDEO_DATA;
        uint8_t flags = 0;
        uint32_t timestamp_us = 0;
        std::vector<std::vector<uint8_t>> fragments; // indexed by frag_idx
        std::chrono::steady_clock::time_point created;
        bool nack_sent = false;
    };

    // Key: frame_id (combined with type to handle video/audio with same IDs)
    struct FrameKey {
        uint16_t frame_id;
        uint8_t type;
        bool operator==(const FrameKey& o) const { return frame_id == o.frame_id && type == o.type; }
    };
    struct FrameKeyHash {
        size_t operator()(const FrameKey& k) const {
            return std::hash<uint32_t>()(k.frame_id | (k.type << 16));
        }
    };

    std::unordered_map<FrameKey, FrameState, FrameKeyHash> pending_;
};

} // namespace lancast
