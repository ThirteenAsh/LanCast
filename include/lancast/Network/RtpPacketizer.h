#pragma once

#include "Common/RtpPacket.h"
#include "Common/FrameBuffer.h"
#include <memory>
#include <vector>

namespace lancast {

// RTP Packetizer - fragments H.264 NAL units into RTP packets
// Handles:
// - Single NAL unit packets (nal_unit)
// - Fragmentation units (FU-A) for large NALs
class RtpPacketizer {
public:
    RtpPacketizer();

    void setSsrc(uint32_t ssrc);
    void setPayloadType(uint8_t pt);
    void setTimestamp(uint32_t timestamp);  // 90kHz units

    // Packetize an encoded frame into multiple RTP packets
    std::vector<RtpPacketPtr> packetize(const EncodedFramePtr& frame);

    uint16_t currentSeqNum() const { return seq_num_; }

private:
    // Parse H.264 Annex B and extract NAL units
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> parseNalUnits(const EncodedFramePtr& frame);

    // Create single NAL unit packet
    RtpPacketPtr createSingleNalPacket(uint8_t nal_header, const std::vector<uint8_t>& nal_data, bool marker);

    // Create FU-A fragment packet
    RtpPacketPtr createFuAPacket(uint8_t nal_header, const std::vector<uint8_t>& nal_data,
                                  bool start, bool end, bool marker);

    uint32_t ssrc_ = 0;
    uint8_t payload_type_ = RtpPacket::DEFAULT_PT;
    uint32_t timestamp_ = 0;
    uint16_t seq_num_ = 0;

    static constexpr size_t MAX_RTP_PAYLOAD_SIZE = 1400;  // Typical MTU - IP/UDP/RTP headers
    static constexpr uint8_t NAL_TYPE_FU_A = 28;
    static constexpr uint8_t NAL_TYPE_STAP_A = 24;
};

using RtpPacketizerPtr = std::shared_ptr<RtpPacketizer>;

}  // namespace lancast
