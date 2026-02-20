#include "capture/audio_capture_pulse.h"
#include "core/logger.h"
#include "core/clock.h"

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include <algorithm>
#include <string>

namespace lancast {

static constexpr const char* TAG = "AudioCapturePulse";

// Noise gate threshold: frames with RMS below this are silenced.
// -46 dB ≈ 0.005 amplitude. Suppresses hardware noise floor and
// mic noise without affecting real audio.
static constexpr float NOISE_GATE_THRESHOLD_SQ = 0.005f * 0.005f;

// Query PulseAudio for the monitor source of the default output sink.
// Returns e.g. "alsa_output.pci-0000_00_1b.0.analog-stereo.monitor",
// or empty string on failure.
static std::string get_default_monitor_source() {
    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) return "";

    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "lancast-probe");
    if (!ctx) {
        pa_mainloop_free(ml);
        return "";
    }

    if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return "";
    }

    // Wait for context to be ready
    bool failed = false;
    while (true) {
        pa_mainloop_iterate(ml, 1, nullptr);
        auto state = pa_context_get_state(ctx);
        if (state == PA_CONTEXT_READY) break;
        if (!PA_CONTEXT_IS_GOOD(state)) { failed = true; break; }
    }

    std::string monitor_name;
    if (!failed) {
        struct ProbeData { std::string name; bool done = false; };
        ProbeData probe;

        pa_context_get_server_info(ctx,
            [](pa_context*, const pa_server_info* info, void* ud) {
                auto* p = static_cast<ProbeData*>(ud);
                if (info && info->default_sink_name) {
                    p->name = std::string(info->default_sink_name) + ".monitor";
                }
                p->done = true;
            }, &probe);

        while (!probe.done) {
            pa_mainloop_iterate(ml, 1, nullptr);
        }
        monitor_name = std::move(probe.name);
    }

    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return monitor_name;
}

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

    // Use the monitor source (system audio loopback) instead of the
    // default source (which is typically the microphone).
    std::string monitor = get_default_monitor_source();
    const char* device = monitor.empty() ? nullptr : monitor.c_str();

    if (!monitor.empty()) {
        LOG_INFO(TAG, "Using monitor source: %s", monitor.c_str());
    } else {
        LOG_WARN(TAG, "Could not detect monitor source, falling back to default device");
    }

    int error = 0;
    pa_ = pa_simple_new(
        nullptr,              // default server
        "lancast",            // application name
        PA_STREAM_RECORD,     // recording stream
        device,               // monitor source (system audio loopback)
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

    // Noise gate: silence frames below the threshold to suppress
    // hardware noise floor / residual monitor noise.
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
