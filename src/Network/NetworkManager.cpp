#include "Network/NetworkManager.h"
#include <chrono>
#include <random>

namespace lancast {

NetworkManager::NetworkManager()
    : received_queue_(RECEIVE_QUEUE_SIZE)
    , ssrc_(0)
    , timestamp_(0)
    , seq_num_(0) {
    // Generate random SSRC
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    ssrc_ = dist(gen);
}

NetworkManager::~NetworkManager() {
    shutdown();
}

bool NetworkManager::initSender(const std::string& target_ip, uint16_t target_port) {
    target_ip_ = target_ip;
    target_port_ = target_port;

    send_socket_ = std::make_shared<UdpSocket>();
    if (!send_socket_->bind(0)) {  // Bind to any available port
        return false;
    }

    // Initialize packetizer
    packetizer_ = std::make_shared<RtpPacketizer>();
    packetizer_->setSsrc(ssrc_);
    packetizer_->setPayloadType(96);
    timestamp_ = 0;
    seq_num_ = 0;

    mode_ = Mode::SENDER;
    return true;
}

bool NetworkManager::initReceiver(uint16_t local_port) {
    local_port_ = local_port;

    recv_socket_ = std::make_shared<UdpSocket>();
    if (!recv_socket_->bind(local_port)) {
        return false;
    }

    // Initialize depacketizer
    depacketizer_ = std::make_shared<RtpDepacketizer>();

    // Start receive thread
    running_ = true;
    receive_thread_ = std::thread(&NetworkManager::receiveThreadFunc, this);

    mode_ = Mode::RECEIVER;
    return true;
}

bool NetworkManager::sendFrame(const EncodedFramePtr& frame) {
    if (mode_ != Mode::SENDER || !packetizer_ || !send_socket_) {
        return false;
    }

    // Set timestamp for all packets in this frame (90kHz clock)
    // For 30fps: 90000 / 30 = 3000 per frame
    packetizer_->setTimestamp(timestamp_);

    auto packets = packetizer_->packetize(frame);
    for (auto& packet : packets) {
        send_socket_->sendTo(packet->build(), target_ip_, target_port_);
    }

    timestamp_ += 3000;  // 90kHz / 30fps
    return true;
}

void NetworkManager::receiveThreadFunc() {
    std::vector<uint8_t> buffer;
    std::string src_ip;
    uint16_t src_port;

    while (running_) {
        buffer.clear();
        int result = recv_socket_->recvFrom(buffer, src_ip, src_port, 100);

        if (result > 0 && depacketizer_) {
            auto packet = RtpPacket::parse(buffer.data(), buffer.size());
            if (packet) {
                auto frame = depacketizer_->depacketize(packet);
                if (frame) {
                    received_queue_.push(frame);
                }
            }
        }
    }
}

EncodedFramePtr NetworkManager::tryGetFrame() {
    EncodedFramePtr frame;
    if (received_queue_.pop(frame, 0)) {
        return frame;
    }
    return nullptr;
}

void NetworkManager::shutdown() {
    running_ = false;

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    send_socket_.reset();
    recv_socket_.reset();
    packetizer_.reset();
    depacketizer_.reset();

    mode_ = Mode::NONE;
}

}  // namespace lancast
