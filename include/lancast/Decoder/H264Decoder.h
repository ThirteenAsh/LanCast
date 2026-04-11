#pragma once

#include "Common/FrameBuffer.h"
#include <memory>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace lancast {

// FFmpeg H.264 decoder wrapper
class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    // Initialize decoder
    bool initialize(int width, int height);

    // Decode an encoded frame - returns decoded video frame
    VideoFramePtr decode(const EncodedFramePtr& frame);

    // Decode from raw H.264 data (Annex B)
    VideoFramePtr decodeAnnexB(const uint8_t* data, size_t len, int64_t pts);

    // Shutdown decoder
    void shutdown();

    bool isInitialized() const { return initialized_; }

private:
    // Send packet to decoder
    bool sendPacket(const AVPacket* packet);

    // Receive frame from decoder
    VideoFramePtr receiveFrame();

    // Convert decoded AVFrame to VideoFrame
    VideoFramePtr convertAvFrame(const AVFrame* av_frame);

    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* av_frame_ = nullptr;
    AVPacket* av_packet_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    int64_t frame_count_ = 0;
};

using H264DecoderPtr = std::shared_ptr<H264Decoder>;

}  // namespace lancast
