#include "core/logger.h"
#include "net/protocol.h"
#include "app/host_session.h"
#include "app/client_session.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
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
    fprintf(stderr, "  %s --host [--port PORT] [--fps FPS]   Start as host\n", prog);
    fprintf(stderr, "  %s --client IP [--port PORT]          Connect to host\n", prog);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    bool host_mode = false;
    std::string client_ip;
    uint16_t port = DEFAULT_PORT;
    uint32_t fps = 10;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0) {
            host_mode = true;
        } else if (strcmp(argv[i], "--client") == 0 && i + 1 < argc) {
            client_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = static_cast<uint32_t>(atoi(argv[++i]));
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
        HostSession session;
        if (!session.start(port, fps, g_running)) {
            return 1;
        }

        LOG_INFO("Main", "Host running (Ctrl+C to stop)");
        while (g_running && session.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        session.stop();
    } else {
        ClientSession session;
        if (!session.connect(client_ip, port)) {
            return 1;
        }

        session.run(g_running); // Blocks on main thread (SDL event loop)
        session.stop();
    }

    return 0;
}
