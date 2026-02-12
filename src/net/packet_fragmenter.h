#pragma once

#include "net/protocol.h"
#include "core/types.h"
#include <vector>
#include <cstdint>

namespace lancast {

class PacketFragmenter {
public:
    // Fragments an encoded packet into UDP-sized Packets.
    // Each fragment gets a 16-byte header + up to MAX_FRAGMENT_DATA bytes of payload.
    std::vector<Packet> fragment(const EncodedPacket& encoded, uint32_t& sequence);
};

} // namespace lancast
