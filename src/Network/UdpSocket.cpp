#include "Network/UdpSocket.h"
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace lancast {

UdpSocket::UdpSocket() {
    // Initialize Winsock is done in NetworkManager or main
}

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::bind(uint16_t port) {
    return bind("0.0.0.0", port);
}

bool UdpSocket::bind(const std::string& ip, uint16_t port) {
    if (socket_ != INVALID_SOCKET) {
        close();
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    }

    if (::bind(socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    return true;
}

bool UdpSocket::sendTo(const std::vector<uint8_t>& data, const std::string& ip, uint16_t port) {
    return sendTo(data.data(), data.size(), ip, port);
}

bool UdpSocket::sendTo(const uint8_t* data, size_t len, const std::string& ip, uint16_t port) {
    if (socket_ == INVALID_SOCKET) return false;

    sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &dest.sin_addr);

    int result = ::sendto(socket_, (const char*)data, (int)len, 0,
                          (sockaddr*)&dest, sizeof(dest));

    return result == (int)len;
}

int UdpSocket::recvFrom(std::vector<uint8_t>& buffer, std::string& src_ip, uint16_t& src_port) {
    return recvFrom(buffer, src_ip, src_port, -1);  // Blocking
}

int UdpSocket::recvFrom(std::vector<uint8_t>& buffer, std::string& src_ip, uint16_t& src_port, int timeout_ms) {
    if (socket_ == INVALID_SOCKET) return -1;

    if (timeout_ms >= 0) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_, &read_fds);

        timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int result = select(0, &read_fds, nullptr, nullptr, &tv);
        if (result <= 0) return result;  // Timeout or error
    }

    // Max UDP packet size is typically 65507
    buffer.resize(65507);

    sockaddr_in from;
    int from_len = sizeof(from);
    int result = ::recvfrom(socket_, (char*)buffer.data(), (int)buffer.size(), 0,
                             (sockaddr*)&from, &from_len);

    if (result > 0) {
        buffer.resize(result);

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
        src_ip = ip_str;
        src_port = ntohs(from.sin_port);
    }

    return result;
}

bool UdpSocket::setNonBlocking(bool enable) {
    if (socket_ == INVALID_SOCKET) return false;

    u_long mode = enable ? 1 : 0;
    return ioctlsocket(socket_, FIONBIO, &mode) == 0;
}

bool UdpSocket::setBroadcast(bool enable) {
    if (socket_ == INVALID_SOCKET) return false;

    BOOL val = enable ? TRUE : FALSE;
    return setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, (const char*)&val, sizeof(val)) == 0;
}

void UdpSocket::close() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

uint16_t UdpSocket::localPort() const {
    if (socket_ == INVALID_SOCKET) return 0;

    sockaddr_in addr;
    int len = sizeof(addr);
    if (getsockname(socket_, (sockaddr*)&addr, &len) == 0) {
        return ntohs(addr.sin_port);
    }
    return 0;
}

}  // namespace lancast
