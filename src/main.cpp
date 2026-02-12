#include "core/logger.h"
#include "core/types.h"
#include "core/clock.h"
#include "core/thread_safe_queue.h"
#include "net/server.h"
#include "net/client.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

using namespace lancast;

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --host [--port PORT]           Start as host\n", prog);
    fprintf(stderr, "  %s --client IP [--port PORT]      Connect to host\n", prog);
}

static void run_host(uint16_t port) {
    Server server(port);
    if (!server.start()) {
        LOG_ERROR("Main", "Failed to start server");
        return;
    }

    LOG_INFO("Main", "Host mode: listening on port %u (Ctrl+C to stop)", port);
    LOG_INFO("Main", "Sending synthetic test frames to connected clients...");

    Clock clock;
    uint16_t frame_id = 0;

    // Recv thread
    std::jthread recv_thread([&](std::stop_token st) {
        while (!st.stop_requested() && g_running) {
            server.poll();
        }
    });

    // Send synthetic test data
    while (g_running) {
        if (server.client_count() > 0) {
            // Create a synthetic test frame (a simple pattern)
            EncodedPacket test_frame;
            test_frame.frame_id = frame_id++;
            test_frame.pts_us = clock.now_us();
            test_frame.type = (frame_id % 60 == 0) ? FrameType::VideoKeyframe : FrameType::VideoPFrame;

            // Fill with identifiable test pattern
            size_t size = 5000; // ~5KB per frame
            test_frame.data.resize(size);
            for (size_t i = 0; i < size; ++i) {
                test_frame.data[i] = static_cast<uint8_t>((frame_id + i) & 0xFF);
            }

            server.broadcast(test_frame);
            LOG_DEBUG("Main", "Sent frame %u (%zu bytes, %s)", frame_id - 1, size,
                      test_frame.type == FrameType::VideoKeyframe ? "keyframe" : "P-frame");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps
    }

    server.stop();
}

static void run_client(const std::string& host_ip, uint16_t port) {
    Client client;

    LOG_INFO("Main", "Connecting to %s:%u...", host_ip.c_str(), port);
    if (!client.connect(host_ip, port)) {
        LOG_ERROR("Main", "Failed to connect");
        return;
    }

    LOG_INFO("Main", "Connected! Receiving stream...");

    ThreadSafeQueue<EncodedPacket> video_queue(30);
    ThreadSafeQueue<EncodedPacket> audio_queue(60);

    uint32_t frames_received = 0;

    while (g_running && client.is_connected()) {
        client.poll(video_queue, audio_queue);

        // Drain received frames (just log them for Phase 1)
        while (auto frame = video_queue.try_pop()) {
            frames_received++;
            if (frames_received % 30 == 0) {
                LOG_INFO("Main", "Received %u frames (%zu bytes, %s)",
                         frames_received, frame->data.size(),
                         frame->type == FrameType::VideoKeyframe ? "keyframe" : "P-frame");
            }
        }
    }

    client.disconnect();
    LOG_INFO("Main", "Total frames received: %u", frames_received);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    bool host_mode = false;
    std::string client_ip;
    uint16_t port = DEFAULT_PORT;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0) {
            host_mode = true;
        } else if (strcmp(argv[i], "--client") == 0 && i + 1 < argc) {
            client_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            Logger::set_level(LogLevel::Debug);
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!host_mode && client_ip.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    if (host_mode) {
        run_host(port);
    } else {
        run_client(client_ip, port);
    }

    return 0;
}
