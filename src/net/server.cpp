#include "net/server.h"
#include "core/logger.h"
#include <algorithm>

namespace lancast {

static constexpr const char* TAG = "Server";

Server::Server(uint16_t port) : port_(port) {}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (!socket_.is_valid()) {
        LOG_ERROR(TAG, "Invalid socket");
        return false;
    }

    if (!socket_.bind(port_)) return false;
    socket_.set_recv_timeout(100);
    socket_.set_recv_buffer(2 * 1024 * 1024);
    socket_.set_send_buffer(2 * 1024 * 1024);

    last_ping_time_ = std::chrono::steady_clock::now();
    running_ = true;
    LOG_INFO(TAG, "Server started on port %u", port_);
    return true;
}

void Server::stop() {
    running_ = false;
    LOG_INFO(TAG, "Server stopped");
}

void Server::broadcast(const EncodedPacket& packet) {
    auto fragments = fragmenter_.fragment(packet, sequence_);

    // Cache keyframe fragments for NACK retransmission
    if (packet.type == FrameType::VideoKeyframe) {
        std::lock_guard lock(keyframe_mutex_);
        last_keyframe_.frame_id = packet.frame_id;
        last_keyframe_.fragments = fragments;
    }

    std::lock_guard lock(clients_mutex_);
    for (const auto& client : clients_) {
        for (const auto& frag : fragments) {
            auto data = frag.serialize();
            socket_.send_to(data, client.endpoint);
        }
    }
}

void Server::send_to(const Packet& packet, const Endpoint& dest) {
    auto data = packet.serialize();
    socket_.send_to(data, dest);
}

void Server::poll() {
    // Send periodic PINGs
    auto now = std::chrono::steady_clock::now();
    if (now - last_ping_time_ >= PING_INTERVAL) {
        send_pings();
        last_ping_time_ = now;
    }

    auto result = socket_.recv_from();
    if (!result) return;

    auto packet = Packet::deserialize(result->data.data(), result->data.size());
    if (!packet.header.is_valid()) return;

    auto type = static_cast<PacketType>(packet.header.type);
    switch (type) {
        case PacketType::HELLO:
            handle_hello(packet, result->source);
            break;
        case PacketType::BYE: {
            std::lock_guard lock(clients_mutex_);
            std::erase_if(clients_, [&](const ClientInfo& c) { return c.endpoint == result->source; });
            LOG_INFO(TAG, "Client disconnected: %s:%u", result->source.ip.c_str(), result->source.port);
            break;
        }
        case PacketType::KEYFRAME_REQ:
            LOG_INFO(TAG, "Keyframe requested by %s:%u", result->source.ip.c_str(), result->source.port);
            if (keyframe_cb_) keyframe_cb_();
            break;
        case PacketType::PONG:
            handle_pong(packet, result->source);
            break;
        case PacketType::NACK:
            handle_nack(packet, result->source);
            break;
        default:
            break;
    }
}

size_t Server::client_count() const {
    std::lock_guard lock(clients_mutex_);
    return clients_.size();
}

double Server::max_rtt_ms() const {
    std::lock_guard lock(clients_mutex_);
    double max_rtt = 0.0;
    for (const auto& c : clients_) {
        if (c.rtt_valid && c.rtt_ms > max_rtt) {
            max_rtt = c.rtt_ms;
        }
    }
    return max_rtt;
}

void Server::handle_hello(const Packet& pkt, const Endpoint& source) {
    {
        std::lock_guard lock(clients_mutex_);
        // Check if already connected
        for (const auto& c : clients_) {
            if (c.endpoint == source) return;
        }
        ClientInfo info;
        info.endpoint = source;
        clients_.push_back(info);
    }
    LOG_INFO(TAG, "Client connected: %s:%u", source.ip.c_str(), source.port);

    // Send WELCOME with stream config
    Packet welcome;
    welcome.header.magic = PROTOCOL_MAGIC;
    welcome.header.version = PROTOCOL_VERSION;
    welcome.header.type = static_cast<uint8_t>(PacketType::WELCOME);
    welcome.header.sequence = sequence_++;

    WelcomePayload wp;
    wp.width = config_.width;
    wp.height = config_.height;
    wp.fps = config_.fps;
    wp.video_bitrate = config_.video_bitrate;
    wp.audio_sample_rate = config_.audio_sample_rate;
    wp.audio_channels = config_.audio_channels;

    welcome.payload.resize(sizeof(WelcomePayload));
    std::memcpy(welcome.payload.data(), &wp, sizeof(WelcomePayload));

    send_to(welcome, source);
    send_stream_config(source);
}

void Server::handle_pong(const Packet& pkt, const Endpoint& source) {
    if (pkt.payload.size() < sizeof(PingPayload)) return;

    PingPayload pp;
    std::memcpy(&pp, pkt.payload.data(), sizeof(PingPayload));

    auto now = std::chrono::steady_clock::now();
    auto send_time = std::chrono::steady_clock::time_point(
        std::chrono::microseconds(pp.timestamp_us));
    double rtt = std::chrono::duration<double, std::milli>(now - send_time).count();

    if (rtt < 0 || rtt > 10000) return; // Sanity check

    std::lock_guard lock(clients_mutex_);
    for (auto& c : clients_) {
        if (c.endpoint == source) {
            c.rtt_ms = rtt;
            c.rtt_valid = true;
            LOG_DEBUG(TAG, "RTT to %s:%u = %.1f ms", source.ip.c_str(), source.port, rtt);
            break;
        }
    }
}

void Server::handle_nack(const Packet& pkt, const Endpoint& source) {
    if (pkt.payload.size() < sizeof(NackPayload)) return;

    NackPayload np;
    std::memcpy(&np, pkt.payload.data(), sizeof(NackPayload));

    std::lock_guard lock(keyframe_mutex_);
    if (np.frame_id != last_keyframe_.frame_id) {
        LOG_DEBUG(TAG, "NACK for old keyframe %u (current: %u), ignoring",
                  np.frame_id, last_keyframe_.frame_id);
        return;
    }

    // Parse and resend missing fragments
    size_t offset = sizeof(NackPayload);
    uint16_t resent = 0;
    for (uint16_t i = 0; i < np.num_missing; ++i) {
        if (offset + sizeof(uint16_t) > pkt.payload.size()) break;

        uint16_t frag_idx;
        std::memcpy(&frag_idx, pkt.payload.data() + offset, sizeof(uint16_t));
        offset += sizeof(uint16_t);

        if (frag_idx < last_keyframe_.fragments.size()) {
            auto data = last_keyframe_.fragments[frag_idx].serialize();
            socket_.send_to(data, source);
            resent++;
        }
    }

    LOG_INFO(TAG, "NACK from %s:%u: resent %u/%u fragments for keyframe %u",
             source.ip.c_str(), source.port, resent, np.num_missing, np.frame_id);
}

void Server::send_pings() {
    auto now = std::chrono::steady_clock::now();
    auto timestamp_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());

    Packet ping;
    ping.header.magic = PROTOCOL_MAGIC;
    ping.header.version = PROTOCOL_VERSION;
    ping.header.type = static_cast<uint8_t>(PacketType::PING);
    ping.header.sequence = sequence_++;

    PingPayload pp;
    pp.timestamp_us = timestamp_us;
    ping.payload.resize(sizeof(PingPayload));
    std::memcpy(ping.payload.data(), &pp, sizeof(PingPayload));

    auto data = ping.serialize();

    std::lock_guard lock(clients_mutex_);
    for (const auto& client : clients_) {
        socket_.send_to(data, client.endpoint);
    }
}

void Server::send_stream_config(const Endpoint& dest) {
    if (config_.codec_data.empty()) return;

    Packet pkt;
    pkt.header.magic = PROTOCOL_MAGIC;
    pkt.header.version = PROTOCOL_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::STREAM_CONFIG);
    pkt.header.sequence = sequence_++;
    pkt.payload = config_.codec_data;

    send_to(pkt, dest);
    LOG_INFO(TAG, "Sent STREAM_CONFIG (%zu bytes) to %s:%u",
             config_.codec_data.size(), dest.ip.c_str(), dest.port);
}

} // namespace lancast
