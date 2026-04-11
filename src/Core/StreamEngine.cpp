#include "Core/StreamEngine.h"
#include <chrono>
#include <random>

namespace lancast {

StreamEngine::StreamEngine() = default;

StreamEngine::~StreamEngine() {
    stopHost();
    stopViewer();
}

bool StreamEngine::startHost(const std::string& room_name, int width, int height, int fps) {
    if (mode_ != Mode::NONE) {
        stopHost();
    }

    width_ = width;
    height_ = height;
    fps_ = fps;

    // Initialize capture
    capturer_ = std::make_shared<DesktopCapturer>();
    if (!capturer_->initialize(width_, height_, fps_)) {
        emit error("Failed to initialize screen capture");
        return false;
    }

    // Initialize encoder
    encoder_ = std::make_shared<H264Encoder>();
    if (!encoder_->initialize(width_, height_, fps_, bitrate_kbps_)) {
        emit error("Failed to initialize H.264 encoder");
        capturer_.reset();
        return false;
    }

    // Get our IP for the stream port
    // Use a random port for streaming
    uint16_t stream_port = 50000 + (rand() % 10000);

    // Initialize network as sender
    network_ = std::make_shared<NetworkManager>();
    // Get broadcast address (last octet = 255 for /24 network)
    // For simplicity, use the target from discovery or default
    // In host mode, we need to know where to send - for now we start without destination
    // The viewer will connect and tell us its address

    // Initialize discovery and start broadcasting
    discovery_ = std::make_shared<RoomDiscovery>();

    // Generate room ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    char room_id_buf[32];
    snprintf(room_id_buf, sizeof(room_id_buf), "%08x", dist(gen));
    std::string room_id = room_id_buf;

    if (!discovery_->startBroadcast(room_id, room_name, stream_port)) {
        emit error("Failed to start room discovery");
        encoder_.reset();
        capturer_.reset();
        return false;
    }

    // Start threads
    running_ = true;
    mode_ = Mode::HOST;
    startCaptureThreads();

    return true;
}

void StreamEngine::stopHost() {
    if (mode_ != Mode::HOST) return;

    running_ = false;
    stopCaptureThreads();

    if (discovery_) {
        discovery_->stopBroadcast();
        discovery_.reset();
    }

    network_.reset();
    encoder_.reset();
    capturer_.reset();

    mode_ = Mode::NONE;
}

bool StreamEngine::startViewer(const RoomInfo& room) {
    if (mode_ != Mode::NONE) {
        stopViewer();
    }

    target_room_ = room;

    // Initialize decoder
    decoder_ = std::make_shared<H264Decoder>();
    if (!decoder_->initialize(room.stream_port_ ? 1920 : 1920, 1080)) {
        // Try with any size, decoder will adapt
        emit error("Failed to initialize H.264 decoder");
        return false;
    }

    // Initialize network as receiver
    network_ = std::make_shared<NetworkManager>();
    if (!network_->initReceiver(50000 + (rand() % 10000))) {
        emit error("Failed to initialize network receiver");
        decoder_.reset();
        return false;
    }

    running_ = true;
    mode_ = Mode::VIEWER;
    startDecodeThreads();

    return true;
}

void StreamEngine::stopViewer() {
    if (mode_ != Mode::VIEWER) return;

    running_ = false;
    stopDecodeThreads();

    network_.reset();
    decoder_.reset();

    mode_ = Mode::NONE;
}

void StreamEngine::startCaptureThreads() {
    capture_thread_ = std::thread(&StreamEngine::captureThreadFunc, this);
    encode_thread_ = std::thread(&StreamEngine::encodeThreadFunc, this);
}

void StreamEngine::stopCaptureThreads() {
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }
}

void StreamEngine::startDecodeThreads() {
    decode_thread_ = std::thread(&StreamEngine::decodeThreadFunc, this);
}

void StreamEngine::stopDecodeThreads() {
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
}

void StreamEngine::captureThreadFunc() {
    using namespace std::chrono;

    auto last_capture = steady_clock::now();
    int frame_interval_ms = 1000 / fps_;

    while (running_) {
        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - last_capture).count();

        if (elapsed >= frame_interval_ms) {
            if (capturer_) {
                auto frame = capturer_->captureFrame();
                if (frame) {
                    capture_queue_.push(frame, 10);  // 10ms timeout
                }
            }
            last_capture = now;
        }

        std::this_thread::sleep_for(milliseconds(1));
    }
}

void StreamEngine::encodeThreadFunc() {
    while (running_) {
        VideoFramePtr frame;
        if (capture_queue_.pop(frame, 100)) {
            if (encoder_ && frame) {
                auto encoded = encoder_->encode(frame);
                if (encoded && network_) {
                    // Send to all viewers who have requested this stream
                    // In a real implementation, we'd maintain a list of connected viewers
                    // For now, the network doesn't have a target yet
                }
            }
        }
    }
}

void StreamEngine::decodeThreadFunc() {
    while (running_) {
        if (network_) {
            auto encoded = network_->tryGetFrame();
            if (encoded && decoder_) {
                auto frame = decoder_->decode(encoded);
                if (frame) {
                    emit newVideoFrame(frame);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void StreamEngine::setBitrate(int kbps) {
    bitrate_kbps_ = kbps;
    if (encoder_) {
        // Encoder bitrate is set at initialization, would need reinit for change
    }
}

void StreamEngine::setFramerate(int fps) {
    fps_ = fps;
    if (capturer_) {
        // Capturer frame rate is set at initialization
    }
}

}  // namespace lancast
