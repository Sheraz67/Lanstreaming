#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <sys/socket.h>
#include <netinet/in.h>

namespace lancast {

struct Endpoint {
    std::string ip;
    uint16_t port = 0;
    sockaddr_in to_sockaddr() const;
    static Endpoint from_sockaddr(const sockaddr_in& addr);
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    bool bind(uint16_t port);
    bool set_nonblocking(bool nonblocking);
    bool set_recv_buffer(int size);
    bool set_send_buffer(int size);

    // Send data to endpoint. Returns bytes sent or -1.
    ssize_t send_to(const uint8_t* data, size_t len, const Endpoint& dest);
    ssize_t send_to(const std::vector<uint8_t>& data, const Endpoint& dest);

    // Receive data. Returns bytes received and source endpoint, or nullopt on timeout/error.
    struct RecvResult {
        std::vector<uint8_t> data;
        Endpoint source;
    };
    std::optional<RecvResult> recv_from(size_t max_size = 1500);

    // Set receive timeout in milliseconds
    bool set_recv_timeout(int ms);

    int fd() const { return fd_; }
    bool is_valid() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

} // namespace lancast
