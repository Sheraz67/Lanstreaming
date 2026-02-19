#include "capture/audio_capture_mac.h"
#include "capture/screen_capture_mac.h"
#include "core/logger.h"
#include "core/clock.h"

namespace lancast {

static constexpr const char* TAG = "AudioCaptureMac";

AudioCaptureMac::~AudioCaptureMac() {
    shutdown();
}

void AudioCaptureMac::set_stream_manager(std::shared_ptr<SCStreamManager> manager) {
    manager_ = std::move(manager);
}

bool AudioCaptureMac::init(uint32_t sample_rate, uint16_t channels) {
    if (!manager_) {
        LOG_ERROR(TAG, "No stream manager set — call set_stream_manager() first");
        return false;
    }

    if (!manager_->is_running()) {
        LOG_ERROR(TAG, "Stream manager is not running");
        return false;
    }

    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_samples_ = sample_rate / 50; // 20ms worth of samples

    initialized_ = true;
    LOG_INFO(TAG, "macOS audio capture initialized: %u Hz, %u channels, %u samples/frame",
             sample_rate, channels, frame_samples_);
    return true;
}

std::optional<RawAudioFrame> AudioCaptureMac::capture_frame() {
    if (!initialized_ || !manager_) return std::nullopt;

    auto samples = manager_->pop_audio_samples(frame_samples_, channels_);
    if (samples.empty()) return std::nullopt;

    RawAudioFrame frame;
    frame.sample_rate = sample_rate_;
    frame.channels = channels_;
    frame.num_samples = frame_samples_;
    frame.samples = std::move(samples);

    Clock clock;
    frame.pts_us = clock.now_us();
    return frame;
}

void AudioCaptureMac::shutdown() {
    if (!initialized_) return;

    // Don't stop the manager — ScreenCaptureMac owns it
    manager_.reset();
    initialized_ = false;
    LOG_INFO(TAG, "macOS audio capture shut down");
}

} // namespace lancast
