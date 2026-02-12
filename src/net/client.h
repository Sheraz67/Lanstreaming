#pragma once

#include "net/socket.h"
#include "net/protocol.h"
#include "net/packet_assembler.h"
#include "core/types.h"
#include "core/thread_safe_queue.h"
#include <atomic>
#include <functional>

namespace lancast {

class Client {
public:
    Client();
    ~Client();

    bool connect(const std::string& host_ip, uint16_t port = DEFAULT_PORT);
    void disconnect();

    // Poll for incoming data (call from recv thread).
    // Complete frames are pushed to the provided queue.
    void poll(ThreadSafeQueue<EncodedPacket>& video_queue,
              ThreadSafeQueue<EncodedPacket>& audio_queue);

    void request_keyframe();

    bool is_connected() const { return connected_.load(); }
    const StreamConfig& stream_config() const { return config_; }

private:
    UdpSocket socket_;
    PacketAssembler assembler_;
    Endpoint server_;
    StreamConfig config_;
    std::atomic<bool> connected_{false};
};

} // namespace lancast
