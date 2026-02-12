#include "core/logger.h"
#include "net/protocol.h"
#include "app/host_session.h"
#include "app/client_session.h"
#include "app/launcher_ui.h"
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
    fprintf(stderr, "  %s                                                        Launch UI\n", prog);
    fprintf(stderr, "  %s --host [--port PORT] [--fps FPS] [--bitrate BITRATE]   Start as host\n", prog);
    fprintf(stderr, "             [--resolution WxH]\n");
    fprintf(stderr, "  %s --client IP [--port PORT]                              Connect to host\n", prog);
}

static bool parse_resolution(const char* str, uint32_t& w, uint32_t& h) {
    // Parse "WxH" or "WXH" format
    const char* x = strchr(str, 'x');
    if (!x) x = strchr(str, 'X');
    if (!x) return false;

    w = static_cast<uint32_t>(atoi(str));
    h = static_cast<uint32_t>(atoi(x + 1));
    return w > 0 && h > 0;
}

static int run_host(uint16_t port, uint32_t fps, uint32_t bitrate,
                    uint32_t width, uint32_t height) {
    HostSession session;
    if (!session.start(port, fps, bitrate, width, height, g_running)) {
        return 1;
    }

    LOG_INFO("Main", "Host running (Ctrl+C to stop)");
    while (g_running && session.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    session.stop();
    return 0;
}

static int run_client(const std::string& ip, uint16_t port) {
    ClientSession session;
    if (!session.connect(ip, port)) {
        return 1;
    }

    session.run(g_running);
    session.stop();
    return 0;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    bool host_mode = false;
    std::string client_ip;
    uint16_t port = DEFAULT_PORT;
    uint32_t fps = 30;
    uint32_t bitrate = 6000000;
    uint32_t width = 0;   // 0 = auto (capture full screen)
    uint32_t height = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0) {
            host_mode = true;
        } else if (strcmp(argv[i], "--client") == 0 && i + 1 < argc) {
            client_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            fps = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--bitrate") == 0 && i + 1 < argc) {
            bitrate = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            if (!parse_resolution(argv[++i], width, height)) {
                fprintf(stderr, "Invalid resolution format. Use WxH (e.g. 1920x1080)\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            Logger::set_level(LogLevel::Debug);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    // If no mode specified, launch the UI
    if (!host_mode && client_ip.empty()) {
        LauncherUI launcher;
        if (!launcher.init()) {
            fprintf(stderr, "Failed to initialize launcher UI\n");
            return 1;
        }

        auto config = launcher.run();
        launcher.shutdown();

        switch (config.mode) {
            case LaunchMode::Host:
                return run_host(port, fps, bitrate, width, height);
            case LaunchMode::Client:
                return run_client(config.host_ip, port);
            case LaunchMode::None:
            default:
                return 0;
        }
    }

    if (host_mode) {
        return run_host(port, fps, bitrate, width, height);
    } else {
        return run_client(client_ip, port);
    }
}
