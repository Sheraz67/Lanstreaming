#ifdef LANCAST_PLATFORM_WINDOWS

#include "capture/mic_capture_wasapi.h"
#include "core/logger.h"
#include "core/clock.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <algorithm>
#include <cstring>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
}

namespace lancast {

static constexpr const char* TAG = "MicCaptureWASAPI";

static constexpr REFERENCE_TIME REFTIMES_PER_SEC = 10000000;

MicCaptureWASAPI::~MicCaptureWASAPI() {
    shutdown();
}

bool MicCaptureWASAPI::init(uint32_t sample_rate, uint16_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_samples_ = sample_rate / 50; // 20ms

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG_ERROR(TAG, "CoInitializeEx failed: 0x%08lx", hr);
        return false;
    }
    com_initialized_ = true;

    // Get default audio CAPTURE endpoint (microphone)
    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)
    );
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "Failed to create device enumerator: 0x%08lx", hr);
        return false;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device_);
    enumerator->Release();
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "Failed to get default capture endpoint: 0x%08lx", hr);
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&audio_client_));
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "Failed to activate audio client: 0x%08lx", hr);
        return false;
    }

    WAVEFORMATEX* mix_format = nullptr;
    hr = audio_client_->GetMixFormat(&mix_format);
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "GetMixFormat failed: 0x%08lx", hr);
        return false;
    }

    device_sample_rate_ = mix_format->nSamplesPerSec;
    device_channels_ = static_cast<uint16_t>(mix_format->nChannels);

    LOG_INFO(TAG, "Mic device format: %u Hz, %u ch, %u bits",
             device_sample_rate_, device_channels_, mix_format->wBitsPerSample);

    // Initialize in shared mode (NO loopback flag — this is a real capture device)
    REFERENCE_TIME buffer_duration = REFTIMES_PER_SEC / 10; // 100ms
    hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0, // No loopback
        buffer_duration, 0,
        mix_format, nullptr
    );
    CoTaskMemFree(mix_format);
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "AudioClient::Initialize failed: 0x%08lx", hr);
        return false;
    }

    hr = audio_client_->GetService(__uuidof(IAudioCaptureClient),
                                   reinterpret_cast<void**>(&capture_client_));
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "GetService(IAudioCaptureClient) failed: 0x%08lx", hr);
        return false;
    }

    // Set up resampler if needed
    if (device_sample_rate_ != sample_rate_ || device_channels_ != channels_) {
        AVChannelLayout src_layout{}, dst_layout{};
        av_channel_layout_default(&src_layout, device_channels_);
        av_channel_layout_default(&dst_layout, channels_);

        swr_alloc_set_opts2(&swr_ctx_,
                            &dst_layout, AV_SAMPLE_FMT_FLT, sample_rate_,
                            &src_layout, AV_SAMPLE_FMT_FLT, device_sample_rate_,
                            0, nullptr);
        if (!swr_ctx_ || swr_init(swr_ctx_) < 0) {
            LOG_ERROR(TAG, "Failed to initialize resampler");
            if (swr_ctx_) { swr_free(&swr_ctx_); swr_ctx_ = nullptr; }
            return false;
        }
        LOG_INFO(TAG, "Mic resampler: %u Hz %u ch -> %u Hz %u ch",
                 device_sample_rate_, device_channels_, sample_rate_, channels_);
    }

    hr = audio_client_->Start();
    if (FAILED(hr)) {
        LOG_ERROR(TAG, "AudioClient::Start failed: 0x%08lx", hr);
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "WASAPI mic capture initialized: %u Hz, %u channels",
             sample_rate_, channels_);
    return true;
}

std::optional<RawAudioFrame> MicCaptureWASAPI::capture_frame() {
    if (!initialized_) return std::nullopt;

    size_t target_total = static_cast<size_t>(frame_samples_) * channels_;

    while (accumulator_.size() < target_total) {
        UINT32 packet_size = 0;
        HRESULT hr = capture_client_->GetNextPacketSize(&packet_size);
        if (FAILED(hr) || packet_size == 0) {
            Sleep(1);
            continue;
        }

        BYTE* data = nullptr;
        UINT32 num_frames = 0;
        DWORD flags = 0;
        hr = capture_client_->GetBuffer(&data, &num_frames, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            size_t silence_count = static_cast<size_t>(num_frames) * device_channels_;
            accumulator_.insert(accumulator_.end(), silence_count, 0.0f);
        } else {
            const float* float_data = reinterpret_cast<const float*>(data);
            size_t sample_count = static_cast<size_t>(num_frames) * device_channels_;

            if (swr_ctx_) {
                int max_out = swr_get_out_samples(swr_ctx_, num_frames);
                std::vector<float> resampled(static_cast<size_t>(max_out) * channels_);
                uint8_t* out_buf[1] = { reinterpret_cast<uint8_t*>(resampled.data()) };
                const uint8_t* in_buf[1] = { reinterpret_cast<const uint8_t*>(float_data) };
                int converted = swr_convert(swr_ctx_, out_buf, max_out,
                                            in_buf, num_frames);
                if (converted > 0) {
                    resampled.resize(static_cast<size_t>(converted) * channels_);
                    accumulator_.insert(accumulator_.end(),
                                        resampled.begin(), resampled.end());
                }
            } else {
                accumulator_.insert(accumulator_.end(),
                                    float_data, float_data + sample_count);
            }
        }

        capture_client_->ReleaseBuffer(num_frames);
    }

    if (accumulator_.size() < target_total) return std::nullopt;

    RawAudioFrame frame;
    frame.sample_rate = sample_rate_;
    frame.channels = channels_;
    frame.num_samples = frame_samples_;
    frame.samples.assign(accumulator_.begin(),
                         accumulator_.begin() + target_total);
    accumulator_.erase(accumulator_.begin(),
                       accumulator_.begin() + target_total);

    Clock clock;
    frame.pts_us = clock.now_us();
    return frame;
}

void MicCaptureWASAPI::shutdown() {
    if (!initialized_) return;

    if (audio_client_) audio_client_->Stop();

    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }

    if (capture_client_) { capture_client_->Release(); capture_client_ = nullptr; }
    if (audio_client_) { audio_client_->Release(); audio_client_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }

    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }

    initialized_ = false;
    LOG_INFO(TAG, "WASAPI mic capture shut down");
}

} // namespace lancast

#endif // LANCAST_PLATFORM_WINDOWS
