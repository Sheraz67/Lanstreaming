#include "net/packet_assembler.h"

namespace lancast {

std::optional<EncodedPacket> PacketAssembler::feed(const Packet& packet) {
    const auto& h = packet.header;
    if (!h.is_valid()) return std::nullopt;
    if (h.frag_total == 0) return std::nullopt;

    FrameKey key{h.frame_id, h.type};

    auto it = pending_.find(key);
    if (it == pending_.end()) {
        FrameState state;
        state.frame_id = h.frame_id;
        state.frag_total = h.frag_total;
        state.frags_received = 0;
        state.type = static_cast<PacketType>(h.type);
        state.flags = h.flags;
        state.timestamp_us = h.timestamp_us;
        state.fragments.resize(h.frag_total);
        state.created = std::chrono::steady_clock::now();
        auto [inserted, _] = pending_.emplace(key, std::move(state));
        it = inserted;
    }

    auto& state = it->second;

    if (h.frag_idx >= state.frag_total) return std::nullopt;

    // Avoid duplicate fragments
    if (!state.fragments[h.frag_idx].empty()) return std::nullopt;

    state.fragments[h.frag_idx] = packet.payload;
    state.frags_received++;
    state.flags |= h.flags; // Accumulate flags (e.g. KEYFRAME)

    if (state.frags_received < state.frag_total) return std::nullopt;

    // All fragments received - assemble
    EncodedPacket result;
    size_t total_size = 0;
    for (const auto& frag : state.fragments) {
        total_size += frag.size();
    }
    result.data.reserve(total_size);
    for (const auto& frag : state.fragments) {
        result.data.insert(result.data.end(), frag.begin(), frag.end());
    }
    result.frame_id = state.frame_id;
    result.pts_us = static_cast<int64_t>(state.timestamp_us);

    if (state.type == PacketType::AUDIO_DATA) {
        result.type = FrameType::Audio;
    } else if (state.flags & FLAG_KEYFRAME) {
        result.type = FrameType::VideoKeyframe;
    } else {
        result.type = FrameType::VideoPFrame;
    }

    pending_.erase(it);
    return result;
}

std::vector<IncompleteKeyframe> PacketAssembler::check_incomplete_keyframes(int64_t age_ms) {
    std::vector<IncompleteKeyframe> result;
    auto now = std::chrono::steady_clock::now();
    auto threshold = std::chrono::milliseconds(age_ms);

    for (auto& [key, state] : pending_) {
        // Only check video keyframes
        if (!(state.flags & FLAG_KEYFRAME)) continue;
        if (state.nack_sent) continue;
        if (now - state.created < threshold) continue;

        IncompleteKeyframe kf;
        kf.frame_id = state.frame_id;
        kf.frag_total = state.frag_total;

        for (uint16_t i = 0; i < state.frag_total; ++i) {
            if (state.fragments[i].empty()) {
                kf.missing_indices.push_back(i);
            }
        }

        if (!kf.missing_indices.empty()) {
            state.nack_sent = true;
            result.push_back(std::move(kf));
        }
    }

    return result;
}

void PacketAssembler::purge_stale(int64_t timeout_ms) {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);

    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.created > timeout) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace lancast
