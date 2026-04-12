#include "Network/RtpPacketizer.h"
#include <cstring>
#include <cstdlib>

namespace lancast {

RtpPacketizer::RtpPacketizer()
    : seq_num_(static_cast<uint16_t>(rand() & 0xFFFF)) {
}

void RtpPacketizer::setSsrc(uint32_t ssrc) {
    ssrc_ = ssrc;
}

void RtpPacketizer::setPayloadType(uint8_t pt) {
    payload_type_ = pt;
}

void RtpPacketizer::setTimestamp(uint32_t timestamp) {
    timestamp_ = timestamp;
}

std::vector<RtpPacketPtr> RtpPacketizer::packetize(const EncodedFramePtr& frame) {
    std::vector<RtpPacketPtr> packets;

    if (!frame || frame->data_.empty()) {
        return packets;
    }

    auto nal_units = parseNalUnits(frame);

    for (size_t i = 0; i < nal_units.size(); ++i) {
        uint8_t nal_header = nal_units[i].first;
        const auto& nal_data = nal_units[i].second;

        bool is_last = (i == nal_units.size() - 1);
        bool marker = is_last;

        if (nal_data.size() <= MAX_RTP_PAYLOAD_SIZE) {
            // Single NAL unit packet
            packets.push_back(createSingleNalPacket(nal_header, nal_data, marker));
        } else {
            // FU-A fragmentation
            size_t fu_payload_size = MAX_RTP_PAYLOAD_SIZE - 2;  // FU header (2 bytes)
            size_t num_frags = (nal_data.size() + fu_payload_size - 1) / fu_payload_size;

            for (size_t f = 0; f < num_frags; ++f) {
                bool start = (f == 0);
                bool end = (f == num_frags - 1);
                size_t offset = f * fu_payload_size;
                size_t size = std::min(fu_payload_size, nal_data.size() - offset);

                std::vector<uint8_t> frag_data(nal_data.begin() + offset,
                                                nal_data.begin() + offset + size);
                packets.push_back(createFuAPacket(nal_header, frag_data, start, end, end && is_last));
            }
        }
    }

    return packets;
}

std::vector<std::pair<uint8_t, std::vector<uint8_t>>> RtpPacketizer::parseNalUnits(const EncodedFramePtr& frame) {
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> result;

    const uint8_t* data = frame->data_.data();
    size_t size = frame->data_.size();

    size_t pos = 0;
    while (pos < size) {
        // Find start code (0x00000001 or 0x000001)
        if (pos + 4 > size) break;

        uint32_t possible_start = (data[pos] << 24) | (data[pos+1] << 16) |
                                   (data[pos+2] << 8) | data[pos+3];
        bool has_4byte_start = (possible_start == 0x00000001);
        bool has_3byte_start = false;

        if (!has_4byte_start && pos + 3 <= size) {
            has_3byte_start = (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1);
        }

        size_t skip = has_4byte_start ? 4 : (has_3byte_start ? 3 : 1);
        if (skip == 1) {
            pos++;
            continue;
        }
        pos += skip;

        if (pos >= size) break;

        // Full NAL header byte (F + NRI + Type).
        uint8_t nal_header = data[pos];

        // Skip NAL header byte for data
        std::vector<uint8_t> nal_data(data + pos + 1, data + size);

        // Find next start code to determine actual NAL size
        size_t end_pos = pos + 1;
        while (end_pos < size) {
            if (end_pos + 4 <= size) {
                uint32_t val = (data[end_pos] << 24) | (data[end_pos+1] << 16) |
                                (data[end_pos+2] << 8) | data[end_pos+3];
                if (val == 0x00000001) break;
            }
            if (end_pos + 3 <= size) {
                if (data[end_pos] == 0 && data[end_pos+1] == 0 && data[end_pos+2] == 1) break;
            }
            end_pos++;
        }

        nal_data.resize(end_pos - pos - 1);
        result.push_back({nal_header, nal_data});

        pos = end_pos;
    }

    return result;
}

RtpPacketPtr RtpPacketizer::createSingleNalPacket(uint8_t nal_header, const std::vector<uint8_t>& nal_data, bool marker) {
    auto packet = std::make_shared<RtpPacket>();

    std::vector<uint8_t> payload;
    payload.reserve(nal_data.size() + 1);

    // Single NAL RTP payload = NAL header + NAL payload.
    payload.push_back(nal_header);
    payload.insert(payload.end(), nal_data.begin(), nal_data.end());

    packet->setPayloadType(payload_type_);
    packet->setSeqNum(seq_num_++);
    packet->setTimestamp(timestamp_);
    packet->setSsrc(ssrc_);
    packet->setMarker(marker ? 1 : 0);
    packet->setPayload(payload.data(), payload.size());

    return packet;
}

RtpPacketPtr RtpPacketizer::createFuAPacket(uint8_t nal_header, const std::vector<uint8_t>& frag_data,
                                              bool start, bool end, bool marker) {
    auto packet = std::make_shared<RtpPacket>();

    // FU indicator + FU header (2 bytes)
    uint8_t fu_indicator = static_cast<uint8_t>((nal_header & 0xE0) | NAL_TYPE_FU_A);
    uint8_t fu_header = 0;
    if (start) fu_header |= 0x80;  // S bit
    if (end) fu_header |= 0x40;    // E bit
    fu_header |= (nal_header & 0x1F);

    std::vector<uint8_t> payload;
    payload.reserve(frag_data.size() + 2);
    payload.push_back(fu_indicator);
    payload.push_back(fu_header);
    payload.insert(payload.end(), frag_data.begin(), frag_data.end());

    packet->setPayloadType(payload_type_);
    packet->setSeqNum(seq_num_++);
    packet->setTimestamp(timestamp_);
    packet->setSsrc(ssrc_);
    packet->setMarker(marker ? 1 : 0);
    packet->setPayload(payload.data(), payload.size());

    return packet;
}

}  // namespace lancast
