#pragma once

#include "net/socket.h"
#include "net/protocol.h"
#include "net/packet_fragmenter.h"
#include "net/packet_assembler.h"
#include "core/types.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>

namespace lancast {

class Server {
public:
    explicit Server(uint16_t port = DEFAULT_PORT);
    ~Server();

    bool start();
    void stop();

    // Send an encoded packet to all connected clients
    void broadcast(const EncodedPacket& packet);

    // Send a raw packet to a specific endpoint
    void send_to(const Packet& packet, const Endpoint& dest);

    // Process incoming packets (call from recv thread)
    void poll();

    using ClientAudioCallback = std::function<void(EncodedPacket)>;

    void set_stream_config(const StreamConfig& config) { config_ = config; }
    void set_keyframe_callback(std::function<void()> cb) { keyframe_cb_ = std::move(cb); }
    void set_client_audio_callback(ClientAudioCallback cb) { client_audio_cb_ = std::move(cb); }

    bool is_running() const { return running_.load(); }
    size_t client_count() const;

    // RTT measurement (max across all clients with valid RTT)
    double max_rtt_ms() const;

private:
    struct ClientInfo {
        Endpoint endpoint;
        double rtt_ms = 0.0;
        bool rtt_valid = false;
    };

    struct KeyframeCache {
        uint16_t frame_id = 0;
        std::vector<Packet> fragments;
    };

    void handle_hello(const Packet& pkt, const Endpoint& source);
    void handle_pong(const Packet& pkt, const Endpoint& source);
    void handle_nack(const Packet& pkt, const Endpoint& source);
    void send_stream_config(const Endpoint& dest);
    void send_pings();

    uint16_t port_;
    UdpSocket socket_;
    PacketFragmenter fragmenter_;
    uint16_t sequence_ = 0;

    mutable std::mutex clients_mutex_;
    std::vector<ClientInfo> clients_;

    std::atomic<bool> running_{false};
    StreamConfig config_;
    std::function<void()> keyframe_cb_;
    ClientAudioCallback client_audio_cb_;
    PacketAssembler client_audio_assembler_;

    // Keyframe NACK retransmission cache
    std::mutex keyframe_mutex_;
    KeyframeCache last_keyframe_;

    // PING/PONG timing
    static constexpr auto PING_INTERVAL = std::chrono::seconds(2);
    std::chrono::steady_clock::time_point last_ping_time_;
};

} // namespace lancast
