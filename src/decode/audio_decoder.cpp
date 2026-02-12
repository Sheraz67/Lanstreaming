#include "decode/audio_decoder.h"
#include "core/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <cstring>

namespace lancast {

static constexpr const char* TAG = "AudioDecoder";

AudioDecoder::~AudioDecoder() {
    shutdown();
}

bool AudioDecoder::init(uint32_t sample_rate, uint16_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        LOG_ERROR(TAG, "Opus decoder not found");
        return false;
    }

    ctx_ = make_codec_context(codec);
    if (!ctx_) {
        LOG_ERROR(TAG, "Failed to allocate decoder context");
        return false;
    }

    ctx_->sample_rate = static_cast<int>(sample_rate);
    av_channel_layout_default(&ctx_->ch_layout, channels);

    if (avcodec_open2(ctx_.get(), codec, nullptr) < 0) {
        LOG_ERROR(TAG, "Failed to open Opus decoder");
        ctx_.reset();
        return false;
    }

    av_frame_ = make_frame();
    av_packet_ = make_packet();
    if (!av_frame_ || !av_packet_) {
        LOG_ERROR(TAG, "Failed to allocate frame/packet");
        shutdown();
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "Opus decoder initialized: %u Hz, %u channels", sample_rate, channels);
    return true;
}

std::optional<RawAudioFrame> AudioDecoder::decode(const EncodedPacket& packet) {
    if (!initialized_ || packet.data.empty()) return std::nullopt;

    // Create padded buffer for FFmpeg
    std::vector<uint8_t> padded(packet.data.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    std::memcpy(padded.data(), packet.data.data(), packet.data.size());

    av_packet_->data = padded.data();
    av_packet_->size = static_cast<int>(packet.data.size());
    av_packet_->pts = packet.pts_us;

    int ret = avcodec_send_packet(ctx_.get(), av_packet_.get());
    if (ret < 0) {
        LOG_ERROR(TAG, "Error sending packet to Opus decoder: %d", ret);
        return std::nullopt;
    }

    std::optional<RawAudioFrame> result;
    while (true) {
        ret = avcodec_receive_frame(ctx_.get(), av_frame_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            LOG_ERROR(TAG, "Error receiving frame from Opus decoder: %d", ret);
            return std::nullopt;
        }

        int nb_samples = av_frame_->nb_samples;
        int out_channels = channels_;

        RawAudioFrame frame;
        frame.sample_rate = sample_rate_;
        frame.channels = channels_;
        frame.num_samples = static_cast<uint32_t>(nb_samples);
        frame.pts_us = packet.pts_us;
        frame.samples.resize(static_cast<size_t>(nb_samples) * out_channels);

        if (av_frame_->format == AV_SAMPLE_FMT_FLT) {
            // Already interleaved float32 — direct copy
            std::memcpy(frame.samples.data(), av_frame_->data[0],
                        frame.samples.size() * sizeof(float));
        } else {
            // Planar format (likely FLTP) — need SwrContext to interleave
            if (!swr_initialized_) {
                SwrContext* raw_swr = nullptr;
                AVChannelLayout out_layout{};
                av_channel_layout_default(&out_layout, out_channels);

                ret = swr_alloc_set_opts2(
                    &raw_swr,
                    &out_layout,
                    AV_SAMPLE_FMT_FLT,
                    static_cast<int>(sample_rate_),
                    &av_frame_->ch_layout,
                    static_cast<AVSampleFormat>(av_frame_->format),
                    av_frame_->sample_rate,
                    0, nullptr
                );

                av_channel_layout_uninit(&out_layout);

                if (ret < 0 || !raw_swr) {
                    LOG_ERROR(TAG, "Failed to configure SwrContext");
                    return std::nullopt;
                }

                swr_.reset(raw_swr);

                if (swr_init(swr_.get()) < 0) {
                    LOG_ERROR(TAG, "Failed to initialize SwrContext");
                    swr_.reset();
                    return std::nullopt;
                }

                swr_initialized_ = true;
            }

            uint8_t* out_buf = reinterpret_cast<uint8_t*>(frame.samples.data());
            const uint8_t** in_buf = const_cast<const uint8_t**>(av_frame_->data);

            int converted = swr_convert(swr_.get(),
                                        &out_buf, nb_samples,
                                        in_buf, nb_samples);
            if (converted < 0) {
                LOG_ERROR(TAG, "swr_convert failed: %d", converted);
                return std::nullopt;
            }

            frame.num_samples = static_cast<uint32_t>(converted);
            frame.samples.resize(static_cast<size_t>(converted) * out_channels);
        }

        result = std::move(frame);
        av_frame_unref(av_frame_.get());
    }

    if (result) {
        LOG_DEBUG(TAG, "Decoded audio: %u samples, %u Hz",
                  result->num_samples, result->sample_rate);
    }
    return result;
}

void AudioDecoder::shutdown() {
    if (!initialized_) return;
    swr_.reset();
    swr_initialized_ = false;
    av_packet_.reset();
    av_frame_.reset();
    ctx_.reset();
    initialized_ = false;
    LOG_INFO(TAG, "Opus decoder shut down");
}

} // namespace lancast
