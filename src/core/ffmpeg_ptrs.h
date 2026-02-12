#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

namespace lancast {

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) av_frame_free(&frame);
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) av_packet_free(&pkt);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* swr) const {
        if (swr) swr_free(&swr);
    }
};

using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

inline AVCodecContextPtr make_codec_context(const AVCodec* codec) {
    return AVCodecContextPtr(avcodec_alloc_context3(codec));
}

inline AVFramePtr make_frame() {
    return AVFramePtr(av_frame_alloc());
}

inline AVPacketPtr make_packet() {
    return AVPacketPtr(av_packet_alloc());
}

inline SwrContextPtr make_swr_context() {
    return SwrContextPtr(swr_alloc());
}

} // namespace lancast
