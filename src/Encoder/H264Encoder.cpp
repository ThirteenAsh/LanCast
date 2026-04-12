#include "Encoder/H264Encoder.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <cstring>

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

namespace lancast {

H264Encoder::H264Encoder() = default;

H264Encoder::~H264Encoder() {
    shutdown();
}

bool H264Encoder::initialize(int width, int height, int fps, int bitrate_kbps) {
    width_ = width;
    height_ = height;
    fps_ = fps;
    bitrate_kbps_ = bitrate_kbps;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        return false;
    }

    // Encoder parameters
    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx_->time_base = {1, fps_};
    codec_ctx_->framerate = {fps_, 1};
    codec_ctx_->bit_rate = bitrate_kbps_ * 1000;
    codec_ctx_->gop_size = fps_ * 2;  // Keyframe every 2 seconds
    codec_ctx_->max_b_frames = 0;     // No B-frames for low latency
    codec_ctx_->thread_count = 4;

    // Rate control
    codec_ctx_->rc_buffer_size = bitrate_kbps_ * 1000;
    codec_ctx_->rc_max_rate = bitrate_kbps_ * 1000;
    codec_ctx_->rc_min_rate = bitrate_kbps_ * 500;

    // Profile for compatibility
    av_opt_set(codec_ctx_->priv_data, "profile", "baseline", 0);
    av_opt_set(codec_ctx_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(codec_ctx_->priv_data, "x264-params", "annexb=1:repeat-headers=1", 0);

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    // Allocate frame
    av_frame_ = av_frame_alloc();
    if (!av_frame_) {
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    av_frame_->format = codec_ctx_->pix_fmt;
    av_frame_->width = width_;
    av_frame_->height = height_;
    av_frame_->linesize[0] = width_;
    av_frame_->linesize[1] = width_ / 2;
    av_frame_->linesize[2] = width_ / 2;

    // Allocate frame buffer
    if (av_frame_get_buffer(av_frame_, 16) < 0) {
        av_frame_free(&av_frame_);
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    // Allocate packet
    av_packet_ = av_packet_alloc();
    if (!av_packet_) {
        av_frame_free(&av_frame_);
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    // Get SPS/PPS from the opened encoder
    if (codec_ctx_->extradata_size > 0) {
        sps_pps_.assign(codec_ctx_->extradata,
                        codec_ctx_->extradata + codec_ctx_->extradata_size);
    }

    initialized_ = true;
    return true;
}

EncodedFramePtr H264Encoder::encode(const VideoFramePtr& frame) {
    if (!initialized_ || !frame) {
        return nullptr;
    }

    // Fill AVFrame with YUV data
    // Y plane
    uint8_t* dst_y = av_frame_->data[0];
    for (int i = 0; i < height_; ++i) {
        memcpy(dst_y + i * av_frame_->linesize[0],
               frame->yData() + i * width_, width_);
    }

    // U plane
    uint8_t* dst_u = av_frame_->data[1];
    for (int i = 0; i < height_ / 2; ++i) {
        memcpy(dst_u + i * av_frame_->linesize[1],
               frame->uData() + i * (width_ / 2), width_ / 2);
    }

    // V plane
    uint8_t* dst_v = av_frame_->data[2];
    for (int i = 0; i < height_ / 2; ++i) {
        memcpy(dst_v + i * av_frame_->linesize[2],
               frame->vData() + i * (width_ / 2), width_ / 2);
    }

    // PTS
    av_frame_->pts = frame_count_ * 90000 / fps_;  // 90kHz clock
    av_frame_->pict_type = AV_PICTURE_TYPE_NONE;

    // Force keyframe on first frame and if requested
    bool is_key = force_keyframe_ || frame->key_frame_;
    if (is_key) {
        av_frame_->pict_type = AV_PICTURE_TYPE_I;
        av_frame_->flags |= AV_FRAME_FLAG_KEY;
        force_keyframe_ = false;
    }

    // Send frame to encoder
    int ret = avcodec_send_frame(codec_ctx_, av_frame_);
    if (ret < 0) {
        return nullptr;
    }

    auto encoded = std::make_shared<EncodedFrame>();
    encoded->pts_ = frame->pts_;
    encoded->key_frame_ = is_key;

    // Receive packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, av_packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            return nullptr;
        }

        const uint8_t* data = av_packet_->data;
        const size_t size = static_cast<size_t>(av_packet_->size);

        const bool has_annexb_start =
            (size >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
             ((data[2] == 0x01) || (data[2] == 0x00 && data[3] == 0x01)));

        if (has_annexb_start) {
            // Most software H.264 encoders output Annex B already.
            encoded->data_.insert(encoded->data_.end(), data, data + size);
        } else {
            // Fallback: convert AVCC length-prefixed NALs to Annex B.
            const uint8_t* ptr = data;
            const uint8_t* end = data + size;

            while (ptr + 4 <= end) {
                uint32_t nal_size = (static_cast<uint32_t>(ptr[0]) << 24) |
                                    (static_cast<uint32_t>(ptr[1]) << 16) |
                                    (static_cast<uint32_t>(ptr[2]) << 8) |
                                    static_cast<uint32_t>(ptr[3]);
                ptr += 4;

                if (ptr + nal_size > end) {
                    break;
                }

                encoded->data_.push_back(0x00);
                encoded->data_.push_back(0x00);
                encoded->data_.push_back(0x00);
                encoded->data_.push_back(0x01);
                encoded->data_.insert(encoded->data_.end(), ptr, ptr + nal_size);

                ptr += nal_size;
            }
        }

        if ((av_packet_->flags & AV_PKT_FLAG_KEY) != 0) {
            encoded->key_frame_ = true;
        }

        av_packet_unref(av_packet_);
    }

    frame_count_++;
    return encoded;
}

void H264Encoder::shutdown() {
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

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    initialized_ = false;
}

}  // namespace lancast
