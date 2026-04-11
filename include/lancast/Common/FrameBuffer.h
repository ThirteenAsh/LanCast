#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace lancast {

// YUV420P video frame from capture or after decode
struct VideoFrame {
    int width_ = 0;
    int height_ = 0;
    int64_t pts_ = 0;  // Presentation timestamp in microseconds
    std::vector<uint8_t> data_;  // YUV420P planar data
    bool key_frame_ = false;

    VideoFrame() = default;

    VideoFrame(int w, int h, int64_t pts)
        : width_(w), height_(h), pts_(pts), key_frame_(false) {
        // YUV420P: Y plane (w*h) + U plane (w*h/4) + V plane (w*h/4)
        data_.resize(width_ * height_ * 3 / 2);
    }

    size_t ySize() const { return width_ * height_; }
    size_t uvSize() const { return width_ * height_ / 4; }
    uint8_t* yData() { return data_.data(); }
    uint8_t* uData() { return data_.data() + ySize(); }
    uint8_t* vData() { return data_.data() + ySize() + uvSize(); }
    const uint8_t* yData() const { return data_.data(); }
    const uint8_t* uData() const { return data_.data() + ySize(); }
    const uint8_t* vData() const { return data_.data() + ySize() + uvSize(); }
};

using VideoFramePtr = std::shared_ptr<VideoFrame>;

// Encoded H.264 frame data
struct EncodedFrame {
    int64_t pts_ = 0;
    int64_t dts_ = 0;
    bool key_frame_ = false;
    std::vector<uint8_t> data_;  // Raw H.264 NAL units (Annex B)

    // Access UnitDelimiter + SPS + PPS + Slice + ...
};

using EncodedFramePtr = std::shared_ptr<EncodedFrame>;

}  // namespace lancast
