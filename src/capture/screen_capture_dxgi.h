#pragma once

#ifdef LANCAST_PLATFORM_WINDOWS

#include "capture/capture_source.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

struct SwsContext;

namespace lancast {

class ScreenCaptureDXGI : public ICaptureSource {
public:
    ScreenCaptureDXGI() = default;
    ~ScreenCaptureDXGI() override;

    bool init(uint32_t target_width, uint32_t target_height,
              uint64_t window_id = 0) override;
    std::optional<RawVideoFrame> capture_frame() override;
    void shutdown() override;

    uint32_t native_width() const override { return screen_width_; }
    uint32_t native_height() const override { return screen_height_; }
    uint32_t target_width() const override { return target_width_; }
    uint32_t target_height() const override { return target_height_; }

    static std::vector<WindowInfo> list_windows();

private:
    // Full-screen DXGI Desktop Duplication
    bool init_dxgi();
    std::optional<RawVideoFrame> capture_dxgi();
    bool reinit_duplication();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;

    // Per-window capture (BitBlt fallback)
    bool init_window(HWND hwnd);
    std::optional<RawVideoFrame> capture_window();
    void cleanup_window_capture();

    HWND hwnd_ = nullptr;
    HDC window_dc_ = nullptr;
    HDC mem_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_bitmap_ = nullptr;
    uint32_t window_width_ = 0;
    uint32_t window_height_ = 0;

    // Common
    SwsContext* sws_ctx_ = nullptr;
    uint32_t screen_width_ = 0;
    uint32_t screen_height_ = 0;
    uint32_t target_width_ = 0;
    uint32_t target_height_ = 0;
    bool use_window_ = false;
    bool initialized_ = false;
};

} // namespace lancast

#endif // LANCAST_PLATFORM_WINDOWS
