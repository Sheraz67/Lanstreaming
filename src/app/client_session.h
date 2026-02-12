#pragma once

#include "net/client.h"
#include "render/sdl_renderer.h"
#include "core/thread_safe_queue.h"
#include "core/types.h"
#include <atomic>
#include <thread>
#include <string>

namespace lancast {

class ClientSession {
public:
    ClientSession() = default;
    ~ClientSession();

    bool connect(const std::string& host_ip, uint16_t port);

    // Runs the SDL render loop on the main thread. Blocks until quit.
    void run(std::atomic<bool>& running);

    void stop();

private:
    void recv_loop(std::stop_token st);

    Client client_;
    SdlRenderer renderer_;
    ThreadSafeQueue<EncodedPacket> video_queue_{30};
    ThreadSafeQueue<EncodedPacket> audio_queue_{60};

    std::atomic<bool>* running_ = nullptr;
    std::jthread recv_thread_;
};

} // namespace lancast
