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
    LOG_INFO(TAG, "Connected: stream %ux%u @ %u fps",
             config.width, config.height, config.fps);

    return true;
}

void ClientSession::run(std::atomic<bool>& running) {
    running_ = &running;

    const auto& config = client_.stream_config();

    // Initialize SDL renderer
    if (!renderer_.init(config.width, config.height, "lancast - viewer")) {
        LOG_ERROR(TAG, "Failed to initialize SDL renderer");
        return;
    }

    // Start receive thread
    recv_thread_ = std::jthread([this](std::stop_token st) { recv_loop(st); });

    LOG_INFO(TAG, "Render loop started");

    uint32_t frames_rendered = 0;

    // Main thread render loop
    while (running_->load() && client_.is_connected()) {
        // Poll SDL events (returns false on quit/ESC)
        if (!renderer_.poll_events()) {
            running_->store(false);
            break;
        }

        // Try to get a video frame
        auto frame = video_queue_.try_pop();
        if (frame) {
            // Interpret EncodedPacket.data as raw YUV420p
            RawVideoFrame raw;
            raw.data = std::move(frame->data);
            raw.width = config.width;
            raw.height = config.height;
            raw.pts_us = frame->pts_us;

            renderer_.render_frame(raw);
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

    video_queue_.close();
    audio_queue_.close();

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

} // namespace lancast
