#include "Discovery/RoomDiscovery.h"
#include "Common/Logger.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <chrono>
#include <random>
#include <algorithm>
#include <cctype>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace lancast {
namespace {

struct DiscoveryEndpoint {
    std::string local_ip;
    std::string broadcast_ip;
    ULONG if_type = 0;
    std::string adapter_name;
    uint32_t local_ip_value = 0;
};

std::string narrowWide(const wchar_t* value) {
    if (!value || !*value) {
        return {};
    }

    int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string out(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), required, nullptr, nullptr);
    out.resize(static_cast<size_t>(required - 1));
    return out;
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isLoopbackIp(uint32_t ip) {
    return (ip & 0xFF000000u) == 0x7F000000u;
}

bool isApipaIp(uint32_t ip) {
    return (ip & 0xFFFF0000u) == 0xA9FE0000u;
}

bool isPrivateIp(uint32_t ip) {
    return (ip & 0xFF000000u) == 0x0A000000u ||
           (ip & 0xFFF00000u) == 0xAC100000u ||
           (ip & 0xFFFF0000u) == 0xC0A80000u;
}

bool looksVirtualAdapter(const std::string& adapter_name) {
    const std::string name = toLowerCopy(adapter_name);
    static const char* kVirtualKeywords[] = {
        "virtual",
        "vmware",
        "hyper-v",
        "vethernet",
        "virtualbox",
        "hamachi",
        "tap",
        "tun",
        "zerotier",
        "tailscale",
        "wireguard"
    };

    for (const char* keyword : kVirtualKeywords) {
        if (name.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int scoreLocalEndpoint(const DiscoveryEndpoint& endpoint) {
    int score = 0;
    if (isPrivateIp(endpoint.local_ip_value)) {
        score += 400;
    } else if (isApipaIp(endpoint.local_ip_value) || isLoopbackIp(endpoint.local_ip_value)) {
        score -= 500;
    } else {
        score += 100;
    }

    if (endpoint.if_type == IF_TYPE_ETHERNET_CSMACD) {
        score += 80;
    } else if (endpoint.if_type == IF_TYPE_IEEE80211) {
        score += 70;
    }

    if (!looksVirtualAdapter(endpoint.adapter_name)) {
        score += 40;
    } else {
        score -= 80;
    }

    return score;
}

uint32_t ipv4StringToHostOrder(const std::string& ip) {
    in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return 0;
    }
    return ntohl(addr.S_un.S_addr);
}

int scoreRemoteIp(const std::string& ip) {
    const uint32_t ip_value = ipv4StringToHostOrder(ip);
    if (ip_value == 0 || isLoopbackIp(ip_value)) {
        return -1000;
    }
    if (isPrivateIp(ip_value)) {
        return 300;
    }
    if (isApipaIp(ip_value)) {
        return -100;
    }
    return 100;
}

std::vector<DiscoveryEndpoint> enumerateDiscoveryEndpoints() {
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

    std::vector<DiscoveryEndpoint> endpoints;
    if (result != ERROR_SUCCESS) {
        return endpoints;
    }

    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType == IF_TYPE_TUNNEL) {
            continue;
        }

        const std::string adapter_name = narrowWide(adapter->FriendlyName);
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }

            auto* ipv4 = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
            const uint32_t ip_value = ntohl(ipv4->sin_addr.S_un.S_addr);
            if (ip_value == 0 || isLoopbackIp(ip_value) || isApipaIp(ip_value)) {
                continue;
            }

            const ULONG prefix_length = unicast->OnLinkPrefixLength;
            if (prefix_length > 30) {
                continue;
            }

            const uint32_t mask = prefix_length == 0
                ? 0
                : (0xFFFFFFFFu << (32 - prefix_length));
            const uint32_t broadcast_value = ip_value | (~mask);

            in_addr ip_addr{};
            ip_addr.S_un.S_addr = htonl(ip_value);
            in_addr broadcast_addr{};
            broadcast_addr.S_un.S_addr = htonl(broadcast_value);

            char ip_str[INET_ADDRSTRLEN] = {};
            char broadcast_str[INET_ADDRSTRLEN] = {};
            if (!inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str)) ||
                !inet_ntop(AF_INET, &broadcast_addr, broadcast_str, sizeof(broadcast_str))) {
                continue;
            }

            const bool already_exists = std::any_of(
                endpoints.begin(),
                endpoints.end(),
                [&](const DiscoveryEndpoint& existing) {
                    return existing.local_ip == ip_str && existing.broadcast_ip == broadcast_str;
                }
            );
            if (already_exists) {
                continue;
            }

            DiscoveryEndpoint endpoint;
            endpoint.local_ip = ip_str;
            endpoint.broadcast_ip = broadcast_str;
            endpoint.if_type = adapter->IfType;
            endpoint.adapter_name = adapter_name;
            endpoint.local_ip_value = ip_value;
            endpoints.push_back(std::move(endpoint));
        }
    }

    return endpoints;
}

std::string selectPreferredLocalIp() {
    const auto endpoints = enumerateDiscoveryEndpoints();
    if (endpoints.empty()) {
        return "127.0.0.1";
    }

    const auto best = std::max_element(
        endpoints.begin(),
        endpoints.end(),
        [](const DiscoveryEndpoint& lhs, const DiscoveryEndpoint& rhs) {
            return scoreLocalEndpoint(lhs) < scoreLocalEndpoint(rhs);
        }
    );
    return best->local_ip;
}

bool sendDiscoveryPacket(SOCKET socket, const uint8_t* data, size_t len, uint16_t port) {
    bool sent = false;
    const auto endpoints = enumerateDiscoveryEndpoints();

    for (const auto& endpoint : endpoints) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        if (inet_pton(AF_INET, endpoint.broadcast_ip.c_str(), &dest.sin_addr) != 1) {
            continue;
        }

        if (sendto(socket, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                   reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == static_cast<int>(len)) {
            sent = true;
        }
    }

    if (sent) {
        return true;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    return sendto(socket, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                  reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == static_cast<int>(len);
}

bool sendDiscoveryPacketToDiscoveryPorts(SOCKET socket, const uint8_t* data, size_t len) {
    bool sent = false;
    for (uint16_t port = RoomDiscovery::DISCOVERY_PORT;
         port <= RoomDiscovery::DISCOVERY_PORT_MAX;
         ++port) {
        sent = sendDiscoveryPacket(socket, data, len, port) || sent;
    }
    return sent;
}

bool sendUnicastDiscoveryControl(SOCKET socket,
                                 const std::string& host_ip,
                                 DiscoveryMsgType type,
                                 const std::string& room_id) {
    if (socket < 0 || host_ip.empty()) {
        return false;
    }

    uint8_t msg[DiscoveryHeader::SIZE + 16] = {};
    msg[0] = DiscoveryHeader::CURRENT_VERSION;
    msg[1] = DiscoveryHeader::DEFAULT_TTL;
    msg[2] = static_cast<uint8_t>(type);
    memcpy(msg + DiscoveryHeader::SIZE, room_id.data(), std::min<size_t>(room_id.size(), 16));

    bool sent = false;
    for (uint16_t port = RoomDiscovery::DISCOVERY_PORT;
         port <= RoomDiscovery::DISCOVERY_PORT_MAX;
         ++port) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        if (inet_pton(AF_INET, host_ip.c_str(), &dest.sin_addr) != 1) {
            continue;
        }

        sent = sendto(socket,
                      reinterpret_cast<const char*>(msg),
                      static_cast<int>(sizeof(msg)),
                      0,
                      reinterpret_cast<sockaddr*>(&dest),
                      sizeof(dest)) == static_cast<int>(sizeof(msg)) || sent;
    }
    return sent;
}

}

RoomDiscovery::RoomDiscovery() = default;

RoomDiscovery::~RoomDiscovery() {
    stopBroadcast();
}

bool RoomDiscovery::openDiscoverySocket() {
    if (socket_ >= 0) {
        return true;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        return false;
    }

    BOOL opt = TRUE;
    setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&opt, sizeof(opt));

    sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;

    for (uint16_t port = DISCOVERY_PORT; port <= DISCOVERY_PORT_MAX; ++port) {
        bind_addr.sin_port = htons(port);
        if (::bind(socket_, (sockaddr*)&bind_addr, sizeof(bind_addr)) == 0) {
            discovery_port_ = port;
            Logger::log("RoomDiscovery bound UDP port " + std::to_string(discovery_port_));
            return true;
        }

        Logger::log("RoomDiscovery UDP port " + std::to_string(port) +
                    " unavailable, error=" + std::to_string(WSAGetLastError()));
    }

    closesocket(socket_);
    socket_ = -1;
    Logger::log("RoomDiscovery failed to bind any UDP port in range " +
                std::to_string(DISCOVERY_PORT) + "-" + std::to_string(DISCOVERY_PORT_MAX));
    return false;
}

bool RoomDiscovery::startDiscovery() {
    if (running_) {
        discovering_ = true;
        return true;
    }

    if (!openDiscoverySocket()) {
        return false;
    }

    broadcasting_ = false;
    discovering_ = true;
    running_ = true;
    Logger::log("RoomDiscovery::startDiscovery endpoints=" +
                std::to_string(enumerateDiscoveryEndpoints().size()) +
                " port=" + std::to_string(discovery_port_));
    discovery_thread_ = std::thread(&RoomDiscovery::discoveryThreadFunc, this);
    return true;
}

bool RoomDiscovery::startBroadcast(const std::string& room_id, const std::string& room_name,
                                    uint16_t stream_port) {
    if (running_) {
        stopBroadcast();
    }

    // Get hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "Unknown");
    }

    const auto endpoints = enumerateDiscoveryEndpoints();
    const std::string local_ip = selectPreferredLocalIp();
    Logger::log("RoomDiscovery::startBroadcast local_ip=" + local_ip +
                " endpoints=" + std::to_string(endpoints.size()));

    // Fill in our room info
    our_info_.room_id_ = room_id;
    our_info_.room_name_ = room_name;
    our_info_.host_name_ = hostname;
    our_info_.host_ip_ = local_ip;
    our_info_.stream_port_ = stream_port;
    our_info_.version_ = DISCOVERY_VERSION;

    if (!openDiscoverySocket()) {
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
    discovery_port_ = DISCOVERY_PORT;

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

    sendDiscoveryPacketToDiscoveryPorts(socket_, msg, sizeof(msg));
}

void RoomDiscovery::sendJoin(const RoomInfo& room) {
    if (socket_ < 0) return;

    if (sendUnicastDiscoveryControl(socket_, room.host_ip_, DiscoveryMsgType::JOIN, room.room_id_)) {
        Logger::log("RoomDiscovery sent JOIN room=" + room.room_id_ + " host=" + room.host_ip_);
    }
}

void RoomDiscovery::sendLeave(const RoomInfo& room) {
    if (socket_ < 0) return;

    if (sendUnicastDiscoveryControl(socket_, room.host_ip_, DiscoveryMsgType::LEAVE, room.room_id_)) {
        Logger::log("RoomDiscovery sent LEAVE room=" + room.room_id_ + " host=" + room.host_ip_);
    }
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

                sendDiscoveryPacketToDiscoveryPorts(socket_, msg, sizeof(msg));

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
        auto it = discovered_rooms_.find(info.room_id_);
        if (it == discovered_rooms_.end() ||
            scoreRemoteIp(info.host_ip_) > scoreRemoteIp(it->second.host_ip_)) {
            discovered_rooms_[info.room_id_] = info;
        }

        if (on_room_discovered_) {
            on_room_discovered_(discovered_rooms_[info.room_id_]);
        }
    } else if (header.msg_type_ == DiscoveryMsgType::QUERY) {
        if (!broadcasting_ || socket_ < 0) {
            return;
        }

        auto serialized = our_info_.serialize();
        uint8_t msg[DiscoveryHeader::SIZE + RoomInfo::SERIALIZED_SIZE];
        msg[0] = DiscoveryHeader::CURRENT_VERSION;
        msg[1] = DiscoveryHeader::DEFAULT_TTL;
        msg[2] = static_cast<uint8_t>(DiscoveryMsgType::RESPONSE);
        memcpy(msg + DiscoveryHeader::SIZE, serialized.data(), RoomInfo::SERIALIZED_SIZE);

        sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = htons(src_port);
        if (inet_pton(AF_INET, src_ip.c_str(), &dest.sin_addr) == 1) {
            sendto(socket_, (const char*)msg, sizeof(msg), 0,
                   (sockaddr*)&dest, sizeof(dest));
        }
    } else if (header.msg_type_ == DiscoveryMsgType::JOIN ||
               header.msg_type_ == DiscoveryMsgType::LEAVE) {
        if (!broadcasting_ || len < DiscoveryHeader::SIZE + 16) {
            return;
        }

        std::string room_id(reinterpret_cast<const char*>(data + DiscoveryHeader::SIZE), 16);
        room_id = room_id.c_str();
        if (room_id != our_info_.room_id_) {
            return;
        }

        if (header.msg_type_ == DiscoveryMsgType::JOIN) {
            Logger::log("RoomDiscovery received JOIN from " + src_ip +
                        " room=" + room_id);
            if (on_viewer_joined_) {
                on_viewer_joined_(src_ip);
            }
        } else {
            Logger::log("RoomDiscovery received LEAVE from " + src_ip +
                        " room=" + room_id);
            if (on_viewer_left_) {
                on_viewer_left_(src_ip);
            }
        }
    }
}

}  // namespace lancast
