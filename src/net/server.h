#pragma once

#include "net/socket.h"
#include "net/protocol.h"
#include "net/packet_fragmenter.h"
#include "core/types.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

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

    void set_stream_config(const StreamConfig& config) { config_ = config; }
    void set_keyframe_callback(std::function<void()> cb) { keyframe_cb_ = std::move(cb); }

    bool is_running() const { return running_.load(); }
    size_t client_count() const;

private:
    void handle_hello(const Packet& pkt, const Endpoint& source);
    void send_stream_config(const Endpoint& dest);

    uint16_t port_;
    UdpSocket socket_;
    PacketFragmenter fragmenter_;
    uint16_t sequence_ = 0;

    std::mutex clients_mutex_;
    std::vector<Endpoint> clients_;

    std::atomic<bool> running_{false};
    StreamConfig config_;
    std::function<void()> keyframe_cb_;
};

} // namespace lancast
