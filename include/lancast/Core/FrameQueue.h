#pragma once

#include "Common/CircularBuffer.h"
#include "Common/FrameBuffer.h"
#include "Common/RtpPacket.h"

namespace lancast {

// Typedef aliases for frame queues used in the pipeline
using VideoFrameQueue = CircularBuffer<VideoFramePtr>;
using EncodedFrameQueue = CircularBuffer<EncodedFramePtr>;
using RtpPacketQueue = CircularBuffer<RtpPacketPtr>;

}  // namespace lancast
