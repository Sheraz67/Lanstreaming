#pragma once

#include "capture/capture_source.h"
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

    bool start(uint16_t port, uint32_t fps, std::atomic<bool>& running);
    void stop();
    bool is_running() const { return running_ != nullptr && running_->load(); }

private:
    void capture_loop(std::stop_token st);
    void network_send_loop(std::stop_token st);
    void server_poll_loop(std::stop_token st);

    std::unique_ptr<ICaptureSource> capture_;
    std::unique_ptr<Server> server_;
    RingBuffer<EncodedPacket, 4> frame_buffer_;

    std::atomic<bool>* running_ = nullptr;
    uint32_t fps_ = 10;
    uint16_t frame_id_ = 0;

    std::jthread capture_thread_;
    std::jthread send_thread_;
    std::jthread poll_thread_;
};

} // namespace lancast
