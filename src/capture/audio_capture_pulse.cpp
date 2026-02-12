#include "capture/audio_capture_pulse.h"
#include "core/logger.h"
#include "core/clock.h"

#include <pulse/simple.h>
#include <pulse/error.h>

namespace lancast {

static constexpr const char* TAG = "AudioCapturePulse";

AudioCapturePulse::~AudioCapturePulse() {
    shutdown();
}

bool AudioCapturePulse::init(uint32_t sample_rate, uint16_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_samples_ = sample_rate / 50; // 20ms worth of samples

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = sample_rate;
    spec.channels = static_cast<uint8_t>(channels);

    int error = 0;
    pa_ = pa_simple_new(
        nullptr,              // default server
        "lancast",            // application name
        PA_STREAM_RECORD,     // recording stream
        nullptr,              // default device (monitor source)
        "audio capture",      // stream description
        &spec,                // sample spec
        nullptr,              // default channel map
        nullptr,              // default buffering attributes
        &error
    );

    if (!pa_) {
        LOG_ERROR(TAG, "Failed to open PulseAudio: %s", pa_strerror(error));
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "PulseAudio capture initialized: %u Hz, %u channels, %u samples/frame",
             sample_rate, channels, frame_samples_);
    return true;
}

std::optional<RawAudioFrame> AudioCapturePulse::capture_frame() {
    if (!initialized_) return std::nullopt;

    RawAudioFrame frame;
    frame.sample_rate = sample_rate_;
    frame.channels = channels_;
    frame.num_samples = frame_samples_;
    frame.samples.resize(static_cast<size_t>(frame_samples_) * channels_);

    int error = 0;
    int ret = pa_simple_read(
        pa_,
        frame.samples.data(),
        frame.samples.size() * sizeof(float),
        &error
    );

    if (ret < 0) {
        LOG_ERROR(TAG, "PulseAudio read failed: %s", pa_strerror(error));
        return std::nullopt;
    }

    Clock clock;
    frame.pts_us = clock.now_us();

    return frame;
}

void AudioCapturePulse::shutdown() {
    if (!initialized_) return;

    if (pa_) {
        pa_simple_free(pa_);
        pa_ = nullptr;
    }

    initialized_ = false;
    LOG_INFO(TAG, "PulseAudio capture shut down");
}

} // namespace lancast
