#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace lancast {

// Forward declaration
class RtpPacket;
using RtpPacketPtr = std::shared_ptr<RtpPacket>;

// RTP packet structure per RFC 3550
// | 7  | 6  | 5  | 4  | 3  | 2  | 1  | 0  |
// | V  | P | X  | CC |    | M  |    PT     |
// V=2bits, P=1bit, X=1bit, CC=4bits, M=1bit, PT=7bits
// |           sequence number (16bits)       |
// |           timestamp (32bits)             |
// |           SSRC (32bits)                  |

class RtpPacket {
public:
    static constexpr size_t HEADER_SIZE = 12;
    static constexpr uint8_t VERSION = 2;
    static constexpr uint8_t DEFAULT_PT = 96;  // H.264

    RtpPacket() {
        memset(header_, 0, HEADER_SIZE);
        header_[0] = (VERSION << 6);  // V=2, P=0, X=0, CC=0
    }

    // Parse from raw buffer (including header)
    static RtpPacketPtr parse(const uint8_t* data, size_t len);

    // Build wire format
    std::vector<uint8_t> build() const;

    // Header fields
    uint8_t version() const { return (header_[0] >> 6) & 0x3; }
    void setVersion(uint8_t v) { header_[0] = (header_[0] & 0x3F) | ((v & 0x3) << 6); }

    bool hasPadding() const { return (header_[0] >> 5) & 0x1; }
    void setPadding(bool p) { header_[0] = (header_[0] & 0xDF) | ((p ? 1 : 0) << 5); }

    bool hasExtension() const { return (header_[0] >> 4) & 0x1; }
    void setExtension(bool x) { header_[0] = (header_[0] & 0xEF) | ((x ? 1 : 0) << 4); }

    uint8_t cc() const { return header_[0] & 0xF; }  // CSRC count

    bool marker() const { return (header_[1] >> 7) & 0x1; }
    void setMarker(bool m) { header_[1] = (header_[1] & 0x7F) | ((m ? 1 : 0) << 7); }

    uint8_t payloadType() const { return header_[1] & 0x7F; }
    void setPayloadType(uint8_t pt) { header_[1] = (header_[1] & 0x80) | (pt & 0x7F); }

    uint16_t seqNum() const { return (header_[2] << 8) | header_[3]; }
    void setSeqNum(uint16_t seq) { header_[2] = (seq >> 8) & 0xFF; header_[3] = seq & 0xFF; }

    uint32_t timestamp() const {
        return (static_cast<uint32_t>(header_[4]) << 24) |
               (static_cast<uint32_t>(header_[5]) << 16) |
               (static_cast<uint32_t>(header_[6]) << 8) |
               static_cast<uint32_t>(header_[7]);
    }
    void setTimestamp(uint32_t ts) {
        header_[4] = (ts >> 24) & 0xFF;
        header_[5] = (ts >> 16) & 0xFF;
        header_[6] = (ts >> 8) & 0xFF;
        header_[7] = ts & 0xFF;
    }

    uint32_t ssrc() const {
        return (static_cast<uint32_t>(header_[8]) << 24) |
               (static_cast<uint32_t>(header_[9]) << 16) |
               (static_cast<uint32_t>(header_[10]) << 8) |
               static_cast<uint32_t>(header_[11]);
    }
    void setSsrc(uint32_t s) {
        header_[8] = (s >> 24) & 0xFF;
        header_[9] = (s >> 16) & 0xFF;
        header_[10] = (s >> 8) & 0xFF;
        header_[11] = s & 0xFF;
    }

    // Payload
    void setPayload(const uint8_t* data, size_t len) {
        payload_.assign(data, data + len);
    }
    const std::vector<uint8_t>& payload() const { return payload_; }
    std::vector<uint8_t>& payload() { return payload_; }

private:
    uint8_t header_[HEADER_SIZE];
    std::vector<uint8_t> payload_;
};

}  // namespace lancast
