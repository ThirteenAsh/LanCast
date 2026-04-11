#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <winsock2.h>

namespace lancast {

// UDP socket wrapper for sending/receiving RTP and discovery packets
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    // Bind to a specific port (0 = any available)
    bool bind(uint16_t port);

    // Bind to a specific address (for receiving on a specific interface)
    bool bind(const std::string& ip, uint16_t port);

    // Send data to a specific destination
    bool sendTo(const std::vector<uint8_t>& data, const std::string& ip, uint16_t port);

    // Send data to a specific destination (raw pointer)
    bool sendTo(const uint8_t* data, size_t len, const std::string& ip, uint16_t port);

    // Receive data (blocking)
    // Returns number of bytes received, 0 on timeout, -1 on error
    int recvFrom(std::vector<uint8_t>& buffer, std::string& src_ip, uint16_t& src_port);

    // Receive with timeout (ms)
    int recvFrom(std::vector<uint8_t>& buffer, std::string& src_ip, uint16_t& src_port, int timeout_ms);

    // Set socket to non-blocking mode
    bool setNonBlocking(bool enable);

    // Enable broadcast on this socket
    bool setBroadcast(bool enable);

    // Close the socket
    void close();

    // Check if socket is valid
    bool isValid() const { return socket_ != INVALID_SOCKET; }

    // Get local port
    uint16_t localPort() const;

    SOCKET socket() const { return socket_; }

private:
    SOCKET socket_ = INVALID_SOCKET;
};

using UdpSocketPtr = std::shared_ptr<UdpSocket>;

}  // namespace lancast
