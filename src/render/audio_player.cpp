#include "render/audio_player.h"
#include "core/logger.h"

#include <SDL3/SDL.h>

namespace lancast {

static constexpr const char* TAG = "AudioPlayer";
static constexpr int MAX_QUEUED_MS = 200;

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::init(uint32_t sample_rate, uint16_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_ERROR(TAG, "SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
            return false;
        }
    }

    SDL_AudioSpec src_spec{};
    src_spec.format = SDL_AUDIO_F32;
    src_spec.channels = static_cast<int>(channels);
    src_spec.freq = static_cast<int>(sample_rate);

    stream_ = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &src_spec,
        nullptr,  // no callback (push model)
        nullptr
    );

    if (!stream_) {
        LOG_ERROR(TAG, "SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }

    SDL_ResumeAudioStreamDevice(stream_);

    initialized_ = true;
    LOG_INFO(TAG, "Audio player initialized: %u Hz, %u channels", sample_rate, channels);
    return true;
}

void AudioPlayer::play_frame(const RawAudioFrame& frame) {
    if (!initialized_ || frame.samples.empty()) return;

    // Check queued data to avoid excessive buffering
    int queued_bytes = SDL_GetAudioStreamQueued(stream_);
    int bytes_per_ms = static_cast<int>(sample_rate_) * static_cast<int>(channels_) *
                       static_cast<int>(sizeof(float)) / 1000;
    int max_queued_bytes = bytes_per_ms * MAX_QUEUED_MS;

    if (queued_bytes > max_queued_bytes) {
        LOG_DEBUG(TAG, "Audio buffer full (%d bytes queued), skipping frame", queued_bytes);
        return;
    }

    int size = static_cast<int>(frame.samples.size() * sizeof(float));
    if (!SDL_PutAudioStreamData(stream_, frame.samples.data(), size)) {
        LOG_ERROR(TAG, "SDL_PutAudioStreamData failed: %s", SDL_GetError());
    }
}

void AudioPlayer::shutdown() {
    if (!initialized_) return;

    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }

    initialized_ = false;
    LOG_INFO(TAG, "Audio player shut down");
}

} // namespace lancast
