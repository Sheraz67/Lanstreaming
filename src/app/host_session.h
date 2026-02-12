#pragma once

#include "capture/capture_source.h"
#include "encode/video_encoder.h"
#include "net/server.h"
#include "core/ring_buffer.h"
#include "core/types.h"
#include <atomic>
#include <memory>
#include <thread>

namespace lancast {

class HostSession {
public:
    HostSession() = default;
    ~HostSession();

    bool start(uint16_t port, uint32_t fps, uint32_t bitrate, std::atomic<bool>& running);
    void stop();
    bool is_running() const { return running_ != nullptr && running_->load(); }

private:
    void capture_loop(std::stop_token st);
    void encode_loop(std::stop_token st);
    void network_send_loop(std::stop_token st);
    void server_poll_loop(std::stop_token st);

    std::unique_ptr<ICaptureSource> capture_;
    std::unique_ptr<VideoEncoder> encoder_;
    std::unique_ptr<Server> server_;

    RingBuffer<RawVideoFrame, 4> raw_buffer_;       // capture -> encode
    RingBuffer<EncodedPacket, 4> encoded_buffer_;    // encode -> send

    std::atomic<bool>* running_ = nullptr;
    uint32_t fps_ = 30;

    std::jthread capture_thread_;
    std::jthread encode_thread_;
    std::jthread send_thread_;
    std::jthread poll_thread_;
};

} // namespace lancast
