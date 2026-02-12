#include "net/socket.h"
#include "core/logger.h"
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>

namespace lancast {

static constexpr const char* TAG = "Socket";

sockaddr_in Endpoint::to_sockaddr() const {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    return addr;
}

Endpoint Endpoint::from_sockaddr(const sockaddr_in& addr) {
    Endpoint ep;
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    ep.ip = buf;
    ep.port = ntohs(addr.sin_port);
    return ep;
}

UdpSocket::UdpSocket() {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        LOG_ERROR(TAG, "Failed to create socket: %s", strerror(errno));
    }
}

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool UdpSocket::bind(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int reuse = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR(TAG, "Bind to port %u failed: %s", port, strerror(errno));
        return false;
    }
    LOG_INFO(TAG, "Bound to port %u", port);
    return true;
}

bool UdpSocket::set_nonblocking(bool nonblocking) {
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) return false;
    if (nonblocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    return fcntl(fd_, F_SETFL, flags) >= 0;
}

bool UdpSocket::set_recv_buffer(int size) {
    return setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) >= 0;
}

bool UdpSocket::set_send_buffer(int size) {
    return setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) >= 0;
}

ssize_t UdpSocket::send_to(const uint8_t* data, size_t len, const Endpoint& dest) {
    sockaddr_in addr = dest.to_sockaddr();
    return sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

ssize_t UdpSocket::send_to(const std::vector<uint8_t>& data, const Endpoint& dest) {
    return send_to(data.data(), data.size(), dest);
}

std::optional<UdpSocket::RecvResult> UdpSocket::recv_from(size_t max_size) {
    std::vector<uint8_t> buf(max_size);
    sockaddr_in src_addr{};
    socklen_t addr_len = sizeof(src_addr);

    ssize_t n = recvfrom(fd_, buf.data(), buf.size(), 0,
                         reinterpret_cast<sockaddr*>(&src_addr), &addr_len);
    if (n <= 0) return std::nullopt;

    buf.resize(static_cast<size_t>(n));
    return RecvResult{std::move(buf), Endpoint::from_sockaddr(src_addr)};
}

bool UdpSocket::set_recv_timeout(int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) >= 0;
}

} // namespace lancast
