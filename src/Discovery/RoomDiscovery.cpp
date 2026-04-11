#include "Discovery/RoomDiscovery.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <chrono>
#include <random>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace lancast {

RoomDiscovery::RoomDiscovery() = default;

RoomDiscovery::~RoomDiscovery() {
    stopBroadcast();
}

bool RoomDiscovery::startBroadcast(const std::string& room_id, const std::string& room_name,
                                    uint16_t stream_port) {
    if (broadcasting_) {
        stopBroadcast();
    }

    // Get hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "Unknown");
    }

    // Get local IP address
    std::string local_ip = "127.0.0.1";

    // Use GetAdaptersInfo to find a non-loopback IP
    IP_ADAPTER_INFO adapter_info[16];
    DWORD buf_len = sizeof(adapter_info);
    if (GetAdaptersInfo(adapter_info, &buf_len) == ERROR_SUCCESS) {
        for (PIP_ADAPTER_INFO p = adapter_info; p; p = p->Next) {
            if (p->Type == MIB_IF_TYPE_ETHERNET && p->IpAddressList.IpAddress.String[0] != '0') {
                local_ip = p->IpAddressList.IpAddress.String;
                break;
            }
        }
    }

    // Fill in our room info
    our_info_.room_id_ = room_id;
    our_info_.room_name_ = room_name;
    our_info_.host_name_ = hostname;
    our_info_.host_ip_ = local_ip;
    our_info_.stream_port_ = stream_port;
    our_info_.version_ = DISCOVERY_VERSION;

    // Create UDP socket
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        return false;
    }

    // Enable broadcast
    BOOL opt = TRUE;
    setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));

    // Bind to discovery port
    sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DISCOVERY_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(socket_, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        closesocket(socket_);
        socket_ = -1;
        return false;
    }

    // Start discovery thread
    running_ = true;
    broadcasting_ = true;
    discovering_ = true;
    discovery_thread_ = std::thread(&RoomDiscovery::discoveryThreadFunc, this);

    return true;
}

void RoomDiscovery::stopBroadcast() {
    running_ = false;

    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }

    if (socket_ >= 0) {
        closesocket(socket_);
        socket_ = -1;
    }

    broadcasting_ = false;
    discovering_ = false;
    discovered_rooms_.clear();
}

void RoomDiscovery::sendQuery() {
    if (socket_ < 0) return;

    // Build query message
    uint8_t msg[DiscoveryHeader::SIZE];
    msg[0] = DiscoveryHeader::CURRENT_VERSION;
    msg[1] = DiscoveryHeader::DEFAULT_TTL;
    msg[2] = static_cast<uint8_t>(DiscoveryMsgType::QUERY);

    // Send to broadcast address
    sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DISCOVERY_PORT);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sendto(socket_, (const char*)msg, sizeof(msg), 0,
           (sockaddr*)&dest, sizeof(dest));
}

std::vector<RoomInfo> RoomDiscovery::getRooms() {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    std::vector<RoomInfo> rooms;
    for (const auto& kv : discovered_rooms_) {
        rooms.push_back(kv.second);
    }
    return rooms;
}

void RoomDiscovery::discoveryThreadFunc() {
    std::vector<uint8_t> recv_buf(1024);
    sockaddr_in from;
    int from_len = sizeof(from);

    auto last_advertisement = std::chrono::steady_clock::now();
    auto last_query = std::chrono::steady_clock::now();

    while (running_) {
        // Set up select for timeout
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_, &read_fds);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms timeout

        int result = select(0, &read_fds, nullptr, nullptr, &tv);
        if (result > 0) {
            int recv_len = recvfrom(socket_, (char*)recv_buf.data(),
                                     (int)recv_buf.size(), 0,
                                     (sockaddr*)&from, &from_len);
            if (recv_len > 0) {
                char src_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, src_ip, sizeof(src_ip));
                uint16_t src_port = ntohs(from.sin_port);

                processReceivedPacket(recv_buf.data(), recv_len, src_ip, src_port);
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Send periodic advertisement if broadcasting
        if (broadcasting_ && socket_ >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_advertisement).count();
            if (elapsed >= ADVERTISEMENT_INTERVAL_MS) {
                auto serialized = our_info_.serialize();
                // Note: serialized is RoomInfo without header, need to add header
                uint8_t msg[DiscoveryHeader::SIZE + RoomInfo::SERIALIZED_SIZE];
                msg[0] = DiscoveryHeader::CURRENT_VERSION;
                msg[1] = DiscoveryHeader::DEFAULT_TTL;
                msg[2] = static_cast<uint8_t>(DiscoveryMsgType::ADVERTISEMENT);
                memcpy(msg + DiscoveryHeader::SIZE, serialized.data(), RoomInfo::SERIALIZED_SIZE);

                sockaddr_in dest;
                memset(&dest, 0, sizeof(dest));
                dest.sin_family = AF_INET;
                dest.sin_port = htons(DISCOVERY_PORT);
                dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

                sendto(socket_, (const char*)msg, sizeof(msg), 0,
                       (sockaddr*)&dest, sizeof(dest));

                last_advertisement = now;
            }
        }

        // Send periodic query if discovering
        if (discovering_ && socket_ >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_query).count();
            if (elapsed >= QUERY_INTERVAL_MS) {
                sendQuery();
                last_query = now;
            }
        }
    }
}

void RoomDiscovery::processReceivedPacket(const uint8_t* data, size_t len,
                                            const std::string& src_ip, uint16_t src_port) {
    if (len < DiscoveryHeader::SIZE) return;

    DiscoveryHeader header;
    header.version_ = data[0];
    header.ttl_ = data[1];
    header.msg_type_ = static_cast<DiscoveryMsgType>(data[2]);

    if (header.version_ != DISCOVERY_VERSION) return;
    if (header.ttl_ == 0) return;

    if (header.msg_type_ == DiscoveryMsgType::ADVERTISEMENT ||
        header.msg_type_ == DiscoveryMsgType::RESPONSE) {
        if (len < DiscoveryHeader::SIZE + RoomInfo::SERIALIZED_SIZE) return;

        RoomInfo info = RoomInfo::deserialize(
            data + DiscoveryHeader::SIZE,
            RoomInfo::SERIALIZED_SIZE
        );

        info.host_ip_ = src_ip;  // Use actual source IP

        // Don't add our own room
        if (info.room_id_ == our_info_.room_id_) return;

        std::lock_guard<std::mutex> lock(rooms_mutex_);
        discovered_rooms_[info.room_id_] = info;

        if (on_room_discovered_) {
            on_room_discovered_(info);
        }
    }
}

}  // namespace lancast
