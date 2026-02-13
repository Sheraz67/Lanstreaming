#include "capture/screen_capture_x11.h"
#include "core/logger.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

namespace lancast {

static constexpr const char* TAG = "CaptureX11";

ScreenCaptureX11::~ScreenCaptureX11() {
    shutdown();
}

bool ScreenCaptureX11::init(uint32_t target_width, uint32_t target_height) {
    // Open X display
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        LOG_ERROR(TAG, "Failed to open X display");
        return false;
    }

    // Get root window and screen dimensions
    int screen = DefaultScreen(display_);
    root_ = RootWindow(display_, screen);

    XWindowAttributes attrs;
    XGetWindowAttributes(display_, root_, &attrs);
    screen_width_ = static_cast<uint32_t>(attrs.width);
    screen_height_ = static_cast<uint32_t>(attrs.height);

    // If target is 0, use native screen resolution
    // YUV420P requires even dimensions for chroma subsampling
    target_width_ = (target_width > 0) ? target_width : screen_width_;
    target_height_ = (target_height > 0) ? target_height : screen_height_;
    target_width_ &= ~1u;
    target_height_ &= ~1u;

    LOG_INFO(TAG, "Screen size: %ux%u, target: %ux%u",
             screen_width_, screen_height_, target_width_, target_height_);

    // Check for XShm extension
    if (!XShmQueryExtension(display_)) {
        LOG_ERROR(TAG, "XShm extension not available");
        XCloseDisplay(display_);
        display_ = nullptr;
        return false;
    }

    // Create shared memory XImage
    int depth = DefaultDepth(display_, screen);
    Visual* visual = DefaultVisual(display_, screen);

    ximage_ = XShmCreateImage(display_, visual, depth, ZPixmap, nullptr,
                               &shm_info_, screen_width_, screen_height_);
    if (!ximage_) {
        LOG_ERROR(TAG, "Failed to create XShm image");
        XCloseDisplay(display_);
        display_ = nullptr;
        return false;
    }

    // Allocate shared memory
    shm_info_.shmid = shmget(IPC_PRIVATE,
                              ximage_->bytes_per_line * ximage_->height,
                              IPC_CREAT | 0600);
    if (shm_info_.shmid < 0) {
        LOG_ERROR(TAG, "shmget failed");
        XDestroyImage(ximage_);
        ximage_ = nullptr;
        XCloseDisplay(display_);
        display_ = nullptr;
        return false;
    }

    shm_info_.shmaddr = static_cast<char*>(shmat(shm_info_.shmid, nullptr, 0));
    if (shm_info_.shmaddr == reinterpret_cast<char*>(-1)) {
        LOG_ERROR(TAG, "shmat failed");
        shmctl(shm_info_.shmid, IPC_RMID, nullptr);
        XDestroyImage(ximage_);
        ximage_ = nullptr;
        XCloseDisplay(display_);
        display_ = nullptr;
        return false;
    }
    ximage_->data = shm_info_.shmaddr;
    shm_info_.readOnly = False;

    if (!XShmAttach(display_, &shm_info_)) {
        LOG_ERROR(TAG, "XShmAttach failed");
        shmdt(shm_info_.shmaddr);
        shmctl(shm_info_.shmid, IPC_RMID, nullptr);
        XDestroyImage(ximage_);
        ximage_ = nullptr;
        XCloseDisplay(display_);
        display_ = nullptr;
        return false;
    }
    shm_attached_ = true;

    // Create swscale context for BGRA -> YUV420P conversion
    // X11 captures in BGRA format (32-bit with alpha in high byte)
    sws_ctx_ = sws_getContext(
        static_cast<int>(screen_width_), static_cast<int>(screen_height_),
        AV_PIX_FMT_BGRA,
        static_cast<int>(target_width_), static_cast<int>(target_height_),
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!sws_ctx_) {
        LOG_ERROR(TAG, "Failed to create swscale context");
        shutdown();
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "Screen capture initialized (XShm)");
    return true;
}

std::optional<RawVideoFrame> ScreenCaptureX11::capture_frame() {
    if (!initialized_) return std::nullopt;

    // Capture screen via XShm
    if (!XShmGetImage(display_, root_, ximage_, 0, 0, AllPlanes)) {
        LOG_WARN(TAG, "XShmGetImage failed");
        return std::nullopt;
    }

    // Set up source (BGRA) planes
    const uint8_t* src_data[1] = { reinterpret_cast<const uint8_t*>(ximage_->data) };
    int src_linesize[1] = { ximage_->bytes_per_line };

    // Allocate destination YUV420P frame
    int w = static_cast<int>(target_width_);
    int h = static_cast<int>(target_height_);
    size_t y_size = static_cast<size_t>(w) * h;
    size_t uv_size = y_size / 4;

    RawVideoFrame frame;
    frame.width = target_width_;
    frame.height = target_height_;
    frame.data.resize(y_size + uv_size * 2);

    uint8_t* dst_data[3] = {
        frame.data.data(),                    // Y
        frame.data.data() + y_size,           // U
        frame.data.data() + y_size + uv_size  // V
    };
    int dst_linesize[3] = { w, w / 2, w / 2 };

    sws_scale(sws_ctx_, src_data, src_linesize, 0,
              static_cast<int>(screen_height_), dst_data, dst_linesize);

    return frame;
}

void ScreenCaptureX11::shutdown() {
    if (!display_) return;

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    if (shm_attached_) {
        XShmDetach(display_, &shm_info_);
        shm_attached_ = false;
    }

    if (shm_info_.shmaddr && shm_info_.shmaddr != reinterpret_cast<char*>(-1)) {
        shmdt(shm_info_.shmaddr);
        shm_info_.shmaddr = nullptr;
    }

    if (shm_info_.shmid >= 0) {
        shmctl(shm_info_.shmid, IPC_RMID, nullptr);
        shm_info_.shmid = -1;
    }

    if (ximage_) {
        XDestroyImage(ximage_);
        ximage_ = nullptr;
    }

    XCloseDisplay(display_);
    display_ = nullptr;
    initialized_ = false;

    LOG_INFO(TAG, "Screen capture shut down");
}

} // namespace lancast
