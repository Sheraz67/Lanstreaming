#pragma once

#ifdef LANCAST_PLATFORM_WINDOWS

#include "capture/audio_capture.h"
#include <cstdint>
#include <optional>
#include <vector>

struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct SwrContext;

namespace lancast {

class AudioCaptureWASAPI : public IAudioCapture {
public:
    AudioCaptureWASAPI() = default;
    ~AudioCaptureWASAPI() override;

    bool init(uint32_t sample_rate, uint16_t channels) override;
    std::optional<RawAudioFrame> capture_frame() override;
    void shutdown() override;

private:
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    SwrContext* swr_ctx_ = nullptr;

    uint32_t sample_rate_ = 48000;
    uint16_t channels_ = 2;
    uint32_t frame_samples_ = 960; // 20ms at 48kHz

    uint32_t device_sample_rate_ = 0;
    uint16_t device_channels_ = 0;
    uint16_t device_bits_ = 0;

    std::vector<float> accumulator_;
    bool initialized_ = false;
    bool com_initialized_ = false;
};

} // namespace lancast

#endif // LANCAST_PLATFORM_WINDOWS
