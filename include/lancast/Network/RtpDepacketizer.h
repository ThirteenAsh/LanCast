#pragma once

#include "Common/RtpPacket.h"
#include "Common/FrameBuffer.h"
#include <memory>
#include <map>

namespace lancast {

// RTP Depacketizer - reassembles RTP packets into H.264 NAL units
// Handles:
// - Single NAL unit packets
// - FU-A fragment reassembly
// - STAP-A aggregated packets
class RtpDepacketizer {
public:
    static constexpr uint8_t NAL_TYPE_FU_A = 28;
    static constexpr uint8_t NAL_TYPE_STAP_A = 24;
    RtpDepacketizer();

    // Process an incoming RTP packet, returns complete frame if all packets received
    // Returns nullptr if more packets expected
    EncodedFramePtr depacketize(const RtpPacketPtr& packet);

    // Check if we have a complete frame waiting
    bool hasCompleteFrame() const;

    // Get the next complete frame (blocks if none)
    EncodedFramePtr getFrame(int timeout_ms = 100);

    // Reset state (e.g., on stream start or gap detected)
    void reset();

    // Get last sequence number for gap detection
    uint16_t lastSeqNum() const { return last_seq_num_; }

private:
    // Process single NAL unit packet
    bool processSingleNal(const RtpPacketPtr& packet);

    // Process FU-A fragment
    bool processFuA(const RtpPacketPtr& packet);

    // Process STAP-A aggregated packet
    bool processStapA(const RtpPacketPtr& packet);

    // Complete current frame and clear buffer
    EncodedFramePtr completeFrame();

    // FU-A reassembly buffer
    struct FuABuffer {
        uint8_t nal_type = 0;
        std::vector<uint8_t> data;
        uint16_t start_seq = 0;
        uint16_t end_seq = 0;
        bool started = false;
        bool ended = false;

        void reset() {
            nal_type = 0;
            data.clear();
            start_seq = 0;
            end_seq = 0;
            started = false;
            ended = false;
        }
    };

    FuABuffer fu_a_buffer_;
    std::vector<uint8_t> current_frame_data_;
    uint32_t current_timestamp_ = 0;
    uint16_t last_seq_num_ = 0;
    bool has_frame_ = false;
    bool current_frame_keyframe_ = false;
    bool have_last_seq_ = false;
    bool drop_until_marker_ = false;
    bool wait_for_keyframe_ = false;
};

using RtpDepacketizerPtr = std::shared_ptr<RtpDepacketizer>;

}  // namespace lancast
