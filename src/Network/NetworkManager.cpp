#include "Network/NetworkManager.h"
#include "Common/Logger.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <chrono>
#include <random>
#include <algorithm>
#include <vector>
#include <deque>
#include <unordered_set>

#pragma comment(lib, "iphlpapi.lib")

namespace {

std::vector<std::string> enumerateDirectedBroadcastTargets() {
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG buffer_length = 16 * 1024;
    std::vector<unsigned char> buffer(buffer_length);

    DWORD result = GetAdaptersAddresses(
        AF_INET,
        flags,
        nullptr,
        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
        &buffer_length
    );

    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(buffer_length);
        result = GetAdaptersAddresses(
            AF_INET,
            flags,
            nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
            &buffer_length
        );
    }

    std::vector<std::string> targets;
    if (result != ERROR_SUCCESS) {
        return targets;
    }

    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) continue;
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType == IF_TYPE_TUNNEL) continue;

        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;

            auto* ipv4 = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
            const uint32_t ip_value = ntohl(ipv4->sin_addr.S_un.S_addr);
            if (ip_value == 0 || (ip_value & 0xFF000000u) == 0x7F000000u || (ip_value & 0xFFFF0000u) == 0xA9FE0000u) {
                continue;
            }

            const ULONG prefix_length = unicast->OnLinkPrefixLength;
            if (prefix_length > 30) continue;

            const uint32_t mask = prefix_length == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix_length));
            const uint32_t broadcast_value = ip_value | (~mask);

            in_addr broadcast_addr{};
            broadcast_addr.S_un.S_addr = htonl(broadcast_value);

            char broadcast_str[INET_ADDRSTRLEN] = {};
            if (!inet_ntop(AF_INET, &broadcast_addr, broadcast_str, sizeof(broadcast_str))) {
                continue;
            }

            const std::string candidate = broadcast_str;
            if (std::find(targets.begin(), targets.end(), candidate) == targets.end()) {
                targets.push_back(candidate);
            }
        }
    }

    return targets;
}

}  // namespace

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
    Logger::log("NetworkManager::initSender ip=" + target_ip + ":" + std::to_string(target_port));
    target_ip_ = target_ip;
    target_port_ = target_port;
    target_ips_.clear();

    if (target_ip == "255.255.255.255") {
        target_ips_ = enumerateDirectedBroadcastTargets();
        if (target_ips_.empty()) {
            target_ips_.push_back(target_ip);
        }
    } else {
        target_ips_.push_back(target_ip);
    }
    Logger::log("NetworkManager::initSender targets=" + std::to_string(target_ips_.size()));
    // Do not send a second loopback copy.
    // When running host/viewer on the same machine, duplicated packets from
    // broadcast + loopback can corrupt depacketization order.
    loopback_target_port_ = 0;

    send_socket_ = std::make_shared<UdpSocket>();
    if (!send_socket_->bind(0)) {  // Bind to any available port
        return false;
    }
    send_socket_->setBroadcast(true);

    // Initialize packetizer
    packetizer_ = std::make_shared<RtpPacketizer>();
    packetizer_->setSsrc(ssrc_);
    packetizer_->setPayloadType(96);
    timestamp_ = 0;
    seq_num_ = 0;

    mode_ = Mode::SENDER;
    return true;
}

bool NetworkManager::initReceiver(uint16_t local_port, const std::string& expected_source_ip) {
    Logger::log("NetworkManager::initReceiver port=" + std::to_string(local_port) +
                " expected=" + expected_source_ip);
    local_port_ = local_port;
    expected_source_ip_ = expected_source_ip;

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
    static uint64_t sent_frames = 0;
    if (mode_ != Mode::SENDER || !packetizer_ || !send_socket_) {
        return false;
    }

    // Set timestamp for all packets in this frame (90kHz clock)
    // For 30fps: 90000 / 30 = 3000 per frame
    packetizer_->setTimestamp(timestamp_);

    auto packets = packetizer_->packetize(frame);
    if ((++sent_frames % 30) == 1) Logger::log("sendFrame bytes=" + std::to_string(frame ? frame->data_.size() : 0) + " packets=" + std::to_string(packets.size()) + " key=" + std::to_string(frame && frame->key_frame_));
    for (auto& packet : packets) {
        auto bytes = packet->build();
        for (const auto& target : target_ips_) {
            send_socket_->sendTo(bytes, target, target_port_);
        }
    }

    timestamp_ += 3000;  // 90kHz / 30fps
    return true;
}

void NetworkManager::receiveThreadFunc() {
    uint64_t packet_count = 0;
    uint64_t dropped_foreign = 0;
    uint64_t dropped_duplicate = 0;
    std::vector<uint8_t> buffer;
    std::string src_ip;
    uint16_t src_port;
    std::deque<uint64_t> recent_packet_keys;
    std::unordered_set<uint64_t> recent_packet_key_set;
    constexpr size_t kRecentPacketWindow = 8192;

    while (running_) {
        buffer.clear();
        int result = recv_socket_->recvFrom(buffer, src_ip, src_port, 100);

        if (result > 0 && depacketizer_) {
            if (!expected_source_ip_.empty() && src_ip != expected_source_ip_) {
                if ((++dropped_foreign % 200) == 1) {
                    Logger::log("drop RTP from unexpected src=" + src_ip +
                                " expected=" + expected_source_ip_);
                }
                continue;
            }
            auto packet = RtpPacket::parse(buffer.data(), buffer.size());
            if ((++packet_count % 100) == 1) Logger::log("recv RTP bytes=" + std::to_string(buffer.size()) + " from=" + src_ip + ":" + std::to_string(src_port));
            if (packet) {
                const uint64_t packet_key =
                    (static_cast<uint64_t>(packet->ssrc()) << 32) ^
                    (static_cast<uint64_t>(packet->timestamp()) << 1) ^
                    static_cast<uint64_t>(packet->seqNum());
                if (recent_packet_key_set.find(packet_key) != recent_packet_key_set.end()) {
                    if ((++dropped_duplicate % 200) == 1) {
                        Logger::log("drop duplicate RTP seq=" + std::to_string(packet->seqNum()) +
                                    " ts=" + std::to_string(packet->timestamp()));
                    }
                    continue;
                }
                recent_packet_key_set.insert(packet_key);
                recent_packet_keys.push_back(packet_key);
                if (recent_packet_keys.size() > kRecentPacketWindow) {
                    const uint64_t oldest = recent_packet_keys.front();
                    recent_packet_keys.pop_front();
                    recent_packet_key_set.erase(oldest);
                }

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


