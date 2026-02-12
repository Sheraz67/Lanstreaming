#include "app/launcher_ui.h"
#include "core/logger.h"

#include <SDL3/SDL.h>

namespace lancast {

static constexpr const char* TAG = "LauncherUI";

LauncherUI::~LauncherUI() {
    shutdown();
}

bool LauncherUI::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR(TAG, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow("lancast", 480, 320, 0);
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

    initialized_ = true;
    return true;
}

LaunchConfig LauncherUI::run() {
    if (!initialized_) return {};

    SDL_StartTextInput(window_);

    while (!done_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    done_ = true;
                    result_.mode = LaunchMode::None;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        if (ip_entry_) {
                            ip_entry_ = false;
                        } else {
                            done_ = true;
                            result_.mode = LaunchMode::None;
                        }
                    } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                        if (ip_entry_) {
                            if (!ip_text_.empty()) {
                                result_.mode = LaunchMode::Client;
                                result_.host_ip = ip_text_;
                                done_ = true;
                            }
                        } else if (selected_ == 0) {
                            result_.mode = LaunchMode::Host;
                            done_ = true;
                        } else {
                            ip_entry_ = true;
                            ip_text_.clear();
                        }
                    } else if (!ip_entry_) {
                        if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN ||
                            event.key.key == SDLK_TAB) {
                            selected_ = 1 - selected_;
                        }
                    } else {
                        if (event.key.key == SDLK_BACKSPACE && !ip_text_.empty()) {
                            ip_text_.pop_back();
                        }
                    }
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    if (ip_entry_) {
                        // Only allow IP-valid characters
                        for (const char* p = event.text.text; *p; ++p) {
                            char c = *p;
                            if ((c >= '0' && c <= '9') || c == '.') {
                                if (ip_text_.size() < 15) {
                                    ip_text_ += c;
                                }
                            }
                        }
                    }
                    break;

                default:
                    break;
            }
        }

        render();
        SDL_Delay(16); // ~60 fps
    }

    SDL_StopTextInput(window_);
    return result_;
}

void LauncherUI::render() {
    SDL_SetRenderDrawColor(renderer_, 24, 24, 32, 255);
    SDL_RenderClear(renderer_);

    float x = 40.0f;
    float y = 40.0f;

    // Title
    SDL_SetRenderDrawColor(renderer_, 100, 180, 255, 255);
    SDL_SetRenderScale(renderer_, 3.0f, 3.0f);
    SDL_RenderDebugText(renderer_, x / 3.0f, y / 3.0f, "lancast");
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);

    y += 60.0f;
    SDL_SetRenderDrawColor(renderer_, 160, 160, 160, 255);
    SDL_RenderDebugText(renderer_, x, y, "LAN Screen + Audio Streaming");

    y += 50.0f;

    if (!ip_entry_) {
        // Menu selection
        const char* items[] = {"[ Host Session ]", "[ Join Session ]"};
        for (int i = 0; i < 2; ++i) {
            if (i == selected_) {
                SDL_SetRenderDrawColor(renderer_, 100, 255, 100, 255);
                SDL_RenderDebugText(renderer_, x, y, "> ");
            } else {
                SDL_SetRenderDrawColor(renderer_, 180, 180, 180, 255);
                SDL_RenderDebugText(renderer_, x, y, "  ");
            }
            SDL_RenderDebugText(renderer_, x + 24.0f, y, items[i]);
            y += 30.0f;
        }

        y += 30.0f;
        SDL_SetRenderDrawColor(renderer_, 100, 100, 100, 255);
        SDL_RenderDebugText(renderer_, x, y, "Up/Down to select, Enter to confirm");
        y += 20.0f;
        SDL_RenderDebugText(renderer_, x, y, "ESC to quit");
    } else {
        // IP entry mode
        SDL_SetRenderDrawColor(renderer_, 180, 180, 180, 255);
        SDL_RenderDebugText(renderer_, x, y, "Enter host IP address:");

        y += 30.0f;

        // Draw input box
        SDL_FRect box = {x - 4.0f, y - 4.0f, 260.0f, 24.0f};
        SDL_SetRenderDrawColor(renderer_, 60, 60, 80, 255);
        SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 100, 180, 255, 255);
        SDL_RenderRect(renderer_, &box);

        // Draw IP text with cursor
        std::string display = ip_text_ + "_";
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDebugText(renderer_, x, y, display.c_str());

        y += 40.0f;
        SDL_SetRenderDrawColor(renderer_, 100, 100, 100, 255);
        SDL_RenderDebugText(renderer_, x, y, "Enter to connect, ESC to go back");
    }

    SDL_RenderPresent(renderer_);
}

void LauncherUI::shutdown() {
    if (!initialized_) return;

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
}

} // namespace lancast
