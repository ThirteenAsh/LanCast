#pragma once

#include "Capture/DesktopCapturer.h"
#include "Encoder/H264Encoder.h"
#include "Decoder/H264Decoder.h"
#include "Network/NetworkManager.h"
#include "Discovery/RoomDiscovery.h"
#include "Common/FrameBuffer.h"
#include "Core/FrameQueue.h"
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <QObject>

namespace lancast {

// StreamEngine - core orchestrator for the streaming pipeline
//
// HOST mode:
//   Capture Thread -> Encode Thread -> Network Thread
//   (DesktopCapturer)  (H264Encoder)    (NetworkManager send)
//
// VIEWER mode:
//   Network Thread -> Depacketize -> Decode Thread -> Render (Qt signal)
//   (NetworkManager recv) (inline)      (H264Decoder)
//
class StreamEngine : public QObject {
    Q_OBJECT

public:
    enum class Mode { NONE, HOST, VIEWER };

    StreamEngine();
    ~StreamEngine();

    // Initialize as host (stream screen)
    bool startHost(const std::string& room_name, int width = 1920, int height = 1080, int fps = 30);

    // Stop host streaming
    void stopHost();

    // Initialize as viewer (receive stream)
    bool startViewer(const RoomInfo& room);

    // Stop viewer
    void stopViewer();

    // Get current mode
    Mode mode() const { return mode_; }

    // Get discovery service
    RoomDiscovery* discovery();

    // Get network manager
    NetworkManager* network() { return network_.get(); }

    // Get decoder (for viewer mode)
    H264Decoder* decoder() { return decoder_.get(); }

    // Get encoder (for host mode)
    H264Encoder* encoder() { return encoder_.get(); }

    // Get capturer (for host mode)
    DesktopCapturer* capturer() { return capturer_.get(); }

    // Set stream parameters
    void setBitrate(int kbps);
    void setFramerate(int fps);

signals:
    // Emitted when a new video frame is ready to display
    void newVideoFrame(const VideoFramePtr& frame);

    // Emitted when room list changes
    void roomsUpdated(const std::vector<RoomInfo>& rooms);

    // Emitted on errors
    void error(const std::string& message);

private:
    void captureThreadFunc();
    void encodeThreadFunc();
    void decodeThreadFunc();

    void startCaptureThreads();
    void stopCaptureThreads();

    void startDecodeThreads();
    void stopDecodeThreads();

    Mode mode_ = Mode::NONE;

    // Host components
    DesktopCapturerPtr capturer_;
    H264EncoderPtr encoder_;
    std::thread capture_thread_;
    std::thread encode_thread_;

    // Network
    NetworkManagerPtr network_;

    // Viewer components
    H264DecoderPtr decoder_;
    std::thread decode_thread_;

    // Discovery
    RoomDiscoveryPtr discovery_;

    // Frame queues
    VideoFrameQueue capture_queue_{30};
    EncodedFrameQueue encode_queue_{30};

    // Thread control
    std::atomic<bool> running_{false};

    // Settings
    int width_ = 1920;
    int height_ = 1080;
    int fps_ = 30;
    int bitrate_kbps_ = 2000;

    RoomInfo target_room_;
};

using StreamEnginePtr = std::shared_ptr<StreamEngine>;

}  // namespace lancast
