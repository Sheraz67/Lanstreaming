#include "encode/audio_encoder.h"
#include "core/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <cstring>

namespace lancast {

static constexpr const char* TAG = "AudioEncoder";

AudioEncoder::~AudioEncoder() {
    shutdown();
}

bool AudioEncoder::init(uint32_t sample_rate, uint16_t channels, uint32_t bitrate) {
    sample_rate_ = sample_rate;
    channels_ = channels;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        LOG_ERROR(TAG, "Opus encoder not found");
        return false;
    }

    ctx_ = make_codec_context(codec);
    if (!ctx_) {
        LOG_ERROR(TAG, "Failed to allocate encoder context");
        return false;
    }

    ctx_->bit_rate = bitrate;
    ctx_->sample_rate = 48000; // Opus requires 48kHz
    ctx_->sample_fmt = AV_SAMPLE_FMT_FLT; // libopus accepts float32 interleaved
    ctx_->time_base = {1, 48000};
    ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_channel_layout_default(&ctx_->ch_layout, channels);

    if (avcodec_open2(ctx_.get(), codec, nullptr) < 0) {
        LOG_ERROR(TAG, "Failed to open Opus encoder");
        ctx_.reset();
        return false;
    }

    // Store extradata
    if (ctx_->extradata && ctx_->extradata_size > 0) {
        extradata_.assign(ctx_->extradata, ctx_->extradata + ctx_->extradata_size);
    }

    av_frame_ = make_frame();
    av_packet_ = make_packet();
    if (!av_frame_ || !av_packet_) {
        LOG_ERROR(TAG, "Failed to allocate frame/packet");
        shutdown();
        return false;
    }

    av_frame_->format = AV_SAMPLE_FMT_FLT;
    av_frame_->sample_rate = 48000;
    av_frame_->nb_samples = ctx_->frame_size; // Opus sets this (960 for 20ms)
    av_channel_layout_copy(&av_frame_->ch_layout, &ctx_->ch_layout);

    if (av_frame_get_buffer(av_frame_.get(), 0) < 0) {
        LOG_ERROR(TAG, "Failed to allocate audio frame buffer");
        shutdown();
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "Opus encoder initialized: %u Hz, %u ch, %u bps, frame_size %d",
             sample_rate, channels, bitrate, ctx_->frame_size);
    return true;
}

std::optional<EncodedPacket> AudioEncoder::encode(const RawAudioFrame& frame) {
    if (!initialized_) return std::nullopt;

    if (av_frame_make_writable(av_frame_.get()) < 0) {
        LOG_ERROR(TAG, "Failed to make frame writable");
        return std::nullopt;
    }

    // Copy interleaved float32 samples into AVFrame
    int frame_size = ctx_->frame_size;
    size_t samples_to_copy = static_cast<size_t>(frame_size) * channels_;
    size_t available = frame.samples.size();
    size_t copy_count = std::min(samples_to_copy, available);

    std::memcpy(av_frame_->data[0], frame.samples.data(), copy_count * sizeof(float));

    // Zero-pad if we have fewer samples than expected
    if (copy_count < samples_to_copy) {
        std::memset(
            reinterpret_cast<float*>(av_frame_->data[0]) + copy_count,
            0,
            (samples_to_copy - copy_count) * sizeof(float)
        );
    }

    av_frame_->pts = pts_;
    pts_ += frame_size;

    int ret = avcodec_send_frame(ctx_.get(), av_frame_.get());
    if (ret < 0) {
        LOG_ERROR(TAG, "Error sending frame to Opus encoder: %d", ret);
        return std::nullopt;
    }

    EncodedPacket result;
    while (true) {
        ret = avcodec_receive_packet(ctx_.get(), av_packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            LOG_ERROR(TAG, "Error receiving packet from Opus encoder: %d", ret);
            return std::nullopt;
        }

        result.data.insert(result.data.end(),
                           av_packet_->data, av_packet_->data + av_packet_->size);
        result.type = FrameType::Audio;
        result.pts_us = frame.pts_us;
        result.frame_id = frame_id_++;

        av_packet_unref(av_packet_.get());
    }

    if (result.data.empty()) return std::nullopt;

    LOG_DEBUG(TAG, "Encoded audio frame %u: %zu bytes", result.frame_id, result.data.size());
    return result;
}

void AudioEncoder::shutdown() {
    if (!initialized_) return;
    av_packet_.reset();
    av_frame_.reset();
    ctx_.reset();
    initialized_ = false;
    LOG_INFO(TAG, "Opus encoder shut down");
}

} // namespace lancast
