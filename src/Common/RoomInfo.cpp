#include "Common/RoomInfo.h"
#include <cstring>

namespace lancast {

std::vector<uint8_t> RoomInfo::serialize() const {
    std::vector<uint8_t> buf(SERIALIZED_SIZE, 0);
    size_t offset = 0;

    // VER (1)
    buf[offset++] = DiscoveryHeader::CURRENT_VERSION;
    // TTL (1)
    buf[offset++] = DiscoveryHeader::DEFAULT_TTL;
    // MSG_TYPE (1)
    buf[offset++] = static_cast<uint8_t>(DiscoveryMsgType::ADVERTISEMENT);

    // RoomID (16 bytes)
    if (room_id_.size() >= 16) {
        memcpy(buf.data() + offset, room_id_.data(), 16);
    }
    offset += 16;

    // RoomName (64 bytes)
    if (room_name_.size() >= 64) {
        memcpy(buf.data() + offset, room_name_.data(), 64);
    } else {
        memcpy(buf.data() + offset, room_name_.data(), room_name_.size());
    }
    offset += 64;

    // HostName (32 bytes)
    if (host_name_.size() >= 32) {
        memcpy(buf.data() + offset, host_name_.data(), 32);
    } else {
        memcpy(buf.data() + offset, host_name_.data(), host_name_.size());
    }
    offset += 32;

    // IP (4 bytes) - parse "xxx.xxx.xxx.xxx"
    uint8_t ip0 = 0, ip1 = 0, ip2 = 0, ip3 = 0;
    if (sscanf(host_ip_.c_str(), "%hhu.%hhu.%hhu.%hhu", &ip0, &ip1, &ip2, &ip3) == 4) {
        buf[offset++] = ip0;
        buf[offset++] = ip1;
        buf[offset++] = ip2;
        buf[offset++] = ip3;
    } else {
        offset += 4;
    }

    // Port (2 bytes)
    buf[offset++] = (stream_port_ >> 8) & 0xFF;
    buf[offset++] = stream_port_ & 0xFF;

    // Version (1 byte)
    buf[offset++] = version_;

    return buf;
}

RoomInfo RoomInfo::deserialize(const uint8_t* data, size_t len) {
    RoomInfo info;
    size_t offset = 0;

    if (len < SERIALIZED_SIZE) return info;

    // Skip VER, TTL, MSG_TYPE
    offset += 3;

    // RoomID (16 bytes)
    info.room_id_.assign(reinterpret_cast<const char*>(data + offset), 16);
    offset += 16;

    // RoomName (64 bytes)
    info.room_name_.assign(reinterpret_cast<const char*>(data + offset), 64);
    // Trim trailing zeros
    info.room_name_ = info.room_name_.c_str();
    offset += 64;

    // HostName (32 bytes)
    info.host_name_.assign(reinterpret_cast<const char*>(data + offset), 32);
    info.host_name_ = info.host_name_.c_str();
    offset += 32;

    // IP (4 bytes)
    char ip_str[32];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             data[offset], data[offset+1], data[offset+2], data[offset+3]);
    info.host_ip_ = ip_str;
    offset += 4;

    // Port (2 bytes)
    info.stream_port_ = (data[offset] << 8) | data[offset + 1];
    offset += 2;

    // Version (1 byte)
    info.version_ = data[offset];

    return info;
}

}  // namespace lancast
