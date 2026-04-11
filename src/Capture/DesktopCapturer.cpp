#include "Capture/DesktopCapturer.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace lancast {

DesktopCapturer::DesktopCapturer()
    : frame_interval_ms_(1000 / 30) {
}

DesktopCapturer::~DesktopCapturer() {
    shutdown();
}

bool DesktopCapturer::initialize(int output_width, int output_height, int fps) {
    width_ = output_width;
    height_ = output_height;
    target_fps_ = fps;
    frame_interval_ms_ = 1000 / fps;

    if (!initD3D()) {
        return false;
    }

    if (!initDuplication()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool DesktopCapturer::initD3D() {
    HRESULT hr;

    // Create D3D11 device
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    IDXGIAdapter1* adapter = nullptr;
    IDXGIFactory1* factory = nullptr;

    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) {
        return false;
    }

    // Find the adapter with the specified output
    for (int i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        // Try to create device on this adapter
        ID3D11Device* temp_device = nullptr;
        ID3D11DeviceContext* temp_context = nullptr;
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               device_flags, feature_levels, 3,
                               D3D11_SDK_VERSION, &temp_device,
                               nullptr, &temp_context);

        if (SUCCEEDED(hr)) {
            device_ = temp_device;
            context_ = temp_context;
            adapter_idx_ = i;

            // Get output 0 (primary display)
            hr = adapter->EnumOutputs(0, &output_);
            if (FAILED(hr)) {
                adapter->Release();
                factory->Release();
                return false;
            }

            hr = output_->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1_);
            if (FAILED(hr)) {
                adapter->Release();
                factory->Release();
                return false;
            }

            adapter->Release();
            factory->Release();
            return true;
        }

        adapter->Release();
    }

    factory->Release();
    return false;
}

bool DesktopCapturer::initDuplication() {
    HRESULT hr = output1_->DuplicateOutput(device_, &duplication_);
    if (FAILED(hr)) {
        // DUPL_OUT_OF_MEMORY or DUPL_UNSUPPORTED_DEVICE are common failures
        return false;
    }

    // Create staging texture for ReadFromTexture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr2 = device_->CreateTexture2D(&desc, nullptr, &staging_texture_);
    if (FAILED(hr2)) {
        return false;
    }

    return true;
}

void DesktopCapturer::shutdown() {
    releaseResources();

    if (staging_texture_) {
        staging_texture_->Release();
        staging_texture_ = nullptr;
    }

    if (duplication_) {
        duplication_->Release();
        duplication_ = nullptr;
    }

    if (output1_) {
        output1_->Release();
        output1_ = nullptr;
    }

    if (output_) {
        output_->Release();
        output_ = nullptr;
    }

    if (context_) {
        context_->Release();
        context_ = nullptr;
    }

    if (device_) {
        device_->Release();
        device_ = nullptr;
    }

    initialized_ = false;
}

void DesktopCapturer::releaseResources() {
    if (duplication_) {
        duplication_->ReleaseFrame();
    }
}

VideoFramePtr DesktopCapturer::captureFrame() {
    if (!initialized_) {
        return nullptr;
    }

    // Rate limiting
    auto now = GetTickCount64();
    if (now - last_frame_time_ < frame_interval_ms_) {
        return nullptr;
    }

    DXGI_OUTDUPL_FRAME_INFO frame_info;
    IDXGIResource* resource = nullptr;
    HRESULT hr = duplication_->AcquireNextFrame(0, &frame_info, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return nullptr;
    }

    if (FAILED(hr)) {
        // Try to re-acquire
        releaseResources();
        initDuplication();
        return nullptr;
    }

    // Get the desktop surface
    ID3D11Texture2D* desktop_texture = nullptr;
    hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktop_texture);
    resource->Release();

    if (FAILED(hr) || !desktop_texture) {
        duplication_->ReleaseFrame();
        return nullptr;
    }

    // Copy to staging texture
    D3D11_TEXTURE2D_DESC desc;
    desktop_texture->GetDesc(&desc);

    // Ensure staging texture matches
    if (desc.Width != width_ || desc.Height != height_) {
        // Recreate staging texture if needed
        if (staging_texture_) {
            staging_texture_->Release();
        }
        desc.Width = width_;
        desc.Height = height_;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        device_->CreateTexture2D(&desc, nullptr, &staging_texture_);
    }

    context_->CopyResource(staging_texture_, desktop_texture);
    desktop_texture->Release();

    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(staging_texture_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return nullptr;
    }

    // Create video frame
    auto frame = std::make_shared<VideoFrame>(width_, height_, now * 1000);  // pts in microseconds
    convertBgraToYuv420(static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch, frame);

    context_->Unmap(staging_texture_, 0);
    duplication_->ReleaseFrame();

    last_frame_time_ = now;
    return frame;
}

void DesktopCapturer::convertBgraToYuv420(const uint8_t* bgra, int row_pitch, VideoFramePtr& frame) {
    uint8_t* y = frame->yData();
    uint8_t* u = frame->uData();
    uint8_t* v = frame->vData();

    for (int h = 0; h < height_; ++h) {
        const uint32_t* src_row = reinterpret_cast<const uint32_t*>(bgra + h * row_pitch);
        for (int w = 0; w < width_; ++w) {
            uint32_t pixel = src_row[w];
            uint8_t b = pixel & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t r = (pixel >> 16) & 0xFF;

            // Y plane
            int y_val = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y[h * width_ + w] = static_cast<uint8_t>(std::clamp(y_val, 0, 255));

            // U and V are subsampled 2x2
            if ((h & 1) == 0 && (w & 1) == 0) {
                int u_val = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v_val = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                size_t uv_idx = (h / 2) * (width_ / 2) + (w / 2);
                u[uv_idx] = static_cast<uint8_t>(std::clamp(u_val, 0, 255));
                v[uv_idx] = static_cast<uint8_t>(std::clamp(v_val, 0, 255));
            }
        }
    }
}

}  // namespace lancast
