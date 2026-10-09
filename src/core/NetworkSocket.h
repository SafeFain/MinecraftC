#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Platform {
enum class SocketStatus { Ready, Pending, Closed, Failed };
struct SocketResult { SocketStatus status; size_t transferred = 0; };
// Numeric IPv4/IPv6 endpoints only: creating a connection never performs a
// blocking DNS lookup. OS descriptors and error handling remain in the adapter.
class NetworkSocket {
public:
    NetworkSocket();
    ~NetworkSocket();
    NetworkSocket(NetworkSocket&&) noexcept;
    NetworkSocket& operator=(NetworkSocket&&) noexcept;
    NetworkSocket(const NetworkSocket&) = delete;
    NetworkSocket& operator=(const NetworkSocket&) = delete;
    bool listen(uint16_t port, bool loopbackOnly = false);
    bool connect(const std::string& address, uint16_t port);
    SocketStatus connectionStatus();
    std::unique_ptr<NetworkSocket> accept();
    SocketResult receive(uint8_t* data, size_t capacity);
    SocketResult send(const uint8_t* data, size_t size);
    uint16_t localPort() const;
    const std::string& error() const;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
