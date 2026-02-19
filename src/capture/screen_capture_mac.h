#pragma once

#include "capture/capture_source.h"

#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>

// Forward-declare Objective-C types for the C++ header
#ifdef __OBJC__
@class SCStream;
@class SCContentFilter;
@class LancastStreamDelegate;
#else
typedef void SCStream;
typedef void SCContentFilter;
typedef void LancastStreamDelegate;
#endif

struct SwsContext;

namespace lancast {

// Shared manager that owns the SCStream used by both video and audio capture.
// ScreenCaptureMac creates and owns it; AudioCaptureMac holds a reference.
class SCStreamManager {
public:
    SCStreamManager() = default;
    ~SCStreamManager();

    bool start(uint32_t target_width, uint32_t target_height,
               unsigned long window_id);
    void stop();

    // Called by the delegate when a video frame arrives
    void push_video_frame(RawVideoFrame frame);

    // Called by the delegate when an audio buffer arrives
    void push_audio_samples(const float* samples, uint32_t num_samples,
                            uint32_t sample_rate, uint16_t channels);

    std::optional<RawVideoFrame> pop_video_frame();

    // Pop interleaved float32 audio samples (returns up to max_samples per channel)
    std::vector<float> pop_audio_samples(uint32_t num_samples, uint16_t channels);

    uint32_t native_width() const { return native_width_; }
    uint32_t native_height() const { return native_height_; }
    bool is_running() const { return running_; }

private:
    SCStream* stream_ = nullptr;
    SCContentFilter* filter_ = nullptr;
    LancastStreamDelegate* delegate_ = nullptr;

    uint32_t native_width_ = 0;
    uint32_t native_height_ = 0;
    bool running_ = false;

    // Video frame queue
    std::mutex video_mutex_;
    std::condition_variable video_cv_;
    std::queue<RawVideoFrame> video_queue_;
    static constexpr size_t kMaxVideoFrames = 4;

    // Audio sample buffer (interleaved float32)
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    std::vector<float> audio_buffer_;
    uint16_t audio_channels_ = 2;
};

class ScreenCaptureMac : public ICaptureSource {
public:
    ScreenCaptureMac() = default;
    ~ScreenCaptureMac() override;

    bool init(uint32_t target_width, uint32_t target_height,
              unsigned long window_id = 0) override;
    std::optional<RawVideoFrame> capture_frame() override;
    void shutdown() override;

    uint32_t native_width() const override { return native_width_; }
    uint32_t native_height() const override { return native_height_; }
    uint32_t target_width() const override { return target_width_; }
    uint32_t target_height() const override { return target_height_; }

    static std::vector<WindowInfo> list_windows();

    // Provides access to the shared stream manager for AudioCaptureMac
    std::shared_ptr<SCStreamManager> stream_manager() const { return manager_; }

private:
    std::shared_ptr<SCStreamManager> manager_;

    uint32_t native_width_ = 0;
    uint32_t native_height_ = 0;
    uint32_t target_width_ = 0;
    uint32_t target_height_ = 0;

    SwsContext* sws_ctx_ = nullptr;
    bool initialized_ = false;
};

} // namespace lancast
