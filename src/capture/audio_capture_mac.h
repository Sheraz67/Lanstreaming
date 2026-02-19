#pragma once

#include "capture/audio_capture.h"
#include <memory>
#include <cstdint>
#include <optional>

namespace lancast {

class SCStreamManager;

class AudioCaptureMac : public IAudioCapture {
public:
    AudioCaptureMac() = default;
    ~AudioCaptureMac() override;

    // Set the shared stream manager (must be called before init)
    void set_stream_manager(std::shared_ptr<SCStreamManager> manager);

    bool init(uint32_t sample_rate, uint16_t channels) override;
    std::optional<RawAudioFrame> capture_frame() override;
    void shutdown() override;

private:
    std::shared_ptr<SCStreamManager> manager_;
    uint32_t sample_rate_ = 48000;
    uint16_t channels_ = 2;
    uint32_t frame_samples_ = 960; // 20ms at 48kHz
    bool initialized_ = false;
};

} // namespace lancast
