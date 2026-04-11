#pragma once

#include "Common/RoomInfo.h"
#include "Common/CircularBuffer.h"
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <functional>

namespace lancast {

// RoomDiscovery handles UDP broadcast-based room advertisement and discovery
// No central server needed - peer to peer discovery
class RoomDiscovery {
public:
    static constexpr uint16_t DISCOVERY_PORT = 45678;
    static constexpr uint8_t  DISCOVERY_VERSION = 1;
    static constexpr uint8_t  DEFAULT_TTL = 5;
    static constexpr int      ADVERTISEMENT_INTERVAL_MS = 1000;
    static constexpr int      QUERY_INTERVAL_MS = 2000;

    RoomDiscovery();
    ~RoomDiscovery();

    // Start broadcasting as a host (advertise our room)
    bool startBroadcast(const std::string& room_id, const std::string& room_name,
                        uint16_t stream_port);

    // Stop broadcasting
    void stopBroadcast();

    // Send a discovery query (to find existing rooms)
    void sendQuery();

    // Get list of discovered rooms
    std::vector<RoomInfo> getRooms();

    // Set callback for new room discovered
    using RoomDiscoveredCallback = std::function<void(const RoomInfo&)>;
    void setCallback(RoomDiscoveredCallback cb) { on_room_discovered_ = cb; }

    // Check if we're broadcasting
    bool isBroadcasting() const { return broadcasting_; }

    // Check if discovery is running
    bool isDiscovering() const { return discovering_; }

    // Get our own broadcast info (when broadcasting)
    RoomInfo ourRoomInfo() const { return our_info_; }

private:
    void discoveryThreadFunc();
    void processReceivedPacket(const uint8_t* data, size_t len,
                                 const std::string& src_ip, uint16_t src_port);

    // Socket for discovery
    int socket_ = -1;

    // Broadcast thread
    std::thread discovery_thread_;
    std::atomic<bool> running_{false};

    // Our room info (when broadcasting)
    RoomInfo our_info_;
    bool broadcasting_ = false;

    // Discovery state
    bool discovering_ = false;
    std::atomic<int64_t> last_query_time_{0};

    // Discovered rooms map (keyed by room_id to avoid duplicates)
    std::map<std::string, RoomInfo> discovered_rooms_;

    // Thread safety
    std::mutex rooms_mutex_;

    // Callback
    RoomDiscoveredCallback on_room_discovered_;
};

using RoomDiscoveryPtr = std::shared_ptr<RoomDiscovery>;

}  // namespace lancast
