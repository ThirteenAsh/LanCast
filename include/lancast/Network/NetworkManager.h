#pragma once

#include "Common/FrameBuffer.h"
#include "Common/RtpPacket.h"
#include "Common/CircularBuffer.h"
#include "Core/FrameQueue.h"
#include "RtpPacketizer.h"
#include "RtpDepacketizer.h"
#include "UdpSocket.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <vector>
#include <mutex>

namespace lancast {

// Network manager handles sending/receiving RTP streams
class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Initialize as sender (HOST mode)
    bool initSender(const std::string& target_ip, uint16_t target_port);

    // Initialize as receiver (VIEWER mode)
    bool initReceiver(uint16_t local_port, const std::string& expected_source_ip = "");

    // Send encoded frame
    bool sendFrame(const EncodedFramePtr& frame);

    // Manage unicast RTP targets for HOST mode.
    void addTargetIp(const std::string& ip);
    void removeTargetIp(const std::string& ip);
    size_t targetCount() const;

    // Receive RTP packet (call from receive thread)
    void receiveThreadFunc();

    // Check if there's a received frame available
    EncodedFramePtr tryGetFrame();

    // Get depacketizer for external use
    RtpDepacketizer* depacketizer() { return depacketizer_.get(); }

    // Stop network operations
    void shutdown();

    bool isSender() const { return mode_ == Mode::SENDER; }
    bool isReceiver() const { return mode_ == Mode::RECEIVER; }

    std::string targetIp() const { return target_ip_; }
    uint16_t targetPort() const { return target_port_; }

private:
    enum class Mode { NONE, SENDER, RECEIVER };

    Mode mode_ = Mode::NONE;
    std::string target_ip_;
    std::vector<std::string> target_ips_;
    mutable std::mutex targets_mutex_;
    uint16_t target_port_ = 0;
    std::string expected_source_ip_;
    std::string loopback_target_ip_ = "127.0.0.1";
    uint16_t loopback_target_port_ = 0;
    uint16_t local_port_ = 0;

    UdpSocketPtr send_socket_;
    UdpSocketPtr recv_socket_;

    RtpPacketizerPtr packetizer_;
    RtpDepacketizerPtr depacketizer_;

    std::thread receive_thread_;
    std::atomic<bool> running_{false};

    EncodedFrameQueue received_queue_;
    static constexpr size_t RECEIVE_QUEUE_SIZE = 64;

    uint32_t ssrc_;
    uint32_t timestamp_;
    uint16_t seq_num_;
};

using NetworkManagerPtr = std::shared_ptr<NetworkManager>;

}  // namespace lancast
