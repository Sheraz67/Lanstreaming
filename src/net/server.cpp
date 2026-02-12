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

    std::lock_guard lock(clients_mutex_);
    for (const auto& client : clients_) {
        for (const auto& frag : fragments) {
            auto data = frag.serialize();
            socket_.send_to(data, client);
        }
    }
}

void Server::send_to(const Packet& packet, const Endpoint& dest) {
    auto data = packet.serialize();
    socket_.send_to(data, dest);
}

void Server::poll() {
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
            std::erase_if(clients_, [&](const Endpoint& e) { return e == result->source; });
            LOG_INFO(TAG, "Client disconnected: %s:%u", result->source.ip.c_str(), result->source.port);
            break;
        }
        case PacketType::KEYFRAME_REQ:
            LOG_INFO(TAG, "Keyframe requested by %s:%u", result->source.ip.c_str(), result->source.port);
            if (keyframe_cb_) keyframe_cb_();
            break;
        default:
            break;
    }
}

size_t Server::client_count() const {
    std::lock_guard lock(const_cast<std::mutex&>(clients_mutex_));
    return clients_.size();
}

void Server::handle_hello(const Packet& pkt, const Endpoint& source) {
    {
        std::lock_guard lock(clients_mutex_);
        // Check if already connected
        for (const auto& c : clients_) {
            if (c == source) return;
        }
        clients_.push_back(source);
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
