#pragma once

#include "capture/capture_source.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

struct SwsContext;

namespace lancast {

class ScreenCaptureX11 : public ICaptureSource {
public:
    ScreenCaptureX11() = default;
    ~ScreenCaptureX11() override;

    bool init(uint32_t target_width, uint32_t target_height) override;
    std::optional<RawVideoFrame> capture_frame() override;
    void shutdown() override;

    uint32_t native_width() const override { return screen_width_; }
    uint32_t native_height() const override { return screen_height_; }

private:
    Display* display_ = nullptr;
    Window root_ = 0;
    XImage* ximage_ = nullptr;
    XShmSegmentInfo shm_info_{};

    uint32_t screen_width_ = 0;
    uint32_t screen_height_ = 0;
    uint32_t target_width_ = 0;
    uint32_t target_height_ = 0;

    SwsContext* sws_ctx_ = nullptr;
    bool shm_attached_ = false;
    bool initialized_ = false;
};

} // namespace lancast
