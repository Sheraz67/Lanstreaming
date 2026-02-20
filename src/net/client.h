#pragma once

#include "net/socket.h"
#include "net/protocol.h"
#include "net/packet_assembler.h"
#include "net/packet_fragmenter.h"
#include "core/types.h"
#include "core/thread_safe_queue.h"
#include <atomic>
#include <functional>

namespace lancast {

enum class ConnectionState : uint8_t {
    Disconnected = 0,
    Connecting   = 1,
    Connected    = 2,
};

class Client {
public:
    Client();
    ~Client();

    bool connect(const std::string& host_ip, uint16_t port = DEFAULT_PORT);
    void disconnect();

    // Poll for incoming data (call from recv thread).
    // Complete frames are pushed to the provided queue.
    void poll(ThreadSafeQueue<EncodedPacket>& video_queue,
              ThreadSafeQueue<EncodedPacket>& audio_queue);

    void request_keyframe();
    void send_audio(const EncodedPacket& packet);

    bool is_connected() const { return state_.load() == ConnectionState::Connected; }
    ConnectionState state() const { return state_.load(); }
    const StreamConfig& stream_config() const { return config_; }

private:
    void send_nack(uint16_t frame_id, const std::vector<uint16_t>& missing);
    void handle_ping(const Packet& pkt);

    UdpSocket socket_;
    PacketAssembler assembler_;
    PacketFragmenter fragmenter_;
    uint16_t mic_sequence_ = 0;
    Endpoint server_;
    StreamConfig config_;
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
};

} // namespace lancast
