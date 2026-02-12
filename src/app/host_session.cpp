#include "app/host_session.h"
#include "capture/screen_capture_x11.h"
#include "core/clock.h"
#include "core/logger.h"

namespace lancast {

static constexpr const char* TAG = "HostSession";

HostSession::~HostSession() {
    stop();
}

bool HostSession::start(uint16_t port, uint32_t fps, std::atomic<bool>& running) {
    running_ = &running;
    fps_ = fps;

    // Initialize screen capture
    capture_ = std::make_unique<ScreenCaptureX11>();
    if (!capture_->init(1920, 1080)) {
        LOG_ERROR(TAG, "Failed to initialize screen capture");
        return false;
    }

    // Configure server with actual capture dimensions
    StreamConfig config;
    config.width = capture_->native_width();
    config.height = capture_->native_height();
    config.fps = fps;

    server_ = std::make_unique<Server>(port);
    server_->set_stream_config(config);

    if (!server_->start()) {
        LOG_ERROR(TAG, "Failed to start server");
        capture_->shutdown();
        return false;
    }

    LOG_INFO(TAG, "Host started: %ux%u @ %u fps, port %u",
             config.width, config.height, fps, port);

    // Launch threads
    poll_thread_ = std::jthread([this](std::stop_token st) { server_poll_loop(st); });
    send_thread_ = std::jthread([this](std::stop_token st) { network_send_loop(st); });
    capture_thread_ = std::jthread([this](std::stop_token st) { capture_loop(st); });

    return true;
}

void HostSession::stop() {
    if (capture_thread_.joinable()) capture_thread_.request_stop();
    if (send_thread_.joinable()) send_thread_.request_stop();
    if (poll_thread_.joinable()) poll_thread_.request_stop();

    if (capture_thread_.joinable()) capture_thread_.join();
    if (send_thread_.joinable()) send_thread_.join();
    if (poll_thread_.joinable()) poll_thread_.join();

    if (server_) server_->stop();
    if (capture_) capture_->shutdown();

    LOG_INFO(TAG, "Host session stopped");
}

void HostSession::capture_loop(std::stop_token st) {
    Clock clock;
    auto frame_interval = std::chrono::microseconds(1'000'000 / fps_);

    LOG_INFO(TAG, "Capture loop started (%u fps, interval %lld us)",
             fps_, static_cast<long long>(frame_interval.count()));

    while (!st.stop_requested() && running_->load()) {
        auto start = std::chrono::steady_clock::now();

        auto raw_frame = capture_->capture_frame();
        if (raw_frame) {
            // Wrap raw YUV data in an EncodedPacket for transport
            EncodedPacket packet;
            packet.data = std::move(raw_frame->data);
            packet.type = FrameType::VideoKeyframe; // Raw frames are always keyframes
            packet.pts_us = clock.now_us();
            packet.frame_id = frame_id_++;

            if (!frame_buffer_.try_push(std::move(packet))) {
                LOG_DEBUG(TAG, "Frame buffer full, dropping frame");
            }
        }

        // Sleep to maintain target FPS
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto sleep_time = frame_interval - elapsed;
        if (sleep_time > std::chrono::microseconds(0)) {
            std::this_thread::sleep_for(sleep_time);
        }
    }

    LOG_INFO(TAG, "Capture loop ended");
}

void HostSession::network_send_loop(std::stop_token st) {
    LOG_INFO(TAG, "Network send loop started");

    while (!st.stop_requested() && running_->load()) {
        auto packet = frame_buffer_.try_pop();
        if (packet) {
            server_->broadcast(*packet);
            LOG_DEBUG(TAG, "Broadcast frame %u (%zu bytes)",
                      packet->frame_id, packet->data.size());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    LOG_INFO(TAG, "Network send loop ended");
}

void HostSession::server_poll_loop(std::stop_token st) {
    LOG_INFO(TAG, "Server poll loop started");

    while (!st.stop_requested() && running_->load()) {
        server_->poll();
    }

    LOG_INFO(TAG, "Server poll loop ended");
}

} // namespace lancast
