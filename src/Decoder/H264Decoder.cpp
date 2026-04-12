#include "Decoder/H264Decoder.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")

namespace lancast {

H264Decoder::H264Decoder() = default;

H264Decoder::~H264Decoder() {
    shutdown();
}

bool H264Decoder::initialize(int width, int height) {
    width_ = width;
    height_ = height;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        return false;
    }

    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx_->thread_count = 4;
    codec_ctx_->flags2 |= AV_CODEC_FLAG2_FAST;  // Allow faster decoding

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        return false;
    }

    av_frame_ = av_frame_alloc();
    if (!av_frame_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        return false;
    }

    av_packet_ = av_packet_alloc();
    if (!av_packet_) {
        av_frame_free(&av_frame_);
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
        return false;
    }

    initialized_ = true;
    return true;
}

VideoFramePtr H264Decoder::decode(const EncodedFramePtr& frame) {
    if (!initialized_ || !frame) {
        return nullptr;
    }

    return decodeAnnexB(frame->data_.data(), frame->data_.size(), frame->pts_);
}

VideoFramePtr H264Decoder::decodeAnnexB(const uint8_t* data, size_t len, int64_t pts) {
    if (!initialized_) {
        return nullptr;
    }

    if (!data || len == 0) {
        return nullptr;
    }

    av_packet_unref(av_packet_);
    av_packet_->data = const_cast<uint8_t*>(data);
    av_packet_->size = static_cast<int>(len);
    av_packet_->pts = pts;

    int ret = avcodec_send_packet(codec_ctx_, av_packet_);
    if (ret < 0) {
        return nullptr;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(codec_ctx_, av_frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return nullptr;
        }

        auto frame = convertAvFrame(av_frame_);
        av_frame_unref(av_frame_);
        if (frame) {
            frame->pts_ = pts;
            return frame;
        }
    }

    return nullptr;
}

VideoFramePtr H264Decoder::convertAvFrame(const AVFrame* av_frame) {
    if (!av_frame || !av_frame->data[0]) {
        return nullptr;
    }

    auto frame = std::make_shared<VideoFrame>(av_frame->width, av_frame->height, 0);

    // Copy Y plane
    for (int i = 0; i < av_frame->height; ++i) {
        memcpy(frame->yData() + i * frame->width_,
               av_frame->data[0] + i * av_frame->linesize[0],
               frame->width_);
    }

    // Copy U plane
    for (int i = 0; i < av_frame->height / 2; ++i) {
        memcpy(frame->uData() + i * (frame->width_ / 2),
               av_frame->data[1] + i * av_frame->linesize[1],
               frame->width_ / 2);
    }

    // Copy V plane
    for (int i = 0; i < av_frame->height / 2; ++i) {
        memcpy(frame->vData() + i * (frame->width_ / 2),
               av_frame->data[2] + i * av_frame->linesize[2],
               frame->width_ / 2);
    }

    return frame;
}

void H264Decoder::shutdown() {
    if (av_packet_) {
        av_packet_free(&av_packet_);
        av_packet_ = nullptr;
    }

    if (av_frame_) {
        av_frame_free(&av_frame_);
        av_frame_ = nullptr;
    }

    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }

    initialized_ = false;
}

}  // namespace lancast
