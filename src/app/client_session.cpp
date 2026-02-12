#include "app/client_session.h"
#include "core/logger.h"

#include <chrono>

namespace lancast {

static constexpr const char* TAG = "ClientSession";

ClientSession::~ClientSession() {
    stop();
}

bool ClientSession::connect(const std::string& host_ip, uint16_t port) {
    LOG_INFO(TAG, "Connecting to %s:%u...", host_ip.c_str(), port);

    if (!client_.connect(host_ip, port)) {
        LOG_ERROR(TAG, "Failed to connect to server");
        return false;
    }

    const auto& config = client_.stream_config();
    LOG_INFO(TAG, "Connected: stream %ux%u @ %u fps, codec_data %zu bytes",
             config.width, config.height, config.fps, config.codec_data.size());

    return true;
}

void ClientSession::run(std::atomic<bool>& running) {
    running_ = &running;

    const auto& config = client_.stream_config();

    // Initialize decoder
    decoder_ = std::make_unique<VideoDecoder>();
    if (!decoder_->init(config.width, config.height, config.codec_data)) {
        LOG_ERROR(TAG, "Failed to initialize video decoder");
        return;
    }

    // Initialize SDL renderer
    if (!renderer_.init(config.width, config.height, "lancast - viewer")) {
        LOG_ERROR(TAG, "Failed to initialize SDL renderer");
        return;
    }

    // Start receive and decode threads
    recv_thread_ = std::jthread([this](std::stop_token st) { recv_loop(st); });
    decode_thread_ = std::jthread([this](std::stop_token st) { decode_loop(st); });

    // Request a keyframe so we start cleanly
    client_.request_keyframe();

    LOG_INFO(TAG, "Render loop started");

    uint32_t frames_rendered = 0;

    // Main thread render loop
    while (running_->load() && client_.is_connected()) {
        // Poll SDL events (returns false on quit/ESC)
        if (!renderer_.poll_events()) {
            running_->store(false);
            break;
        }

        // Try to get a decoded video frame
        auto frame = decoded_queue_.try_pop();
        if (frame) {
            renderer_.render_frame(*frame);
            frames_rendered++;

            if (frames_rendered % 30 == 0) {
                LOG_INFO(TAG, "Rendered %u frames", frames_rendered);
            }
        } else {
            // No frame available, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    LOG_INFO(TAG, "Render loop ended (total frames: %u)", frames_rendered);
}

void ClientSession::stop() {
    if (recv_thread_.joinable()) {
        recv_thread_.request_stop();
        recv_thread_.join();
    }
    if (decode_thread_.joinable()) {
        decode_thread_.request_stop();
        decode_thread_.join();
    }

    video_queue_.close();
    audio_queue_.close();
    decoded_queue_.close();

    if (decoder_) decoder_->shutdown();
    renderer_.shutdown();
    client_.disconnect();

    LOG_INFO(TAG, "Client session stopped");
}

void ClientSession::recv_loop(std::stop_token st) {
    LOG_INFO(TAG, "Receive loop started");

    while (!st.stop_requested() && running_->load() && client_.is_connected()) {
        client_.poll(video_queue_, audio_queue_);
    }

    LOG_INFO(TAG, "Receive loop ended");
}

void ClientSession::decode_loop(std::stop_token st) {
    LOG_INFO(TAG, "Decode loop started");

    while (!st.stop_requested() && running_->load()) {
        auto packet = video_queue_.wait_pop(std::chrono::milliseconds(50));
        if (packet) {
            auto decoded = decoder_->decode(*packet);
            if (decoded) {
                decoded_queue_.push(std::move(*decoded));
            }
        }
    }

    LOG_INFO(TAG, "Decode loop ended");
}

} // namespace lancast
