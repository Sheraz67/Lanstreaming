#include "net/socket.h"
#include "core/logger.h"

#ifdef _WIN32
#  include <cstring>
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <arpa/inet.h>
#  include <cerrno>
#  include <cstring>
#endif

namespace lancast {

static constexpr const char* TAG = "Socket";

#ifdef _WIN32
static std::string wsa_error_string(int err) {
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    // Strip trailing newline
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return std::string(buf);
}
static std::string last_error_string() { return wsa_error_string(WSAGetLastError()); }
#else
static std::string last_error_string() { return strerror(errno); }
#endif

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
    if (fd_ == INVALID_SOCK) {
        LOG_ERROR(TAG, "Failed to create socket: %s", last_error_string().c_str());
    }
}

UdpSocket::~UdpSocket() {
    if (fd_ != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = INVALID_SOCK;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        if (fd_ != INVALID_SOCK) {
#ifdef _WIN32
            closesocket(fd_);
#else
            close(fd_);
#endif
        }
        fd_ = other.fd_;
        other.fd_ = INVALID_SOCK;
    }
    return *this;
}

bool UdpSocket::bind(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int reuse = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR(TAG, "Bind to port %u failed: %s", port, last_error_string().c_str());
        return false;
    }
    LOG_INFO(TAG, "Bound to port %u", port);
    return true;
}

bool UdpSocket::set_nonblocking(bool nonblocking) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(fd_, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) return false;
    if (nonblocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    return fcntl(fd_, F_SETFL, flags) >= 0;
#endif
}

bool UdpSocket::set_recv_buffer(int size) {
    return setsockopt(fd_, SOL_SOCKET, SO_RCVBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size)) >= 0;
}

bool UdpSocket::set_send_buffer(int size) {
    return setsockopt(fd_, SOL_SOCKET, SO_SNDBUF,
                      reinterpret_cast<const char*>(&size), sizeof(size)) >= 0;
}

ssize_t UdpSocket::send_to(const uint8_t* data, size_t len, const Endpoint& dest) {
    sockaddr_in addr = dest.to_sockaddr();
    return sendto(fd_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

ssize_t UdpSocket::send_to(const std::vector<uint8_t>& data, const Endpoint& dest) {
    return send_to(data.data(), data.size(), dest);
}

std::optional<UdpSocket::RecvResult> UdpSocket::recv_from(size_t max_size) {
    std::vector<uint8_t> buf(max_size);
    sockaddr_in src_addr{};
#ifdef _WIN32
    int addr_len = sizeof(src_addr);
#else
    socklen_t addr_len = sizeof(src_addr);
#endif

    ssize_t n = recvfrom(fd_, reinterpret_cast<char*>(buf.data()),
                         static_cast<int>(buf.size()), 0,
                         reinterpret_cast<sockaddr*>(&src_addr), &addr_len);
    if (n <= 0) return std::nullopt;

    buf.resize(static_cast<size_t>(n));
    return RecvResult{std::move(buf), Endpoint::from_sockaddr(src_addr)};
}

bool UdpSocket::set_recv_timeout(int ms) {
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(ms);
    return setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&timeout), sizeof(timeout)) >= 0;
#else
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) >= 0;
#endif
}

} // namespace lancast
