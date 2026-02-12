#include "render/sdl_renderer.h"
#include "core/logger.h"

#include <SDL3/SDL.h>

namespace lancast {

static constexpr const char* TAG = "SdlRenderer";

SdlRenderer::~SdlRenderer() {
    shutdown();
}

bool SdlRenderer::init(uint32_t width, uint32_t height, const std::string& title) {
    width_ = width;
    height_ = height;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR(TAG, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(title.c_str(),
                                static_cast<int>(width),
                                static_cast<int>(height),
                                SDL_WINDOW_RESIZABLE);
    if (!window_) {
        LOG_ERROR(TAG, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        LOG_ERROR(TAG, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    texture_ = SDL_CreateTexture(renderer_,
                                  SDL_PIXELFORMAT_IYUV,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  static_cast<int>(width),
                                  static_cast<int>(height));
    if (!texture_) {
        LOG_ERROR(TAG, "SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "SDL renderer initialized (%ux%u)", width, height);
    return true;
}

void SdlRenderer::render_frame(const RawVideoFrame& frame) {
    if (!initialized_) return;
    if (frame.width != width_ || frame.height != height_) return;

    int w = static_cast<int>(width_);
    int h = static_cast<int>(height_);
    size_t y_size = static_cast<size_t>(w) * h;
    size_t uv_size = y_size / 4;

    if (frame.data.size() < y_size + uv_size * 2) return;

    const uint8_t* y_plane = frame.data.data();
    const uint8_t* u_plane = frame.data.data() + y_size;
    const uint8_t* v_plane = frame.data.data() + y_size + uv_size;

    SDL_UpdateYUVTexture(texture_, nullptr,
                          y_plane, w,
                          u_plane, w / 2,
                          v_plane, w / 2);

    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

bool SdlRenderer::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                return false;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    return false;
                }
                if (event.key.key == SDLK_F11) {
                    toggle_fullscreen();
                }
                break;
            default:
                break;
        }
    }
    return true;
}

void SdlRenderer::toggle_fullscreen() {
    if (!initialized_ || !window_) return;

    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_);
    LOG_INFO(TAG, "Fullscreen %s", fullscreen_ ? "enabled" : "disabled");
}

void SdlRenderer::shutdown() {
    if (!initialized_) return;

    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();

    initialized_ = false;
    LOG_INFO(TAG, "SDL renderer shut down");
}

} // namespace lancast
