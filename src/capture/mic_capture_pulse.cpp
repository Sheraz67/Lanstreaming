#include "capture/mic_capture_pulse.h"
#include "core/logger.h"
#include "core/clock.h"

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <algorithm>

namespace lancast {

static constexpr const char* TAG = "MicCapturePulse";

// Noise gate threshold: frames with RMS below this are silenced.
// -46 dB ~ 0.005 amplitude. Suppresses mic noise floor.
static constexpr float NOISE_GATE_THRESHOLD_SQ = 0.005f * 0.005f;

MicCapturePulse::~MicCapturePulse() {
    shutdown();
}

bool MicCapturePulse::init(uint32_t sample_rate, uint16_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_samples_ = sample_rate / 50; // 20ms worth of samples

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = sample_rate;
    spec.channels = static_cast<uint8_t>(channels);

    // Use nullptr device = PulseAudio default source (microphone input)
    int error = 0;
    pa_ = pa_simple_new(
        nullptr,              // default server
        "lancast",            // application name
        PA_STREAM_RECORD,     // recording stream
        nullptr,              // default source (microphone)
        "mic capture",        // stream description
        &spec,                // sample spec
        nullptr,              // default channel map
        nullptr,              // default buffering attributes
        &error
    );

    if (!pa_) {
        LOG_ERROR(TAG, "Failed to open PulseAudio mic: %s", pa_strerror(error));
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "Mic capture initialized: %u Hz, %u channels, %u samples/frame",
             sample_rate, channels, frame_samples_);
    return true;
}

std::optional<RawAudioFrame> MicCapturePulse::capture_frame() {
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
        LOG_ERROR(TAG, "PulseAudio mic read failed: %s", pa_strerror(error));
        return std::nullopt;
    }

    // Noise gate: silence frames below the threshold
    float sum_sq = 0.0f;
    for (float s : frame.samples) {
        sum_sq += s * s;
    }
    float mean_sq = sum_sq / static_cast<float>(frame.samples.size());
    if (mean_sq < NOISE_GATE_THRESHOLD_SQ) {
        std::fill(frame.samples.begin(), frame.samples.end(), 0.0f);
    }

    Clock clock;
    frame.pts_us = clock.now_us();

    return frame;
}

void MicCapturePulse::shutdown() {
    if (!initialized_) return;

    if (pa_) {
        pa_simple_free(pa_);
        pa_ = nullptr;
    }

    initialized_ = false;
    LOG_INFO(TAG, "Mic capture shut down");
}

} // namespace lancast
