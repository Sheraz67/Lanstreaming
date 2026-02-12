#pragma once

#include <string>
#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;

namespace lancast {

enum class LaunchMode {
    None,   // User quit
    Host,
    Client,
};

struct LaunchConfig {
    LaunchMode mode = LaunchMode::None;
    std::string host_ip;  // Only set for Client mode
};

class LauncherUI {
public:
    LauncherUI() = default;
    ~LauncherUI();

    bool init();

    // Blocks until user chooses host/client or closes window.
    LaunchConfig run();

    void shutdown();

private:
    void render();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool initialized_ = false;

    int selected_ = 0;         // 0 = Host, 1 = Client
    bool ip_entry_ = false;    // True when typing IP
    std::string ip_text_;      // IP text buffer
    bool done_ = false;
    LaunchConfig result_;
};

} // namespace lancast
