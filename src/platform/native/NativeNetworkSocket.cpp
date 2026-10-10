#include "core/NetworkSocket.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace Platform {
namespace {
#ifdef _WIN32
using Descriptor=SOCKET;
using AddressLength=int;
constexpr Descriptor INVALID_DESCRIPTOR=INVALID_SOCKET;
int lastError() { return WSAGetLastError(); }
bool pending(int error) { return error==WSAEWOULDBLOCK || error==WSAEINPROGRESS || error==WSAEALREADY; }
bool interrupted(int error) { return error==WSAEINTR; }
void destroy(Descriptor socket) { closesocket(socket); }
bool initialize() {
    struct Runtime { bool ready; Runtime() { WSADATA data; ready=WSAStartup(MAKEWORD(2,2),&data)==0; } ~Runtime() { if(ready) WSACleanup(); } };
    static Runtime runtime; return runtime.ready;
}
bool nonblocking(Descriptor socket) { u_long enabled=1; return ioctlsocket(socket,FIONBIO,&enabled)==0; }
#else
using Descriptor=int;
using AddressLength=socklen_t;
constexpr Descriptor INVALID_DESCRIPTOR=-1;
int lastError() { return errno; }
bool pending(int error) { return error==EAGAIN || error==EWOULDBLOCK || error==EINPROGRESS || error==EALREADY; }
bool interrupted(int error) { return error==EINTR; }
void destroy(Descriptor socket) { ::close(socket); }
bool initialize() { return true; }
bool nonblocking(Descriptor socket) {
    const int flags=fcntl(socket,F_GETFL,0);
    if(flags<0 || fcntl(socket,F_SETFL,flags|O_NONBLOCK)<0) return false;
    const int descriptorFlags=fcntl(socket,F_GETFD,0);
    return descriptorFlags>=0 && fcntl(socket,F_SETFD,descriptorFlags|FD_CLOEXEC)==0;
}
#endif
void options(Descriptor socket) {
    int enabled=1;
    setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&enabled),sizeof(enabled));
#ifdef SO_NOSIGPIPE
    setsockopt(socket,SOL_SOCKET,SO_NOSIGPIPE,reinterpret_cast<const char*>(&enabled),sizeof(enabled));
#endif
}
}
struct NetworkSocket::Impl {
    Descriptor descriptor=INVALID_DESCRIPTOR;
    std::string error;
    bool connecting=false;
    ~Impl() { if(descriptor!=INVALID_DESCRIPTOR) destroy(descriptor); }
    void fail(int code) { error="Network socket error "+std::to_string(code); }
};
NetworkSocket::NetworkSocket() : m_impl(std::make_unique<Impl>()) {}
NetworkSocket::~NetworkSocket()=default;
NetworkSocket::NetworkSocket(NetworkSocket&&) noexcept=default;
NetworkSocket& NetworkSocket::operator=(NetworkSocket&&) noexcept=default;
void NetworkSocket::close() {
    if(m_impl && m_impl->descriptor!=INVALID_DESCRIPTOR) { destroy(m_impl->descriptor); m_impl->descriptor=INVALID_DESCRIPTOR; }
    if(m_impl) m_impl->connecting=false;
}
const std::string& NetworkSocket::error() const { return m_impl->error; }
bool NetworkSocket::listen(uint16_t port, bool loopbackOnly) {
    close(); m_impl->error.clear();
    if(!initialize()) { m_impl->error="Cannot initialize network sockets"; return false; }
    // Prefer a dual-stack listener; IPv4-only systems still support LAN rooms.
    for (int family : {AF_INET6, AF_INET}) {
        m_impl->descriptor = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (m_impl->descriptor == INVALID_DESCRIPTOR) { m_impl->fail(lastError()); continue; }
        int reuse = 1;
#ifdef _WIN32
        const int reuseOption = SO_EXCLUSIVEADDRUSE;
#else
        const int reuseOption = SO_REUSEADDR;
#endif
        bool configured = setsockopt(m_impl->descriptor, SOL_SOCKET, reuseOption,
            reinterpret_cast<const char*>(&reuse), sizeof(reuse)) == 0;
        sockaddr_storage endpoint{}; AddressLength length;
        if (family == AF_INET6) {
            int ipv6Only = 0;
            configured = configured && setsockopt(m_impl->descriptor, IPPROTO_IPV6, IPV6_V6ONLY,
                reinterpret_cast<const char*>(&ipv6Only), sizeof(ipv6Only)) == 0;
            auto& address = *reinterpret_cast<sockaddr_in6*>(&endpoint);
            address.sin6_family = AF_INET6; address.sin6_port = htons(port);
            address.sin6_addr = loopbackOnly ? in6addr_loopback : in6addr_any;
            length = sizeof(address);
        } else {
            auto& address = *reinterpret_cast<sockaddr_in*>(&endpoint);
            address.sin_family = AF_INET; address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
            length = sizeof(address);
        }
        if (!configured) { m_impl->fail(lastError()); close(); continue; }
        if (!nonblocking(m_impl->descriptor)) { m_impl->fail(lastError()); close(); return false; }
        if (::bind(m_impl->descriptor, reinterpret_cast<sockaddr*>(&endpoint), length) != 0) {
            const int error = lastError(); m_impl->fail(error); close();
#ifdef _WIN32
            const bool unavailable = error == WSAEAFNOSUPPORT || error == WSAEADDRNOTAVAIL;
#else
            const bool unavailable = error == EAFNOSUPPORT || error == EADDRNOTAVAIL;
#endif
            if (family == AF_INET6 && unavailable) continue;
            return false;
        }
        if (::listen(m_impl->descriptor, 16) != 0) { m_impl->fail(lastError()); close(); return false; }
        m_impl->error.clear(); return true;
    }
    return false;
}

bool NetworkSocket::connect(const std::string& host, uint16_t port) {
    close(); m_impl->error.clear();
    if(!initialize() || host.empty() || !port) { m_impl->error="Invalid network endpoint"; return false; }
    std::string numericHost = host;
    uint32_t scope = 0;
    const auto zone = numericHost.find('%');
    if (zone != std::string::npos) {
        const auto suffix = numericHost.substr(zone + 1);
        if (suffix.empty() || suffix.size() > 10) { m_impl->error = "Invalid IPv6 scope"; return false; }
        uint64_t value = 0;
        for (char character : suffix) {
            if (character < '0' || character > '9') { m_impl->error = "IPv6 scope must be an interface index"; return false; }
            value = value * 10 + static_cast<unsigned>(character - '0');
        }
        if (!value || value > UINT32_MAX) { m_impl->error = "Invalid IPv6 scope"; return false; }
        scope = static_cast<uint32_t>(value); numericHost.resize(zone);
    }
    sockaddr_storage address{}; AddressLength size=0;
    auto* ipv4=reinterpret_cast<sockaddr_in*>(&address);
    auto* ipv6=reinterpret_cast<sockaddr_in6*>(&address);
    int family=AF_INET;
    if(inet_pton(AF_INET,numericHost.c_str(),&ipv4->sin_addr)==1) {
        if (scope) { m_impl->error = "IPv4 addresses cannot have a scope"; return false; }
        ipv4->sin_family=AF_INET; ipv4->sin_port=htons(port); size=sizeof(sockaddr_in);
    } else if(inet_pton(AF_INET6,numericHost.c_str(),&ipv6->sin6_addr)==1) {
        family=AF_INET6; ipv6->sin6_family=AF_INET6; ipv6->sin6_port=htons(port); ipv6->sin6_scope_id=scope; size=sizeof(sockaddr_in6);
    } else { m_impl->error="Enter a numeric IPv4 or IPv6 address"; return false; }
    m_impl->descriptor=::socket(family,SOCK_STREAM,IPPROTO_TCP);
    if(m_impl->descriptor==INVALID_DESCRIPTOR) { m_impl->fail(lastError()); return false; }
    if(!nonblocking(m_impl->descriptor)) { m_impl->fail(lastError()); close(); return false; }
    options(m_impl->descriptor);
    if(::connect(m_impl->descriptor,reinterpret_cast<sockaddr*>(&address),size)==0) return true;
    const int error=lastError();
    if(pending(error)) { m_impl->connecting=true; return true; }
    m_impl->fail(error); close(); return false;
}
SocketStatus NetworkSocket::connectionStatus() {
    if(m_impl->descriptor==INVALID_DESCRIPTOR) return SocketStatus::Closed;
    if(!m_impl->connecting) return SocketStatus::Ready;
#ifdef _WIN32
    fd_set writable,errors; FD_ZERO(&writable); FD_ZERO(&errors);
    FD_SET(m_impl->descriptor,&writable); FD_SET(m_impl->descriptor,&errors);
    timeval timeout{};
    const int result=select(0,nullptr,&writable,&errors,&timeout);
#else
    pollfd descriptor{m_impl->descriptor,POLLOUT,0};
    const int result=::poll(&descriptor,1,0);
#endif
    if(result==0) return SocketStatus::Pending;
    if(result<0) { const auto code=lastError(); if(interrupted(code)) return SocketStatus::Pending; m_impl->fail(code); close(); return SocketStatus::Failed; }
    int error=0; AddressLength length=sizeof(error);
    if(getsockopt(m_impl->descriptor,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)!=0) error=lastError();
    if(error) { m_impl->fail(error); close(); return SocketStatus::Failed; }
    m_impl->connecting=false; return SocketStatus::Ready;
}
std::unique_ptr<NetworkSocket> NetworkSocket::accept() {
    if(m_impl->descriptor==INVALID_DESCRIPTOR) return {};
    Descriptor accepted=::accept(m_impl->descriptor,nullptr,nullptr);
    if(accepted==INVALID_DESCRIPTOR) { const auto code=lastError(); if(!pending(code) && !interrupted(code)) m_impl->fail(code); return {}; }
    if(!nonblocking(accepted)) { m_impl->fail(lastError()); destroy(accepted); return {}; }
    options(accepted);
    auto result=std::make_unique<NetworkSocket>(); result->m_impl->descriptor=accepted; return result;
}
SocketResult NetworkSocket::receive(uint8_t* data, size_t capacity) {
    if(!capacity) return {SocketStatus::Ready,0};
    const auto status=connectionStatus(); if(status!=SocketStatus::Ready) return {status,0};
    const auto count=::recv(m_impl->descriptor,reinterpret_cast<char*>(data),static_cast<int>(std::min<size_t>(capacity,INT_MAX)),0);
    if(count>0) return {SocketStatus::Ready,static_cast<size_t>(count)};
    if(count==0) { close(); return {SocketStatus::Closed,0}; }
    const auto error=lastError(); if(pending(error) || interrupted(error)) return {SocketStatus::Pending,0};
    m_impl->fail(error); close(); return {SocketStatus::Failed,0};
}
SocketResult NetworkSocket::send(const uint8_t* data, size_t size) {
    if(!size) return {SocketStatus::Ready,0};
    const auto status=connectionStatus(); if(status!=SocketStatus::Ready) return {status,0};
#ifdef MSG_NOSIGNAL
    constexpr int flags=MSG_NOSIGNAL;
#else
    constexpr int flags=0;
#endif
    const auto count=::send(m_impl->descriptor,reinterpret_cast<const char*>(data),static_cast<int>(std::min<size_t>(size,INT_MAX)),flags);
    if(count>0) return {SocketStatus::Ready,static_cast<size_t>(count)};
    if(count==0) { close(); return {SocketStatus::Closed,0}; }
    const auto error=lastError(); if(pending(error) || interrupted(error)) return {SocketStatus::Pending,0};
    m_impl->fail(error); close(); return {SocketStatus::Failed,0};
}
uint16_t NetworkSocket::localPort() const {
    if(m_impl->descriptor==INVALID_DESCRIPTOR) return 0;
    sockaddr_storage address{}; AddressLength size=sizeof(address);
    if(getsockname(m_impl->descriptor,reinterpret_cast<sockaddr*>(&address),&size)!=0) return 0;
    if(address.ss_family==AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
}
}
