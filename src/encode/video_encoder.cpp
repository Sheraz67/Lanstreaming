#include "encode/video_encoder.h"
#include "core/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>

namespace lancast {

static constexpr const char* TAG = "VideoEncoder";

VideoEncoder::~VideoEncoder() {
    shutdown();
}

bool VideoEncoder::init(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate) {
    width_ = width;
    height_ = height;
    fps_ = fps;
    bitrate_ = bitrate;

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        LOG_ERROR(TAG, "libx264 encoder not found");
        return false;
    }

    ctx_ = make_codec_context(codec);
    if (!ctx_) {
        LOG_ERROR(TAG, "Failed to allocate encoder context");
        return false;
    }

    ctx_->width = static_cast<int>(width);
    ctx_->height = static_cast<int>(height);
    ctx_->time_base = {1, static_cast<int>(fps)};
    ctx_->framerate = {static_cast<int>(fps), 1};
    ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx_->bit_rate = bitrate;
    ctx_->rc_max_rate = bitrate;
    ctx_->rc_buffer_size = static_cast<int>(bitrate / 2);
    ctx_->gop_size = 60;
    ctx_->max_b_frames = 0;
    ctx_->thread_count = 4;
    ctx_->thread_type = FF_THREAD_SLICE;
    ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_opt_set(ctx_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(ctx_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(ctx_->priv_data, "forced-idr", "1", 0);

    if (avcodec_open2(ctx_.get(), codec, nullptr) < 0) {
        LOG_ERROR(TAG, "Failed to open encoder");
        ctx_.reset();
        return false;
    }

    // Store SPS/PPS extradata
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

    av_frame_->format = AV_PIX_FMT_YUV420P;
    av_frame_->width = static_cast<int>(width);
    av_frame_->height = static_cast<int>(height);

    if (av_frame_get_buffer(av_frame_.get(), 0) < 0) {
        LOG_ERROR(TAG, "Failed to allocate frame buffer");
        shutdown();
        return false;
    }

    initialized_ = true;
    LOG_INFO(TAG, "Encoder initialized: %ux%u @ %u fps, bitrate %u",
             width, height, fps, bitrate);
    return true;
}

std::optional<EncodedPacket> VideoEncoder::encode(const RawVideoFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return std::nullopt;

    if (av_frame_make_writable(av_frame_.get()) < 0) {
        LOG_ERROR(TAG, "Failed to make frame writable");
        return std::nullopt;
    }

    // Copy YUV planes from compact RawVideoFrame into FFmpeg's padded frame.
    // FFmpeg's linesize[] may be larger than width due to alignment padding.
    const uint8_t* src = frame.data.data();
    int w = static_cast<int>(width_);
    int h = static_cast<int>(height_);
    int half_w = w / 2;
    int half_h = h / 2;

    // Y plane
    int y_stride = av_frame_->linesize[0];
    for (int y = 0; y < h; ++y) {
        uint8_t* dst_row = av_frame_->data[0] + y * y_stride;
        std::memcpy(dst_row, src + y * w, w);
        if (y_stride > w)
            std::memset(dst_row + w, 0, y_stride - w);
    }
    // U plane
    const uint8_t* u_src = src + w * h;
    int u_stride = av_frame_->linesize[1];
    for (int y = 0; y < half_h; ++y) {
        uint8_t* dst_row = av_frame_->data[1] + y * u_stride;
        std::memcpy(dst_row, u_src + y * half_w, half_w);
        if (u_stride > half_w)
            std::memset(dst_row + half_w, 0, u_stride - half_w);
    }
    // V plane
    const uint8_t* v_src = u_src + half_w * half_h;
    int v_stride = av_frame_->linesize[2];
    for (int y = 0; y < half_h; ++y) {
        uint8_t* dst_row = av_frame_->data[2] + y * v_stride;
        std::memcpy(dst_row, v_src + y * half_w, half_w);
        if (v_stride > half_w)
            std::memset(dst_row + half_w, 0, v_stride - half_w);
    }

    av_frame_->pts = pts_++;

    // Force keyframe if requested
    if (force_keyframe_.exchange(false)) {
        av_frame_->pict_type = AV_PICTURE_TYPE_I;
    } else {
        av_frame_->pict_type = AV_PICTURE_TYPE_NONE;
    }

    int ret = avcodec_send_frame(ctx_.get(), av_frame_.get());
    if (ret < 0) {
        LOG_ERROR(TAG, "Error sending frame to encoder: %d", ret);
        return std::nullopt;
    }

    EncodedPacket result;
    while (true) {
        ret = avcodec_receive_packet(ctx_.get(), av_packet_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            LOG_ERROR(TAG, "Error receiving packet from encoder: %d", ret);
            return std::nullopt;
        }

        // Append NAL data (there's typically one packet per frame)
        result.data.insert(result.data.end(),
                           av_packet_->data, av_packet_->data + av_packet_->size);
        result.type = (av_packet_->flags & AV_PKT_FLAG_KEY)
                          ? FrameType::VideoKeyframe
                          : FrameType::VideoPFrame;
        result.pts_us = frame.pts_us;
        result.frame_id = frame_id_++;

        av_packet_unref(av_packet_.get());
    }

    if (result.data.empty()) return std::nullopt;

    LOG_DEBUG(TAG, "Encoded frame %u: %zu bytes, %s",
              result.frame_id, result.data.size(),
              result.type == FrameType::VideoKeyframe ? "keyframe" : "P-frame");
    return result;
}

void VideoEncoder::request_keyframe() {
    force_keyframe_.store(true);
    LOG_DEBUG(TAG, "Keyframe requested");
}

bool VideoEncoder::set_bitrate(uint32_t bitrate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bitrate == bitrate_) return true;
    if (!initialized_) return false;

    uint32_t w = width_, h = height_, fps = fps_;
    LOG_INFO(TAG, "Changing bitrate: %u -> %u", bitrate_, bitrate);

    shutdown();
    bool ok = init(w, h, fps, bitrate);
    if (ok) {
        request_keyframe();
    }
    return ok;
}

void VideoEncoder::shutdown() {
    if (!initialized_) return;
    av_packet_.reset();
    av_frame_.reset();
    ctx_.reset();
    initialized_ = false;
    LOG_INFO(TAG, "Encoder shut down");
}

} // namespace lancast
