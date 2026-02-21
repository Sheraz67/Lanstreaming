#ifdef LANCAST_PLATFORM_WINDOWS

#include "capture/screen_capture_dxgi.h"
#include "core/logger.h"
#include "core/clock.h"

#include <cinttypes>
#include <dwmapi.h>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

namespace lancast {

static constexpr const char* TAG = "CaptureDXGI";

// ──────────────────────────────────────────────────────────────
// list_windows — EnumWindows to get visible windows
// ──────────────────────────────────────────────────────────────

struct EnumContext {
    std::vector<WindowInfo>* result;
};

static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;

    // Skip windows with no title
    char title[256];
    int len = GetWindowTextA(hwnd, title, sizeof(title));
    if (len <= 0) return TRUE;

    // Skip windows with zero or negative size
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return TRUE;

    // Skip cloaked windows (e.g. UWP background apps)
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;

    WindowInfo info;
    info.id = reinterpret_cast<uint64_t>(hwnd);
    info.title = std::string(title, len);
    info.width = static_cast<uint32_t>(w);
    info.height = static_cast<uint32_t>(h);
    ctx->result->push_back(std::move(info));

    return TRUE;
}

std::vector<WindowInfo> ScreenCaptureDXGI::list_windows() {
    std::vector<WindowInfo> result;
    EnumContext ctx{&result};
    EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&ctx));
    return result;
}

// ──────────────────────────────────────────────────────────────
// Lifecycle
// ──────────────────────────────────────────────────────────────

ScreenCaptureDXGI::~ScreenCaptureDXGI() {
    shutdown();
}

bool ScreenCaptureDXGI::init(uint32_t target_width, uint32_t target_height,
                              uint64_t window_id) {
    if (window_id != 0) {
        HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(window_id));
        if (!IsWindow(hwnd)) {
            LOG_ERROR(TAG, "Invalid window handle: 0x%" PRIx64, window_id);
            return false;
        }
        use_window_ = true;
        if (!init_window(hwnd)) return false;
    } else {
        use_window_ = false;
        if (!init_dxgi()) return false;
    }

    // YUV420P requires even dimensions
    target_width_ = (target_width > 0) ? target_width : screen_width_;
    target_height_ = (target_height > 0) ? target_height : screen_height_;
    target_width_ &= ~1u;
    target_height_ &= ~1u;

    // Create swscale context for BGRA -> YUV420P
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
    LOG_INFO(TAG, "Capture initialized: %ux%u -> %ux%u (%s)",
             screen_width_, screen_height_, target_width_, target_height_,
             use_window_ ? "BitBlt/window" : "DXGI/desktop");
    return true;
}

void ScreenCaptureDXGI::shutdown() {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    cleanup_window_capture();

    duplication_.Reset();
    staging_.Reset();
    context_.Reset();
    device_.Reset();

    initialized_ = false;
    LOG_INFO(TAG, "Screen capture shut down");
}

std::optional<RawVideoFrame> ScreenCaptureDXGI::capture_frame() {
    if (!initialized_) return std::nullopt;
    return use_window_ ? capture_window() : capture_dxgi();
}

// ──────────────────────────────────────────────────────────────
// DXGI Desktop Duplication (full-screen)
// ──────────────────────────────────────────────────────────────

bool ScreenCaptureDXGI::init_dxgi() {
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0,
        D3D11_SDK_VERSION,
        device_.GetAddressOf(), &feature_level, context_.GetAddressOf()
    );
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "D3D11CreateDevice failed: 0x%08lx", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    hr = device_.As(&dxgi_device);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, output.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "No display outputs found");
        return false;
    }

    DXGI_OUTPUT_DESC output_desc;
    output->GetDesc(&output_desc);
    screen_width_ = static_cast<uint32_t>(
        output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left);
    screen_height_ = static_cast<uint32_t>(
        output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top);

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    hr = output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "DuplicateOutput failed: 0x%08lx (need Desktop Duplication API support)", hr);
        return false;
    }

    // Create staging texture for CPU readback
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = screen_width_;
    desc.Height = screen_height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device_->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "Failed to create staging texture: 0x%08lx", hr);
        return false;
    }

    LOG_INFO(TAG, "DXGI Desktop Duplication initialized: %ux%u", screen_width_, screen_height_);
    return true;
}

bool ScreenCaptureDXGI::reinit_duplication() {
    duplication_.Reset();

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = device_.As(&dxgi_device);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, output.GetAddressOf());
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    hr = output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf());
    if (FAILED(hr)) {
        LOG_WARN(TAG, "Failed to reinitialize duplication: 0x%08lx", hr);
        return false;
    }

    LOG_INFO(TAG, "DXGI duplication reinitialized");
    return true;
}

std::optional<RawVideoFrame> ScreenCaptureDXGI::capture_dxgi() {
    if (!duplication_) return std::nullopt;

    Microsoft::WRL::ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info{};

    HRESULT hr = duplication_->AcquireNextFrame(16, &frame_info, resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return std::nullopt;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        LOG_WARN(TAG, "DXGI access lost, reinitializing...");
        reinit_duplication();
        return std::nullopt;
    }
    if (FAILED(hr)) {
        LOG_WARN(TAG, "AcquireNextFrame failed: 0x%08lx", hr);
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop_tex;
    hr = resource.As(&desktop_tex);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return std::nullopt;
    }

    context_->CopyResource(staging_.Get(), desktop_tex.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return std::nullopt;
    }

    // Convert BGRA -> YUV420P
    const uint8_t* src_data[1] = { static_cast<const uint8_t*>(mapped.pData) };
    int src_linesize[1] = { static_cast<int>(mapped.RowPitch) };

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

    context_->Unmap(staging_.Get(), 0);
    duplication_->ReleaseFrame();

    Clock clock;
    frame.pts_us = clock.now_us();
    return frame;
}

// ──────────────────────────────────────────────────────────────
// Per-window capture (BitBlt/PrintWindow fallback)
// ──────────────────────────────────────────────────────────────

bool ScreenCaptureDXGI::init_window(HWND hwnd) {
    hwnd_ = hwnd;

    RECT rect;
    if (!GetClientRect(hwnd_, &rect)) {
        LOG_ERROR(TAG, "GetClientRect failed");
        return false;
    }

    screen_width_ = static_cast<uint32_t>(rect.right - rect.left);
    screen_height_ = static_cast<uint32_t>(rect.bottom - rect.top);
    if (screen_width_ == 0 || screen_height_ == 0) {
        LOG_ERROR(TAG, "Window has zero client area");
        return false;
    }
    window_width_ = screen_width_;
    window_height_ = screen_height_;

    window_dc_ = GetDC(hwnd_);
    if (!window_dc_) {
        LOG_ERROR(TAG, "GetDC failed");
        return false;
    }

    mem_dc_ = CreateCompatibleDC(window_dc_);
    if (!mem_dc_) {
        LOG_ERROR(TAG, "CreateCompatibleDC failed");
        ReleaseDC(hwnd_, window_dc_);
        window_dc_ = nullptr;
        return false;
    }

    bitmap_ = CreateCompatibleBitmap(window_dc_, screen_width_, screen_height_);
    if (!bitmap_) {
        LOG_ERROR(TAG, "CreateCompatibleBitmap failed");
        cleanup_window_capture();
        return false;
    }
    old_bitmap_ = SelectObject(mem_dc_, bitmap_);

    LOG_INFO(TAG, "Window capture initialized: %ux%u", screen_width_, screen_height_);
    return true;
}

void ScreenCaptureDXGI::cleanup_window_capture() {
    if (mem_dc_ && old_bitmap_) {
        SelectObject(mem_dc_, old_bitmap_);
        old_bitmap_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    if (mem_dc_) {
        DeleteDC(mem_dc_);
        mem_dc_ = nullptr;
    }
    if (window_dc_ && hwnd_) {
        ReleaseDC(hwnd_, window_dc_);
        window_dc_ = nullptr;
    }
}

std::optional<RawVideoFrame> ScreenCaptureDXGI::capture_window() {
    if (!IsWindow(hwnd_)) {
        LOG_WARN(TAG, "Target window no longer valid");
        return std::nullopt;
    }

    // Check for window resize
    RECT rect;
    if (!GetClientRect(hwnd_, &rect)) return std::nullopt;
    uint32_t cur_w = static_cast<uint32_t>(rect.right - rect.left);
    uint32_t cur_h = static_cast<uint32_t>(rect.bottom - rect.top);
    if (cur_w == 0 || cur_h == 0) return std::nullopt;

    if (cur_w != window_width_ || cur_h != window_height_) {
        // Window resized — recreate resources
        cleanup_window_capture();
        screen_width_ = cur_w;
        screen_height_ = cur_h;
        window_width_ = cur_w;
        window_height_ = cur_h;

        window_dc_ = GetDC(hwnd_);
        mem_dc_ = CreateCompatibleDC(window_dc_);
        bitmap_ = CreateCompatibleBitmap(window_dc_, screen_width_, screen_height_);
        old_bitmap_ = SelectObject(mem_dc_, bitmap_);

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

    // Capture using PrintWindow for better compatibility with occluded/DWM windows
    if (!PrintWindow(hwnd_, mem_dc_, PW_RENDERFULLCONTENT)) {
        // Fall back to BitBlt
        if (!BitBlt(mem_dc_, 0, 0, screen_width_, screen_height_,
                    window_dc_, 0, 0, SRCCOPY)) {
            LOG_WARN(TAG, "BitBlt failed");
            return std::nullopt;
        }
    }

    // Read pixel data from bitmap
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = static_cast<LONG>(screen_width_);
    bi.biHeight = -static_cast<LONG>(screen_height_); // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    size_t pixel_bytes = static_cast<size_t>(screen_width_) * screen_height_ * 4;
    std::vector<uint8_t> pixels(pixel_bytes);

    GetDIBits(mem_dc_, bitmap_, 0, screen_height_,
              pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    // Convert BGRA -> YUV420P
    const uint8_t* src_data[1] = { pixels.data() };
    int src_linesize[1] = { static_cast<int>(screen_width_ * 4) };

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

    Clock clock;
    frame.pts_us = clock.now_us();
    return frame;
}

} // namespace lancast

#endif // LANCAST_PLATFORM_WINDOWS
