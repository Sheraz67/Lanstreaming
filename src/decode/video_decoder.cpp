#include "decode/video_decoder.h"
#include "core/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <cstring>

namespace lancast {

static constexpr const char* TAG = "VideoDecoder";

VideoDecoder::~VideoDecoder() {
    shutdown();
}

bool VideoDecoder::init(uint32_t width, uint32_t height, const std::vector<uint8_t>& extradata) {
    width_ = width;
    height_ = height;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG_ERROR(TAG, "H.264 decoder not found");
        return false;
    }

    ctx_ = make_codec_context(codec);
    if (!ctx_) {
        LOG_ERROR(TAG, "Failed to allocate decoder context");
        return false;
    }

    ctx_->width = static_cast<int>(width);
    ctx_->height = static_cast<int>(height);
    ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx_->thread_count = 4;
    ctx_->thread_type = FF_THREAD_SLICE;

    // Set extradata (SPS/PPS) with required padding
    if (!extradata.empty()) {
        ctx_->extradata_size = static_cast<int>(extradata.size());
        ctx_->extradata = static_cast<uint8_t*>(
            av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!ctx_->extradata) {
            LOG_ERROR(TAG, "Failed to allocate extradata");
            ctx_.reset();
            return false;
        }
        std::memcpy(ctx_->extradata, extradata.data(), extradata.size());
    }

    if (avcodec_open2(ctx_.get(), codec, nullptr) < 0) {
        LOG_ERROR(TAG, "Failed to open decoder");
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
    LOG_INFO(TAG, "Decoder initialized: %ux%u", width, height);
    return true;
}

std::optional<RawVideoFrame> VideoDecoder::decode(const EncodedPacket& packet) {
    if (!initialized_ || packet.data.empty()) return std::nullopt;

    // Create padded buffer for FFmpeg
    std::vector<uint8_t> padded(packet.data.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    std::memcpy(padded.data(), packet.data.data(), packet.data.size());

    av_packet_->data = padded.data();
    av_packet_->size = static_cast<int>(packet.data.size());
    av_packet_->pts = packet.pts_us;

    int ret = avcodec_send_packet(ctx_.get(), av_packet_.get());
    if (ret < 0) {
        LOG_ERROR(TAG, "Error sending packet to decoder: %d", ret);
        return std::nullopt;
    }

    std::optional<RawVideoFrame> result;
    while (true) {
        ret = avcodec_receive_frame(ctx_.get(), av_frame_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            LOG_ERROR(TAG, "Error receiving frame from decoder: %d", ret);
            return std::nullopt;
        }

        // Dimensions should always be even for YUV420P
        int w = av_frame_->width & ~1;
        int h = av_frame_->height & ~1;
        int half_w = w / 2;
        int half_h = h / 2;

        RawVideoFrame frame;
        frame.width = static_cast<uint32_t>(w);
        frame.height = static_cast<uint32_t>(h);
        frame.pts_us = packet.pts_us;
        frame.data.resize(w * h + 2 * half_w * half_h);

        // Copy Y plane row-by-row (linesize may differ from width)
        uint8_t* dst = frame.data.data();
        for (int y = 0; y < h; ++y) {
            std::memcpy(dst + y * w,
                        av_frame_->data[0] + y * av_frame_->linesize[0], w);
        }
        // Copy U plane
        uint8_t* u_dst = dst + w * h;
        for (int y = 0; y < half_h; ++y) {
            std::memcpy(u_dst + y * half_w,
                        av_frame_->data[1] + y * av_frame_->linesize[1], half_w);
        }
        // Copy V plane
        uint8_t* v_dst = u_dst + half_w * half_h;
        for (int y = 0; y < half_h; ++y) {
            std::memcpy(v_dst + y * half_w,
                        av_frame_->data[2] + y * av_frame_->linesize[2], half_w);
        }

        result = std::move(frame);
        av_frame_unref(av_frame_.get());
    }

    if (result) {
        LOG_DEBUG(TAG, "Decoded frame: %ux%u, %zu bytes",
                  result->width, result->height, result->data.size());
    }
    return result;
}

void VideoDecoder::shutdown() {
    if (!initialized_) return;
    av_packet_.reset();
    av_frame_.reset();
    ctx_.reset();
    initialized_ = false;
    LOG_INFO(TAG, "Decoder shut down");
}

} // namespace lancast
