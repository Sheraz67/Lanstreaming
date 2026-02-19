#pragma once

#include "net/client.h"
#include "decode/video_decoder.h"
#include "decode/audio_decoder.h"
#include "render/sdl_renderer.h"
#include "render/audio_player.h"
#include "core/thread_safe_queue.h"
#include "core/types.h"
#include "core/jthread.h"
#include <atomic>
#include <memory>
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
    void recv_loop(lancast::stop_token st);
    void decode_loop(lancast::stop_token st);
    void audio_decode_loop(lancast::stop_token st);

    Client client_;
    std::unique_ptr<VideoDecoder> decoder_;
    std::unique_ptr<AudioDecoder> audio_decoder_;
    std::unique_ptr<AudioPlayer> audio_player_;
    SdlRenderer renderer_;

    ThreadSafeQueue<EncodedPacket> video_queue_{30};
    ThreadSafeQueue<EncodedPacket> audio_queue_{60};
    ThreadSafeQueue<RawVideoFrame> decoded_queue_{4};

    std::atomic<bool>* running_ = nullptr;
    lancast::jthread recv_thread_;
    lancast::jthread decode_thread_;
    lancast::jthread audio_decode_thread_;
};

} // namespace lancast
