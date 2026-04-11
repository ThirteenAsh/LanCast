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

    // Convert Annex B to length-prefixed format for avparser
    // Or just feed directly with avcodec_send_input

    // Parse start codes and create packets
    const uint8_t* ptr = data;
    size_t remaining = len;

    while (remaining > 0) {
        // Find start code
        size_t offset = 0;
        while (offset + 4 <= remaining) {
            uint32_t val = (ptr[offset] << 24) | (ptr[offset+1] << 16) |
                           (ptr[offset+2] << 8) | ptr[offset+3];
            if (val == 0x00000001) {
                offset += 4;
                break;
            }
            offset++;
        }

        if (offset + 4 > remaining) break;

        // Find next start code
        size_t nal_end = offset;
        while (nal_end + 4 <= remaining) {
            uint32_t val = (ptr[nal_end] << 24) | (ptr[nal_end+1] << 16) |
                           (ptr[nal_end+2] << 8) | ptr[nal_end+3];
            if (val == 0x00000001) break;
            nal_end++;
        }

        size_t nal_size = nal_end - offset;

        // Create packet with length prefix
        av_packet_->data = (uint8_t*)ptr + offset - 4;  // Include length bytes
        av_packet_->size = (int)(nal_size + 4);

        // Set length bytes
        av_packet_->data[0] = (nal_size >> 24) & 0xFF;
        av_packet_->data[1] = (nal_size >> 16) & 0xFF;
        av_packet_->data[2] = (nal_size >> 8) & 0xFF;
        av_packet_->data[3] = nal_size & 0xFF;

        int ret = avcodec_send_packet(codec_ctx_, av_packet_);
        if (ret < 0) {
            av_packet_unref(av_packet_);
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx_, av_frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret >= 0) {
                auto frame = convertAvFrame(av_frame_);
                av_frame_unref(av_frame_);
                if (frame) {
                    frame->pts_ = pts;
                    return frame;
                }
            }
        }

        ptr += nal_end;
        remaining = len - (ptr - data);
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
