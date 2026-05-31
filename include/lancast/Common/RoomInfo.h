#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <QMetaType>

namespace lancast {

// Room information for discovery
struct RoomInfo {
    std::string room_id_;      // UUID string
    std::string room_name_;    // Display name
    std::string host_name_;    // Computer name
    std::string host_ip_;      // IPv4 string
    uint16_t stream_port_;     // RTP stream port
    uint8_t  version_;         // Protocol version

    RoomInfo() : stream_port_(0), version_(1) {}

    // Serialize to bytes for UDP broadcast
    std::vector<uint8_t> serialize() const;

    // Deserialize from bytes
    static RoomInfo deserialize(const uint8_t* data, size_t len);

    static constexpr size_t SERIALIZED_SIZE = 16 + 64 + 32 + 4 + 2 + 1;
    // RoomID(16) + RoomName(64) + HostName(32) + IP(4) + Port(2) + Ver(1)
};

using RoomInfoPtr = std::shared_ptr<RoomInfo>;
using RoomInfoList = std::vector<RoomInfo>;

// Discovery message types
enum class DiscoveryMsgType : uint8_t {
    ADVERTISEMENT = 0x01,
    QUERY         = 0x02,
    RESPONSE      = 0x03,
    JOIN          = 0x04,
    LEAVE         = 0x05
};

// Discovery header: VER(1) + TTL(1) + MSG_TYPE(1)
struct DiscoveryHeader {
    uint8_t version_;
    uint8_t ttl_;       // Time to live, decremented on each hop
    DiscoveryMsgType msg_type_;

    static constexpr size_t SIZE = 3;
    static constexpr uint8_t CURRENT_VERSION = 1;
    static constexpr uint8_t DEFAULT_TTL = 5;
};

}  // namespace lancast

Q_DECLARE_METATYPE(lancast::RoomInfo)
Q_DECLARE_METATYPE(lancast::RoomInfoList)
Q_DECLARE_METATYPE(std::string)

