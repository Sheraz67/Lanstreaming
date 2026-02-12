#include "net/packet_fragmenter.h"
#include <algorithm>
#include <cassert>

namespace lancast {

std::vector<Packet> PacketFragmenter::fragment(const EncodedPacket& encoded, uint32_t& sequence) {
    std::vector<Packet> fragments;

    const size_t data_size = encoded.data.size();
    if (data_size == 0) return fragments;

    const size_t num_frags = (data_size + MAX_FRAGMENT_DATA - 1) / MAX_FRAGMENT_DATA;
    assert(num_frags <= 255 && "Frame too large to fragment (>255 fragments)");

    PacketType ptype;
    uint8_t flags = FLAG_NONE;

    switch (encoded.type) {
        case FrameType::VideoKeyframe:
            ptype = PacketType::VIDEO_DATA;
            flags |= FLAG_KEYFRAME;
            break;
        case FrameType::VideoPFrame:
            ptype = PacketType::VIDEO_DATA;
            break;
        case FrameType::Audio:
            ptype = PacketType::AUDIO_DATA;
            break;
    }

    for (size_t i = 0; i < num_frags; ++i) {
        Packet pkt;
        pkt.header.magic = PROTOCOL_MAGIC;
        pkt.header.version = PROTOCOL_VERSION;
        pkt.header.type = static_cast<uint8_t>(ptype);
        pkt.header.flags = flags;
        if (i == 0) pkt.header.flags |= FLAG_FIRST;
        if (i == num_frags - 1) pkt.header.flags |= FLAG_LAST;
        pkt.header.sequence = sequence++;
        pkt.header.timestamp_us = static_cast<uint32_t>(encoded.pts_us & 0xFFFFFFFF);
        pkt.header.frame_id = encoded.frame_id;
        pkt.header.frag_idx = static_cast<uint8_t>(i);
        pkt.header.frag_total = static_cast<uint8_t>(num_frags);

        size_t offset = i * MAX_FRAGMENT_DATA;
        size_t chunk = std::min(MAX_FRAGMENT_DATA, data_size - offset);
        pkt.payload.assign(encoded.data.begin() + offset,
                           encoded.data.begin() + offset + chunk);

        fragments.push_back(std::move(pkt));
    }

    return fragments;
}

} // namespace lancast
