#include "Network/RtpDepacketizer.h"
#include "Common/Logger.h"
#include <cstring>

namespace lancast {

RtpDepacketizer::RtpDepacketizer()
    : last_seq_num_(0), has_frame_(false), current_frame_keyframe_(false) {
}

EncodedFramePtr RtpDepacketizer::depacketize(const RtpPacketPtr& packet) {
    if (!packet) return nullptr;

    if (packet->payload().empty()) return nullptr;

    const uint16_t seq = packet->seqNum();
    if (have_last_seq_) {
        const uint16_t expected_seq = static_cast<uint16_t>(last_seq_num_ + 1);
        if (seq != expected_seq) {
            Logger::log("RTP sequence gap last=" + std::to_string(last_seq_num_) +
                        " got=" + std::to_string(seq) +
                        " expected=" + std::to_string(expected_seq));
            current_frame_data_.clear();
            current_frame_keyframe_ = false;
            has_frame_ = false;
            fu_a_buffer_.reset();
            drop_until_marker_ = true;
            wait_for_keyframe_ = true;
        }
    }

    last_seq_num_ = seq;
    have_last_seq_ = true;

    if (drop_until_marker_) {
        if (packet->marker()) {
            drop_until_marker_ = false;
        }
        return nullptr;
    }

    uint8_t payload_type = packet->payload()[0] & 0x1F;

    if (payload_type >= 1 && payload_type <= 23) {
        // Single NAL unit packet
        if (!processSingleNal(packet)) {
            return nullptr;
        }
    } else if (payload_type == NAL_TYPE_FU_A) {
        // FU-A fragment
        if (!processFuA(packet)) {
            return nullptr;
        }
    } else if (payload_type == NAL_TYPE_STAP_A) {
        // STAP-A aggregated packet
        if (!processStapA(packet)) {
            return nullptr;
        }
    } else {
        return nullptr;  // Unknown type
    }

    // Check if frame is complete
    if (has_frame_) {
        return completeFrame();
    }

    return nullptr;
}

bool RtpDepacketizer::processSingleNal(const RtpPacketPtr& packet) {
    const auto& payload = packet->payload();
    if (payload.empty()) return false;

    // Reconstruct length + NAL type header
    uint8_t nal_type = payload[0] & 0x1F;

    // Check for keyframe (IDR slice)
    // IDR slices have nal_type = 5, SPS = 7, PPS = 8
    if (nal_type == 5 || nal_type == 7 || nal_type == 8) {
        current_frame_keyframe_ = true;
    }

    std::vector<uint8_t> nal_unit;
    nal_unit.reserve(payload.size());

    // Start code
    nal_unit.push_back(0x00);
    nal_unit.push_back(0x00);
    nal_unit.push_back(0x00);
    nal_unit.push_back(0x01);

    // Single NAL payload already contains full NAL header + data.
    nal_unit.insert(nal_unit.end(), payload.begin(), payload.end());

    current_frame_data_.insert(current_frame_data_.end(),
                                nal_unit.begin(), nal_unit.end());
    current_timestamp_ = packet->timestamp();
    has_frame_ = packet->marker();

    return true;
}

bool RtpDepacketizer::processFuA(const RtpPacketPtr& packet) {
    const auto& payload = packet->payload();
    if (payload.size() < 3) return false;

    uint8_t fu_indicator = payload[0];
    uint8_t fu_header = payload[1];

    bool start = (fu_header & 0x80) != 0;
    bool end = (fu_header & 0x40) != 0;
    uint8_t nal_type = fu_header & 0x1F;

    // Check for keyframe markers
    if (nal_type == 5) current_frame_keyframe_ = true;

    if (start) {
        Logger::log("FU-A start seq=" + std::to_string(packet->seqNum()) + " ts=" + std::to_string(packet->timestamp()) + " nal=" + std::to_string(nal_type));
        // Start of fragment - reset buffer
        fu_a_buffer_.data.clear();
        fu_a_buffer_.nal_type = nal_type;
        fu_a_buffer_.started = true;
        fu_a_buffer_.start_seq = packet->seqNum();
    }

    if (!fu_a_buffer_.started) {
        drop_until_marker_ = true;
        return false;  // Received middle without start
    }

    // Add fragment data (skip 2 header bytes)
    fu_a_buffer_.data.insert(fu_a_buffer_.data.end(),
                              payload.begin() + 2, payload.end());

    if (end) {
        Logger::log("FU-A end seq=" + std::to_string(packet->seqNum()) + " ts=" + std::to_string(packet->timestamp()));
        fu_a_buffer_.ended = true;
        fu_a_buffer_.end_seq = packet->seqNum();
    }

    // If complete, copy to frame data
    if (fu_a_buffer_.ended) {
        std::vector<uint8_t> nal_unit;
        nal_unit.reserve(fu_a_buffer_.data.size() + 4);

        // Start code
        nal_unit.push_back(0x00);
        nal_unit.push_back(0x00);
        nal_unit.push_back(0x00);
        nal_unit.push_back(0x01);

        // Rebuild original NAL header from FU indicator(F/NRI) + FU header(type).
        uint8_t nal_header = (fu_indicator & 0xE0) | (fu_a_buffer_.nal_type & 0x1F);
        nal_unit.push_back(nal_header);

        // Fragment data
        nal_unit.insert(nal_unit.end(),
                        fu_a_buffer_.data.begin(), fu_a_buffer_.data.end());

        current_frame_data_.insert(current_frame_data_.end(),
                                    nal_unit.begin(), nal_unit.end());
        current_timestamp_ = packet->timestamp();
        has_frame_ = packet->marker();

        fu_a_buffer_.reset();
    }

    return true;
}

bool RtpDepacketizer::processStapA(const RtpPacketPtr& packet) {
    const auto& payload = packet->payload();
    if (payload.size() < 3) return false;

    // Skip STAP-A header (1 byte)
    size_t offset = 1;

    while (offset < payload.size()) {
        if (offset + 4 > payload.size()) break;

        // NAL size (4 bytes big-endian)
        uint32_t nal_size = (payload[offset] << 24) |
                             (payload[offset+1] << 16) |
                             (payload[offset+2] << 8) |
                             payload[offset+3];
        offset += 4;

        if (offset + nal_size > payload.size()) break;

        uint8_t nal_type = payload[offset] & 0x1F;
        if (nal_type == 5 || nal_type == 7 || nal_type == 8) {
            current_frame_keyframe_ = true;
        }

        // Add to frame
        current_frame_data_.push_back(0x00);
        current_frame_data_.push_back(0x00);
        current_frame_data_.push_back(0x00);
        current_frame_data_.push_back(0x01);
        current_frame_data_.insert(current_frame_data_.end(),
                                    payload.begin() + offset,
                                    payload.begin() + offset + nal_size);

        offset += nal_size;
    }

    current_timestamp_ = packet->timestamp();
    has_frame_ = packet->marker();

    return true;
}

EncodedFramePtr RtpDepacketizer::completeFrame() {
    if (current_frame_data_.empty()) {
        has_frame_ = false;
        return nullptr;
    }

    if (wait_for_keyframe_ && !current_frame_keyframe_) {
        Logger::log("drop non-key frame while waiting for keyframe after RTP loss ts=" +
                    std::to_string(current_timestamp_));
        current_frame_data_.clear();
        current_frame_keyframe_ = false;
        has_frame_ = false;
        return nullptr;
    }

    auto frame = std::make_shared<EncodedFrame>();
    Logger::log("completeFrame bytes=" + std::to_string(current_frame_data_.size()) + " key=" + std::to_string(current_frame_keyframe_) + " ts=" + std::to_string(current_timestamp_));
    frame->data_ = std::move(current_frame_data_);
    frame->key_frame_ = current_frame_keyframe_;
    frame->pts_ = current_timestamp_ * 1000 / 90;  // Convert 90kHz to microseconds

    if (current_frame_keyframe_) {
        wait_for_keyframe_ = false;
    }
    current_frame_data_.clear();
    current_frame_keyframe_ = false;
    has_frame_ = false;

    return frame;
}

bool RtpDepacketizer::hasCompleteFrame() const {
    return has_frame_;
}

EncodedFramePtr RtpDepacketizer::getFrame(int timeout_ms) {
    // In a real implementation, this would wait on a condition variable
    // For now, just return what we have
    if (has_frame_) {
        return completeFrame();
    }
    return nullptr;
}

void RtpDepacketizer::reset() {
    current_frame_data_.clear();
    current_timestamp_ = 0;
    last_seq_num_ = 0;
    has_frame_ = false;
    current_frame_keyframe_ = false;
    have_last_seq_ = false;
    drop_until_marker_ = false;
    wait_for_keyframe_ = false;
    fu_a_buffer_ = {};
}

}  // namespace lancast



