#pragma once

#include "core/types.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lancast {

struct WindowInfo {
    uint64_t id;            // Platform window ID (X11 Window, CGWindowID, HWND)
    std::string title;
    uint32_t width;
    uint32_t height;
};

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    virtual bool init(uint32_t target_width, uint32_t target_height,
                      uint64_t window_id = 0) = 0;
    virtual std::optional<RawVideoFrame> capture_frame() = 0;
    virtual void shutdown() = 0;

    virtual uint32_t native_width() const = 0;
    virtual uint32_t native_height() const = 0;
    virtual uint32_t target_width() const = 0;
    virtual uint32_t target_height() const = 0;
};

} // namespace lancast
