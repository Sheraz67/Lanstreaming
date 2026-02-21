#include "capture/screen_capture_x11.h"
#include "core/logger.h"

#include <cinttypes>
#include <X11/Xatom.h>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

namespace lancast {

static constexpr const char* TAG = "CaptureX11";

// Helper: get a string property from an X11 window
static std::string get_window_name(Display* dpy, Window win) {
    // Try _NET_WM_NAME first (UTF-8)
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", True);
    if (net_wm_name != None && utf8_string != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* prop = nullptr;
        if (XGetWindowProperty(dpy, win, net_wm_name, 0, 1024, False,
                               utf8_string, &actual_type, &actual_format,
                               &nitems, &bytes_after, &prop) == Success && prop) {
            std::string name(reinterpret_cast<char*>(prop), nitems);
            XFree(prop);
            if (!name.empty()) return name;
        }
    }

    // Fallback to WM_NAME
    char* wm_name = nullptr;
    if (XFetchName(dpy, win, &wm_name) && wm_name) {
        std::string name(wm_name);
        XFree(wm_name);
        return name;
    }

    return {};
}

std::vector<WindowInfo> ScreenCaptureX11::list_windows() {
    std::vector<WindowInfo> result;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return result;

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    // Get _NET_CLIENT_LIST from root window
    Atom client_list_atom = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (client_list_atom == None) {
        XCloseDisplay(dpy);
        return result;
    }

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(dpy, root, client_list_atom, 0, 1024, False,
                           XA_WINDOW, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) != Success || !prop) {
        XCloseDisplay(dpy);
        return result;
    }

    Window* windows = reinterpret_cast<Window*>(prop);
    for (unsigned long i = 0; i < nitems; ++i) {
        Window win = windows[i];

        XWindowAttributes attrs;
        if (!XGetWindowAttributes(dpy, win, &attrs)) continue;
        if (attrs.width <= 0 || attrs.height <= 0) continue;
        if (attrs.map_state != IsViewable) continue;

        std::string title = get_window_name(dpy, win);
        if (title.empty()) continue;

        WindowInfo info;
        info.id = win;
        info.title = std::move(title);
        info.width = static_cast<uint32_t>(attrs.width);
        info.height = static_cast<uint32_t>(attrs.height);
        result.push_back(std::move(info));
    }

    XFree(prop);
    XCloseDisplay(dpy);
    return result;
}

ScreenCaptureX11::~ScreenCaptureX11() {
    shutdown();
}

bool ScreenCaptureX11::init(uint32_t target_width, uint32_t target_height,
                             uint64_t window_id) {
    // Open X display
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        LOG_ERROR(TAG, "Failed to open X display");
        return false;
    }

    // Get root window
    int screen = DefaultScreen(display_);
    root_ = RootWindow(display_, screen);

    // Determine capture target
    if (window_id != 0) {
        target_window_ = static_cast<Window>(window_id);
        use_window_ = true;

        // Validate the window exists
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(display_, target_window_, &attrs)) {
            LOG_ERROR(TAG, "Invalid window ID: 0x%" PRIx64, window_id);
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }
        screen_width_ = static_cast<uint32_t>(attrs.width);
        screen_height_ = static_cast<uint32_t>(attrs.height);
        LOG_INFO(TAG, "Capturing window 0x%" PRIx64 " (%ux%u)", window_id,
                 screen_width_, screen_height_);
    } else {
        target_window_ = root_;
        use_window_ = false;

        XWindowAttributes attrs;
        XGetWindowAttributes(display_, root_, &attrs);
        screen_width_ = static_cast<uint32_t>(attrs.width);
        screen_height_ = static_cast<uint32_t>(attrs.height);
    }

    // If target is 0, use native resolution
    // YUV420P requires even dimensions for chroma subsampling
    target_width_ = (target_width > 0) ? target_width : screen_width_;
    target_height_ = (target_height > 0) ? target_height : screen_height_;
    target_width_ &= ~1u;
    target_height_ &= ~1u;

    LOG_INFO(TAG, "Capture size: %ux%u, target: %ux%u",
             screen_width_, screen_height_, target_width_, target_height_);

    if (!use_window_) {
        // Full screen: use XShm for best performance
        if (!XShmQueryExtension(display_)) {
            LOG_ERROR(TAG, "XShm extension not available");
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }

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
    }
    // For window capture, we use XGetImage per frame (no XShm setup needed)

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
    LOG_INFO(TAG, "Screen capture initialized (%s)",
             use_window_ ? "XGetImage/window" : "XShm/root");
    return true;
}

std::optional<RawVideoFrame> ScreenCaptureX11::capture_frame() {
    if (!initialized_) return std::nullopt;

    const uint8_t* src_ptr;
    int src_linesize_val;

    if (!use_window_) {
        // Full screen: XShm (fastest)
        if (!XShmGetImage(display_, root_, ximage_, 0, 0, AllPlanes)) {
            LOG_WARN(TAG, "XShmGetImage failed");
            return std::nullopt;
        }
        src_ptr = reinterpret_cast<const uint8_t*>(ximage_->data);
        src_linesize_val = ximage_->bytes_per_line;
    } else {
        // Window capture: use XGetImage (more compatible across compositors)
        // Re-query window geometry in case it was resized
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(display_, target_window_, &attrs)) {
            LOG_WARN(TAG, "Target window no longer valid");
            return std::nullopt;
        }

        uint32_t cur_w = static_cast<uint32_t>(attrs.width);
        uint32_t cur_h = static_cast<uint32_t>(attrs.height);

        // If window resized, update swscale context
        if (cur_w != screen_width_ || cur_h != screen_height_) {
            screen_width_ = cur_w;
            screen_height_ = cur_h;

            if (sws_ctx_) sws_freeContext(sws_ctx_);
            sws_ctx_ = sws_getContext(
                static_cast<int>(screen_width_), static_cast<int>(screen_height_),
                AV_PIX_FMT_BGRA,
                static_cast<int>(target_width_), static_cast<int>(target_height_),
                AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            if (!sws_ctx_) {
                LOG_ERROR(TAG, "Failed to recreate swscale context after resize");
                return std::nullopt;
            }
            LOG_INFO(TAG, "Window resized to %ux%u", cur_w, cur_h);
        }

        XImage* img = XGetImage(display_, target_window_, 0, 0,
                                screen_width_, screen_height_,
                                AllPlanes, ZPixmap);
        if (!img) {
            LOG_WARN(TAG, "XGetImage failed");
            return std::nullopt;
        }

        src_ptr = reinterpret_cast<const uint8_t*>(img->data);
        src_linesize_val = img->bytes_per_line;

        // Convert and build frame, then free the XImage
        const uint8_t* src_data[1] = { src_ptr };
        int src_linesize[1] = { src_linesize_val };

        int w = static_cast<int>(target_width_);
        int h = static_cast<int>(target_height_);
        size_t y_size = static_cast<size_t>(w) * h;
        size_t uv_size = y_size / 4;

        RawVideoFrame frame;
        frame.width = target_width_;
        frame.height = target_height_;
        frame.data.resize(y_size + uv_size * 2);

        uint8_t* dst_data[3] = {
            frame.data.data(),
            frame.data.data() + y_size,
            frame.data.data() + y_size + uv_size
        };
        int dst_linesize[3] = { w, w / 2, w / 2 };

        sws_scale(sws_ctx_, src_data, src_linesize, 0,
                  static_cast<int>(screen_height_), dst_data, dst_linesize);

        XDestroyImage(img);
        return frame;
    }

    // Full screen path (XShm) — shared conversion code
    const uint8_t* src_data[1] = { src_ptr };
    int src_linesize[1] = { src_linesize_val };

    int w = static_cast<int>(target_width_);
    int h = static_cast<int>(target_height_);
    size_t y_size = static_cast<size_t>(w) * h;
    size_t uv_size = y_size / 4;

    RawVideoFrame frame;
    frame.width = target_width_;
    frame.height = target_height_;
    frame.data.resize(y_size + uv_size * 2);

    uint8_t* dst_data[3] = {
        frame.data.data(),
        frame.data.data() + y_size,
        frame.data.data() + y_size + uv_size
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
