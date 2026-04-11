#pragma once

#include "Common/FrameBuffer.h"
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <string>

namespace lancast {

// DXGI-based screen capturer using Desktop Duplication API
class DesktopCapturer {
public:
    DesktopCapturer();
    ~DesktopCapturer();

    // Initialize capture for a specific output (adapter/output index)
    bool initialize(int output_width = 1920, int output_height = 1080, int fps = 30);

    // Capture a single frame - returns nullptr if no new frame available
    VideoFramePtr captureFrame();

    // Shutdown and release resources
    void shutdown();

    // Get capture dimensions
    int width() const { return width_; }
    int height() const { return height_; }
    int fps() const { return target_fps_; }

private:
    bool initD3D();
    bool initDuplication();
    void releaseResources();

    // Convert BGRA (DXGI) to YUV420P
    void convertBgraToYuv420(const uint8_t* bgra, int row_pitch, VideoFramePtr& frame);

    // D3D11 resources
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIOutput* output_ = nullptr;
    IDXGIOutput1* output1_ = nullptr;
    IDXGIOutputDuplication* duplication_ = nullptr;
    ID3D11Texture2D* staging_texture_ = nullptr;

    // Capture state
    int width_ = 0;
    int height_ = 0;
    int target_fps_ = 30;
    int frame_interval_ms_;  // = 1000 / fps

    // Frame timing
    int64_t last_frame_time_ = 0;
    bool initialized_ = false;

    // Adapter/Output indices
    int adapter_idx_ = 0;
    int output_idx_ = 0;
};

using DesktopCapturerPtr = std::shared_ptr<DesktopCapturer>;

}  // namespace lancast
