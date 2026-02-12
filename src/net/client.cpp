#include "net/client.h"
#include "core/logger.h"

namespace lancast {

static constexpr const char* TAG = "Client";

Client::Client() = default;
Client::~Client() { disconnect(); }

bool Client::connect(const std::string& host_ip, uint16_t port) {
    if (!socket_.is_valid()) {
        LOG_ERROR(TAG, "Invalid socket");
        return false;
    }

    // Bind to any available port
    socket_.set_recv_timeout(1000);
    socket_.set_recv_buffer(2 * 1024 * 1024);

    server_ = {host_ip, port};

    // Send HELLO
    Packet hello;
    hello.header.magic = PROTOCOL_MAGIC;
    hello.header.version = PROTOCOL_VERSION;
    hello.header.type = static_cast<uint8_t>(PacketType::HELLO);
    hello.header.sequence = 0;

    auto data = hello.serialize();
    socket_.send_to(data, server_);
    LOG_INFO(TAG, "Sent HELLO to %s:%u", host_ip.c_str(), port);

    // Wait for WELCOME
    auto result = socket_.recv_from();
    if (!result) {
        LOG_ERROR(TAG, "No WELCOME received (timeout)");
        return false;
    }

    auto pkt = Packet::deserialize(result->data.data(), result->data.size());
    if (!pkt.header.is_valid() ||
        static_cast<PacketType>(pkt.header.type) != PacketType::WELCOME) {
        LOG_ERROR(TAG, "Expected WELCOME, got type 0x%02x", pkt.header.type);
        return false;
    }

    if (pkt.payload.size() >= sizeof(WelcomePayload)) {
        WelcomePayload wp;
        std::memcpy(&wp, pkt.payload.data(), sizeof(WelcomePayload));
        config_.width = wp.width;
        config_.height = wp.height;
        config_.fps = wp.fps;
        config_.video_bitrate = wp.video_bitrate;
        config_.audio_sample_rate = wp.audio_sample_rate;
        config_.audio_channels = wp.audio_channels;
    }

    // Wait for STREAM_CONFIG packet (codec extradata / SPS/PPS)
    auto config_result = socket_.recv_from();
    if (config_result) {
        auto config_pkt = Packet::deserialize(config_result->data.data(), config_result->data.size());
        if (config_pkt.header.is_valid() &&
            static_cast<PacketType>(config_pkt.header.type) == PacketType::STREAM_CONFIG) {
            config_.codec_data = std::move(config_pkt.payload);
            LOG_INFO(TAG, "Received STREAM_CONFIG: %zu bytes codec data", config_.codec_data.size());
        }
    }

    // Switch to shorter timeout for streaming
    socket_.set_recv_timeout(50);
    connected_ = true;
    LOG_INFO(TAG, "Connected to %s:%u (%ux%u@%u)", host_ip.c_str(), port,
             config_.width, config_.height, config_.fps);
    return true;
}

void Client::disconnect() {
    if (!connected_) return;

    Packet bye;
    bye.header.magic = PROTOCOL_MAGIC;
    bye.header.version = PROTOCOL_VERSION;
    bye.header.type = static_cast<uint8_t>(PacketType::BYE);

    auto data = bye.serialize();
    socket_.send_to(data, server_);

    connected_ = false;
    LOG_INFO(TAG, "Disconnected");
}

void Client::poll(ThreadSafeQueue<EncodedPacket>& video_queue,
                  ThreadSafeQueue<EncodedPacket>& audio_queue) {
    auto result = socket_.recv_from();
    if (!result) return;

    auto pkt = Packet::deserialize(result->data.data(), result->data.size());
    if (!pkt.header.is_valid()) return;

    auto type = static_cast<PacketType>(pkt.header.type);

    if (type == PacketType::VIDEO_DATA || type == PacketType::AUDIO_DATA) {
        auto frame = assembler_.feed(pkt);
        if (frame) {
            if (frame->type == FrameType::Audio) {
                audio_queue.push(std::move(*frame));
            } else {
                video_queue.push(std::move(*frame));
            }
        }
    }

    // Periodically purge stale incomplete frames
    assembler_.purge_stale();
}

void Client::request_keyframe() {
    Packet req;
    req.header.magic = PROTOCOL_MAGIC;
    req.header.version = PROTOCOL_VERSION;
    req.header.type = static_cast<uint8_t>(PacketType::KEYFRAME_REQ);

    auto data = req.serialize();
    socket_.send_to(data, server_);
}

} // namespace lancast
