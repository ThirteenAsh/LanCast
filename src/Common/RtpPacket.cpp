#include "Common/RtpPacket.h"
#include <cstring>

namespace lancast {

RtpPacketPtr RtpPacket::parse(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE) return nullptr;

    auto packet = std::make_shared<RtpPacket>();
    std::memcpy(packet->header_, data, HEADER_SIZE);

    if (len > HEADER_SIZE) {
        packet->payload_.assign(data + HEADER_SIZE, data + len);
    }

    return packet;
}

std::vector<uint8_t> RtpPacket::build() const {
    std::vector<uint8_t> result(HEADER_SIZE + payload_.size());
    std::memcpy(result.data(), header_, HEADER_SIZE);
    if (!payload_.empty()) {
        std::memcpy(result.data() + HEADER_SIZE, payload_.data(), payload_.size());
    }
    return result;
}

}  // namespace lancast
