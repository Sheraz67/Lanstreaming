#pragma once

#include "capture/capture_source.h"
#include "capture/audio_capture_pulse.h"
#include "encode/video_encoder.h"
#include "encode/audio_encoder.h"
#include "net/server.h"
#include "core/ring_buffer.h"
#include "core/thread_safe_queue.h"
#include "core/types.h"
#include <atomic>
#include <memory>
#include <thread>

namespace lancast {

class HostSession {
public:
    HostSession() = default;
    ~HostSession();

    bool start(uint16_t port, uint32_t fps, uint32_t bitrate,
               uint32_t width, uint32_t height, std::atomic<bool>& running);
    void stop();
    bool is_running() const { return running_ != nullptr && running_->load(); }

private:
    void capture_loop(std::stop_token st);
    void encode_loop(std::stop_token st);
    void network_send_loop(std::stop_token st);
    void server_poll_loop(std::stop_token st);
    void audio_capture_loop(std::stop_token st);
    void audio_encode_loop(std::stop_token st);

    void check_adaptive_bitrate();

    std::unique_ptr<ICaptureSource> capture_;
    std::unique_ptr<VideoEncoder> encoder_;
    std::unique_ptr<AudioCapturePulse> audio_capture_;
    std::unique_ptr<AudioEncoder> audio_encoder_;
    std::unique_ptr<Server> server_;

    RingBuffer<RawVideoFrame, 4> raw_buffer_;       // capture -> encode
    RingBuffer<EncodedPacket, 4> encoded_buffer_;    // encode -> send

    ThreadSafeQueue<RawAudioFrame> audio_raw_queue_{8};       // audio capture -> encode
    ThreadSafeQueue<EncodedPacket> audio_encoded_queue_{16};   // audio encode -> send

    std::atomic<bool>* running_ = nullptr;
    uint32_t fps_ = 30;
    uint32_t target_bitrate_ = 6000000;
    uint32_t current_bitrate_ = 6000000;

    std::jthread capture_thread_;
    std::jthread encode_thread_;
    std::jthread send_thread_;
    std::jthread poll_thread_;
    std::jthread audio_capture_thread_;
    std::jthread audio_encode_thread_;

    // Adaptive bitrate timing
    std::chrono::steady_clock::time_point last_bitrate_check_;
};

} // namespace lancast
