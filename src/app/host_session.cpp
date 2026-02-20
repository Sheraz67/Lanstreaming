#include "app/host_session.h"
#include "core/clock.h"
#include "core/logger.h"

#if defined(LANCAST_PLATFORM_LINUX)
#include "capture/screen_capture_x11.h"
#include "capture/audio_capture_pulse.h"
#elif defined(LANCAST_PLATFORM_MACOS)
#include "capture/screen_capture_mac.h"
#include "capture/audio_capture_mac.h"
#endif

namespace lancast {

static constexpr const char* TAG = "HostSession";

HostSession::~HostSession() {
    stop();
}

bool HostSession::start(uint16_t port, uint32_t fps, uint32_t bitrate,
                         uint32_t width, uint32_t height, unsigned long window_id,
                         std::atomic<bool>& running) {
    running_ = &running;
    fps_ = fps;
    target_bitrate_ = bitrate;
    current_bitrate_ = bitrate;

    // Initialize screen capture (platform-specific)
#if defined(LANCAST_PLATFORM_LINUX)
    capture_ = std::make_unique<ScreenCaptureX11>();
#elif defined(LANCAST_PLATFORM_MACOS)
    capture_ = std::make_unique<ScreenCaptureMac>();
#endif
    if (!capture_->init(width, height, window_id)) {
        LOG_ERROR(TAG, "Failed to initialize screen capture");
        return false;
    }

    uint32_t w = capture_->target_width();
    uint32_t h = capture_->target_height();

    // Initialize encoder
    encoder_ = std::make_unique<VideoEncoder>();
    if (!encoder_->init(w, h, fps, bitrate)) {
        LOG_ERROR(TAG, "Failed to initialize video encoder");
        capture_->shutdown();
        return false;
    }

    // Initialize audio capture (platform-specific)
#if defined(LANCAST_PLATFORM_LINUX)
    audio_capture_ = std::make_unique<AudioCapturePulse>();
#elif defined(LANCAST_PLATFORM_MACOS)
    {
        auto mac_audio = std::make_unique<AudioCaptureMac>();
        auto* mac_capture = dynamic_cast<ScreenCaptureMac*>(capture_.get());
        if (mac_capture) {
            mac_audio->set_stream_manager(mac_capture->stream_manager());
        }
        audio_capture_ = std::move(mac_audio);
    }
#endif
    if (!audio_capture_->init(48000, 2)) {
        LOG_WARN(TAG, "Failed to initialize audio capture — continuing without audio");
        audio_capture_.reset();
    }

    // Initialize audio encoder
    if (audio_capture_) {
        audio_encoder_ = std::make_unique<AudioEncoder>();
        if (!audio_encoder_->init(48000, 2, 128000)) {
            LOG_WARN(TAG, "Failed to initialize audio encoder — continuing without audio");
            audio_capture_->shutdown();
            audio_capture_.reset();
            audio_encoder_.reset();
        }
    }

    // Configure server with target (output) dimensions
    StreamConfig config;
    config.width = w;
    config.height = h;
    config.fps = fps;
    config.video_bitrate = bitrate;
    config.codec_data = encoder_->extradata();

    // Set keyframe callback
    server_ = std::make_unique<Server>(port);
    server_->set_stream_config(config);
    server_->set_keyframe_callback([this]() {
        if (encoder_) encoder_->request_keyframe();
    });

    if (!server_->start()) {
        LOG_ERROR(TAG, "Failed to start server");
        if (audio_encoder_) audio_encoder_->shutdown();
        if (audio_capture_) audio_capture_->shutdown();
        encoder_->shutdown();
        capture_->shutdown();
        return false;
    }

    // Initialize client audio decoder + player (for receiving mic audio from clients)
    client_audio_decoder_ = std::make_unique<AudioDecoder>();
    if (!client_audio_decoder_->init(48000, 2)) {
        LOG_WARN(TAG, "Failed to initialize client audio decoder — continuing without client mic playback");
        client_audio_decoder_.reset();
    }

    if (client_audio_decoder_) {
        client_audio_player_ = std::make_unique<AudioPlayer>();
        if (!client_audio_player_->init(48000, 2)) {
            LOG_WARN(TAG, "Failed to initialize client audio player — continuing without client mic playback");
            client_audio_decoder_->shutdown();
            client_audio_decoder_.reset();
            client_audio_player_.reset();
        }
    }

    // Set callback so server dispatches received client audio to our queue
    if (client_audio_decoder_ && client_audio_player_) {
        server_->set_client_audio_callback([this](EncodedPacket pkt) {
            client_audio_queue_.push(std::move(pkt));
        });
    }

    LOG_INFO(TAG, "Host started: %ux%u @ %u fps, bitrate %u, port %u, audio %s, client mic playback %s",
             w, h, fps, bitrate, port,
             audio_capture_ ? "enabled" : "disabled",
             client_audio_decoder_ ? "enabled" : "disabled");

    last_bitrate_check_ = std::chrono::steady_clock::now();

    // Launch threads
    poll_thread_ = lancast::jthread([this](lancast::stop_token st) { server_poll_loop(st); });
    send_thread_ = lancast::jthread([this](lancast::stop_token st) { network_send_loop(st); });
    encode_thread_ = lancast::jthread([this](lancast::stop_token st) { encode_loop(st); });
    capture_thread_ = lancast::jthread([this](lancast::stop_token st) { capture_loop(st); });

    if (audio_capture_ && audio_encoder_) {
        audio_encode_thread_ = lancast::jthread([this](lancast::stop_token st) { audio_encode_loop(st); });
        audio_capture_thread_ = lancast::jthread([this](lancast::stop_token st) { audio_capture_loop(st); });
    }

    if (client_audio_decoder_ && client_audio_player_) {
        client_audio_decode_thread_ = lancast::jthread([this](lancast::stop_token st) { client_audio_decode_loop(st); });
    }

    return true;
}

void HostSession::stop() {
    if (client_audio_decode_thread_.joinable()) client_audio_decode_thread_.request_stop();
    if (audio_capture_thread_.joinable()) audio_capture_thread_.request_stop();
    if (audio_encode_thread_.joinable()) audio_encode_thread_.request_stop();
    if (capture_thread_.joinable()) capture_thread_.request_stop();
    if (encode_thread_.joinable()) encode_thread_.request_stop();
    if (send_thread_.joinable()) send_thread_.request_stop();
    if (poll_thread_.joinable()) poll_thread_.request_stop();

    client_audio_queue_.close();
    audio_raw_queue_.close();
    audio_encoded_queue_.close();

    if (client_audio_decode_thread_.joinable()) client_audio_decode_thread_.join();
    if (audio_capture_thread_.joinable()) audio_capture_thread_.join();
    if (audio_encode_thread_.joinable()) audio_encode_thread_.join();
    if (capture_thread_.joinable()) capture_thread_.join();
    if (encode_thread_.joinable()) encode_thread_.join();
    if (send_thread_.joinable()) send_thread_.join();
    if (poll_thread_.joinable()) poll_thread_.join();

    if (server_) server_->stop();
    if (client_audio_player_) client_audio_player_->shutdown();
    if (client_audio_decoder_) client_audio_decoder_->shutdown();
    if (audio_encoder_) audio_encoder_->shutdown();
    if (audio_capture_) audio_capture_->shutdown();
    if (encoder_) encoder_->shutdown();
    if (capture_) capture_->shutdown();

    LOG_INFO(TAG, "Host session stopped");
}

void HostSession::capture_loop(lancast::stop_token st) {
    Clock clock;
    auto frame_interval = std::chrono::microseconds(1'000'000 / fps_);

    LOG_INFO(TAG, "Capture loop started (%u fps, interval %lld us)",
             fps_, static_cast<long long>(frame_interval.count()));

    while (!st.stop_requested() && running_->load()) {
        auto start = std::chrono::steady_clock::now();

        auto raw_frame = capture_->capture_frame();
        if (raw_frame) {
            if (!raw_buffer_.try_push(std::move(*raw_frame))) {
                LOG_DEBUG(TAG, "Raw buffer full, dropping frame");
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

void HostSession::encode_loop(lancast::stop_token st) {
    LOG_INFO(TAG, "Encode loop started");

    while (!st.stop_requested() && running_->load()) {
        auto raw_frame = raw_buffer_.try_pop();
        if (raw_frame) {
            auto encoded = encoder_->encode(*raw_frame);
            if (encoded) {
                if (!encoded_buffer_.try_push(std::move(*encoded))) {
                    LOG_DEBUG(TAG, "Encoded buffer full, dropping frame");
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    LOG_INFO(TAG, "Encode loop ended");
}

void HostSession::network_send_loop(lancast::stop_token st) {
    LOG_INFO(TAG, "Network send loop started");

    while (!st.stop_requested() && running_->load()) {
        bool sent_anything = false;

        // Check video encoded buffer
        auto video_packet = encoded_buffer_.try_pop();
        if (video_packet) {
            server_->broadcast(*video_packet);
            LOG_DEBUG(TAG, "Broadcast video frame %u (%zu bytes, %s)",
                      video_packet->frame_id, video_packet->data.size(),
                      video_packet->type == FrameType::VideoKeyframe ? "keyframe" : "P-frame");
            sent_anything = true;
        }

        // Check audio encoded queue
        auto audio_packet = audio_encoded_queue_.try_pop();
        if (audio_packet) {
            server_->broadcast(*audio_packet);
            LOG_DEBUG(TAG, "Broadcast audio frame %u (%zu bytes)",
                      audio_packet->frame_id, audio_packet->data.size());
            sent_anything = true;
        }

        if (!sent_anything) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    LOG_INFO(TAG, "Network send loop ended");
}

void HostSession::server_poll_loop(lancast::stop_token st) {
    LOG_INFO(TAG, "Server poll loop started");

    while (!st.stop_requested() && running_->load()) {
        server_->poll();

        // Periodically check adaptive bitrate
        check_adaptive_bitrate();
    }

    LOG_INFO(TAG, "Server poll loop ended");
}

void HostSession::check_adaptive_bitrate() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_bitrate_check_ < std::chrono::seconds(5)) return;
    last_bitrate_check_ = now;

    if (server_->client_count() == 0) return;

    double rtt = server_->max_rtt_ms();
    if (rtt <= 0) return; // No valid RTT measurements yet

    uint32_t desired_bitrate;
    if (rtt > 100.0) {
        desired_bitrate = target_bitrate_ / 2;
    } else if (rtt > 50.0) {
        desired_bitrate = target_bitrate_ * 3 / 4;
    } else {
        desired_bitrate = target_bitrate_;
    }

    if (desired_bitrate != current_bitrate_) {
        LOG_INFO(TAG, "Adaptive bitrate: RTT=%.1f ms, adjusting %u -> %u",
                 rtt, current_bitrate_, desired_bitrate);
        if (encoder_->set_bitrate(desired_bitrate)) {
            current_bitrate_ = desired_bitrate;
        }
    }
}

void HostSession::audio_capture_loop(lancast::stop_token st) {
    LOG_INFO(TAG, "Audio capture loop started");

    while (!st.stop_requested() && running_->load()) {
        auto frame = audio_capture_->capture_frame();
        if (frame) {
            audio_raw_queue_.push(std::move(*frame));
        }
    }

    LOG_INFO(TAG, "Audio capture loop ended");
}

void HostSession::audio_encode_loop(lancast::stop_token st) {
    LOG_INFO(TAG, "Audio encode loop started");

    while (!st.stop_requested() && running_->load()) {
        auto frame = audio_raw_queue_.wait_pop(std::chrono::milliseconds(50));
        if (frame) {
            auto encoded = audio_encoder_->encode(*frame);
            if (encoded) {
                audio_encoded_queue_.push(std::move(*encoded));
            }
        }
    }

    LOG_INFO(TAG, "Audio encode loop ended");
}

void HostSession::client_audio_decode_loop(lancast::stop_token st) {
    LOG_INFO(TAG, "Client audio decode loop started");

    while (!st.stop_requested() && running_->load()) {
        auto packet = client_audio_queue_.wait_pop(std::chrono::milliseconds(50));
        if (packet) {
            auto decoded = client_audio_decoder_->decode(*packet);
            if (decoded) {
                client_audio_player_->play_frame(*decoded);
            }
        }
    }

    LOG_INFO(TAG, "Client audio decode loop ended");
}

} // namespace lancast
