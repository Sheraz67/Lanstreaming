#pragma once

#include "core/types.h"
#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace lancast {

class SdlRenderer {
public:
    SdlRenderer() = default;
    ~SdlRenderer();

    bool init(uint32_t width, uint32_t height, const std::string& title = "lancast");
    void render_frame(const RawVideoFrame& frame);

    // Returns false if user requested quit (close window or ESC)
    bool poll_events();
    void toggle_fullscreen();

    void shutdown();

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool initialized_ = false;
    bool fullscreen_ = false;
};

} // namespace lancast
