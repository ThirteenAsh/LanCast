#pragma once

#include "Common/FrameBuffer.h"
#include <memory>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace lancast {

// FFmpeg H.264 encoder wrapper
class H264Encoder {
public:
    H264Encoder();
    ~H264Encoder();

    // Initialize encoder
    bool initialize(int width, int height, int fps, int bitrate_kbps = 2000);

    // Encode a video frame - returns encoded data (NAL units in Annex B format)
    EncodedFramePtr encode(const VideoFramePtr& frame);

    // Get SPS/PPS for stream initialization (call after initialize)
    std::vector<uint8_t> getSpsPps() const { return sps_pps_; }

    // Shutdown encoder
    void shutdown();

    bool isInitialized() const { return initialized_; }

private:
    void convertYuvToAvFrame(const VideoFramePtr& frame);
    void writeAnnexB(std::vector<uint8_t>& out, const uint8_t* data, size_t len, int nal_type);

    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* av_frame_ = nullptr;
    AVPacket* av_packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    int fps_ = 30;
    int bitrate_kbps_ = 2000;

    bool initialized_ = false;
    std::vector<uint8_t> sps_pps_;
    int frame_count_ = 0;

    // For forced keyframe
    bool force_keyframe_ = true;  // First frame is always keyframe
};

using H264EncoderPtr = std::shared_ptr<H264Encoder>;

}  // namespace lancast
