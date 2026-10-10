#include "core/LanDiscovery.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <utility>

#if !defined(__APPLE__) && !defined(__ANDROID__)
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <mdns.h>

namespace Platform {
namespace {
constexpr char SERVICE[] = "_minecraftc._tcp.local.";
mdns_string_t string(const std::string& value) { return {value.data(), value.size()}; }
std::string string(mdns_string_t value) { return value.str ? std::string(value.str, value.length) : std::string{}; }
bool suffix(const std::string& value) {
    return value.size() > sizeof(SERVICE) && value.compare(value.size() - (sizeof(SERVICE) - 1), sizeof(SERVICE) - 1, SERVICE) == 0;
}
bool text(const std::string& value, size_t limit) {
    return !value.empty() && value.size() <= limit && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
uint32_t number(const std::string& value, uint32_t maximum) {
    if (value.empty() || value.size() > 10) return 0;
    uint64_t result = 0;
    for (char c : value) { if (c < '0' || c > '9') return 0; result = result * 10 + static_cast<unsigned>(c - '0'); }
    return result <= maximum ? static_cast<uint32_t>(result) : 0;
}
}
struct LanDiscovery::Impl {
    struct Interface { int socket = -1; sockaddr_storage address{}; };
    struct Cached { LanDiscoveredRoom room; std::string host; double expiry = 0, lastQuery = -100; };
    struct Host { std::string address; double expiry = 0; };
    std::vector<Interface> interfaces;
    std::map<std::string, Cached> cache;
    std::map<std::string, Host> hosts;
    std::vector<LanDiscoveredRoom> rooms;
    std::string error, instance, hostname;
    LanAdvertisement advertisement;
    bool browsing = false, advertising = false, dirty = false;
    double now = 0, lastQuery = -100, lastAnnouncement = -100;
    size_t answers = 0;
    ~Impl() { for (auto& nic : interfaces) mdns_socket_close(nic.socket); }
    void open(const sockaddr* source) {
        if (interfaces.size() >= 16) return;
        Interface nic;
        if (source->sa_family == AF_INET) {
            auto address = *reinterpret_cast<const sockaddr_in*>(source); address.sin_port = htons(MDNS_PORT);
            nic.socket = mdns_socket_open_ipv4(&address);
            std::memcpy(&nic.address, &address, sizeof(address));
        } else if (source->sa_family == AF_INET6) {
            auto address = *reinterpret_cast<const sockaddr_in6*>(source); address.sin6_port = htons(MDNS_PORT);
            nic.socket = mdns_socket_open_ipv6(&address);
            std::memcpy(&nic.address, &address, sizeof(address));
        }
        if (nic.socket >= 0) interfaces.push_back(nic);
    }
    void releaseIdle() {
        if (browsing || advertising) return;
        for (const auto& nic : interfaces) mdns_socket_close(nic.socket);
        interfaces.clear();
    }
    bool initialize() {
        if (!interfaces.empty()) return true;
#ifdef _WIN32
        struct Winsock { bool ready; Winsock() { WSADATA data; ready = WSAStartup(MAKEWORD(2, 2), &data) == 0; } ~Winsock() { if (ready) WSACleanup(); } };
        static Winsock winsock;
        if (!winsock.ready) { error = "Cannot initialize discovery sockets"; return false; }
        ULONG bytes = 16384;
        std::vector<uint8_t> buffer(bytes);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &bytes);
        if (result == ERROR_BUFFER_OVERFLOW && bytes <= 1024 * 1024) {
            buffer.resize(bytes); adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
            result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &bytes);
        }
        if (result == NO_ERROR) for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || (adapter->Flags & IP_ADAPTER_NO_MULTICAST)) continue;
            for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next)
                if (address->DadState == IpDadStatePreferred) open(address->Address.lpSockaddr);
        }
#else
        ifaddrs* addresses = nullptr;
        if (getifaddrs(&addresses) == 0) {
            for (auto* address = addresses; address; address = address->ifa_next) {
                if (!address->ifa_addr || !(address->ifa_flags & IFF_UP) || !(address->ifa_flags & IFF_MULTICAST) ||
                    (address->ifa_flags & IFF_LOOPBACK)) continue;
                open(address->ifa_addr);
            }
            freeifaddrs(addresses);
        }
#endif
        if (interfaces.empty()) { error = "No multicast LAN nic is available"; return false; }
        error.clear(); return true;
    }
    std::pair<mdns_record_t, std::vector<mdns_record_t>> records(const Interface& nic) const {
        mdns_record_t ptr{}; ptr.name = {SERVICE, sizeof(SERVICE) - 1}; ptr.type = MDNS_RECORDTYPE_PTR; ptr.data.ptr.name = string(instance);
        std::vector<mdns_record_t> additional;
        mdns_record_t srv{}; srv.name = string(instance); srv.type = MDNS_RECORDTYPE_SRV;
        srv.data.srv.name = string(hostname); srv.data.srv.port = advertisement.port; additional.push_back(srv);
        mdns_record_t address{}; address.name = string(hostname);
        if (nic.address.ss_family == AF_INET) { address.type = MDNS_RECORDTYPE_A; address.data.a.addr = *reinterpret_cast<const sockaddr_in*>(&nic.address); }
        else { address.type = MDNS_RECORDTYPE_AAAA; address.data.aaaa.addr = *reinterpret_cast<const sockaddr_in6*>(&nic.address); }
        additional.push_back(address);
        return {ptr, additional};
    }
    // All string backing lives through the synchronous mDNS write.
    template<class Send> void send(const Interface& nic, Send write) const {
        auto record = records(nic);
        const std::array<std::pair<std::string, std::string>, 7> metadata{{
            {"name", advertisement.name}, {"version", advertisement.version}, {"protocol", std::to_string(advertisement.protocol)},
            {"generation", std::to_string(advertisement.generation)}, {"players", std::to_string(advertisement.players)},
            {"capacity", std::to_string(advertisement.capacity)}, {"pvp", advertisement.pvp ? "1" : "0"}}};
        for (const auto& pair : metadata) {
            mdns_record_t txt{}; txt.name = string(instance); txt.type = MDNS_RECORDTYPE_TXT;
            txt.data.txt.key = string(pair.first); txt.data.txt.value = string(pair.second); record.second.push_back(txt);
        }
        std::array<uint8_t, 4096> buffer{};
        write(buffer, record.first, record.second);
    }
    static int callback(int socket, const sockaddr* from, size_t fromLength, mdns_entry_type_t entry,
        uint16_t query, uint16_t type, uint16_t recordClass, uint32_t ttl, const void* data, size_t size,
        size_t nameOffset, size_t, size_t recordOffset, size_t recordLength, void* user) {
        auto& self = *static_cast<Impl*>(user);
        std::array<char, 256> nameBuffer{};
        auto offset = nameOffset;
        const auto name = string(mdns_string_extract(data, size, &offset, nameBuffer.data(), nameBuffer.size()));
        if (entry == MDNS_ENTRYTYPE_QUESTION) {
            if (!self.advertising || self.answers >= 8 || (name != SERVICE && name != self.instance && name != self.hostname)) return 0;
            const auto found = std::find_if(self.interfaces.begin(), self.interfaces.end(), [socket](const Interface& nic) { return nic.socket == socket; });
            if (found == self.interfaces.end()) return 0;
            ++self.answers;
            self.send(*found, [&](auto& buffer, const auto& ptr, const auto& additional) {
                if (recordClass & MDNS_UNICAST_RESPONSE)
                    mdns_query_answer_unicast(socket, from, fromLength, buffer.data(), buffer.size(), query,
                        static_cast<mdns_record_type_t>(type), name.data(), name.size(), ptr, nullptr, 0, additional.data(), additional.size());
                else mdns_query_answer_multicast(socket, buffer.data(), buffer.size(), ptr, nullptr, 0, additional.data(), additional.size());
            });
            return 0;
        }
        if (!self.browsing) return 0;
        if (type == MDNS_RECORDTYPE_PTR && name == SERVICE) {
            const auto target = string(mdns_record_parse_ptr(data, size, recordOffset, recordLength, nameBuffer.data(), nameBuffer.size()));
            if (!suffix(target)) return 0;
            if (!ttl) { self.cache.erase(target); return 0; }
            if (self.cache.size() >= 64 && !self.cache.count(target)) return 0;
            auto& cached = self.cache[target]; cached.room.instance = target;
            cached.expiry = self.now + std::min<uint32_t>(ttl, 15);
            if (self.now - cached.lastQuery >= 5 && self.answers < 8) {
                std::array<uint8_t, 1024> queryBuffer{};
                mdns_query_send(socket, MDNS_RECORDTYPE_ANY, target.data(), target.size(), queryBuffer.data(), queryBuffer.size(), 0);
                cached.lastQuery = self.now; ++self.answers;
            }
            return 0;
        }
        if (!ttl) { self.cache.erase(name); self.hosts.erase(name); return 0; }
        const double expiry = self.now + std::min<uint32_t>(ttl, 15);
        if (type == MDNS_RECORDTYPE_A || type == MDNS_RECORDTYPE_AAAA) {
            if (self.hosts.size() >= 64 && !self.hosts.count(name)) return 0;
            std::array<char, INET6_ADDRSTRLEN> buffer{};
            std::string address;
            if (type == MDNS_RECORDTYPE_A) {
                sockaddr_in parsed{}; if (recordLength != 4) return 0;
                mdns_record_parse_a(data, size, recordOffset, recordLength, &parsed);
                if (inet_ntop(AF_INET, &parsed.sin_addr, buffer.data(), buffer.size())) address = buffer.data();
            } else {
                sockaddr_in6 parsed{}; if (recordLength != 16) return 0;
                mdns_record_parse_aaaa(data, size, recordOffset, recordLength, &parsed);
                if (inet_ntop(AF_INET6, &parsed.sin6_addr, buffer.data(), buffer.size())) address = buffer.data();
                if (IN6_IS_ADDR_LINKLOCAL(&parsed.sin6_addr) && from->sa_family == AF_INET6)
                    address += "%" + std::to_string(reinterpret_cast<const sockaddr_in6*>(from)->sin6_scope_id);
            }
            if (!address.empty() && (type == MDNS_RECORDTYPE_A || !self.hosts.count(name))) self.hosts[name] = {address, expiry};
            return 0;
        }
        if (!suffix(name) || (self.cache.size() >= 64 && !self.cache.count(name))) return 0;
        auto& cached = self.cache[name]; cached.room.instance = name; cached.expiry = expiry;
        if (type == MDNS_RECORDTYPE_SRV) {
            const auto srv = mdns_record_parse_srv(data, size, recordOffset, recordLength, nameBuffer.data(), nameBuffer.size());
            cached.host = string(srv.name); cached.room.port = srv.port;
            if (!cached.host.empty() && !self.hosts.count(cached.host) && self.answers < 8) {
                std::array<uint8_t, 1024> queryBuffer{};
                mdns_query_send(socket, MDNS_RECORDTYPE_ANY, cached.host.data(), cached.host.size(), queryBuffer.data(), queryBuffer.size(), 0);
                ++self.answers;
            }
        } else if (type == MDNS_RECORDTYPE_TXT) {
            std::array<mdns_record_txt_t, 16> records{};
            const auto count = mdns_record_parse_txt(data, size, recordOffset, recordLength, records.data(), records.size());
            for (size_t i = 0; i < count; ++i) {
                const auto key = string(records[i].key), value = string(records[i].value);
                if (key == "name" && text(value, 128)) cached.room.name = value;
                else if (key == "version" && text(value, 64)) cached.room.version = value;
                else if (key == "protocol") cached.room.protocol = static_cast<uint16_t>(number(value, 65535));
                else if (key == "generation") cached.room.generation = number(value, UINT32_MAX);
                else if (key == "players") cached.room.players = static_cast<uint8_t>(number(value, 8));
                else if (key == "capacity") cached.room.capacity = static_cast<uint8_t>(number(value, 8));
                else if (key == "pvp") cached.room.pvp = value == "1";
            }
        }
        return 0;
    }
    void announce(bool goodbye) const {
        for (const auto& nic : interfaces) send(nic, [&](auto& buffer, const auto& ptr, const auto& additional) {
            if (goodbye) mdns_goodbye_multicast(nic.socket, buffer.data(), buffer.size(), ptr, nullptr, 0, additional.data(), additional.size());
            else mdns_announce_multicast(nic.socket, buffer.data(), buffer.size(), ptr, nullptr, 0, additional.data(), additional.size());
        });
    }
};
LanDiscovery::LanDiscovery() : m_impl(std::make_unique<Impl>()) {}
LanDiscovery::~LanDiscovery() { stopAdvertising(); }
bool LanDiscovery::browse() { m_impl->browsing = m_impl->initialize(); m_impl->lastQuery = -100; return m_impl->browsing; }
void LanDiscovery::stopBrowsing() { m_impl->browsing = false; m_impl->cache.clear(); m_impl->hosts.clear(); m_impl->rooms.clear(); m_impl->releaseIdle(); }
bool LanDiscovery::advertise(const LanAdvertisement& room) {
    if (room.instance.size() != 32 || !std::all_of(room.instance.begin(), room.instance.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }) ||
        !text(room.name, 128) || !text(room.version, 64) || !room.port || !room.protocol || !room.generation || room.capacity < 2 || room.capacity > 8 || room.players < 1 || room.players > room.capacity) {
        m_impl->error = "Invalid LAN advertisement"; return false;
    }
    if (!m_impl->initialize()) return false;
    if (m_impl->advertising && (m_impl->advertisement.instance != room.instance || m_impl->advertisement.port != room.port)) stopAdvertising();
    const auto& old = m_impl->advertisement;
    m_impl->dirty = m_impl->dirty || !m_impl->advertising || old.name != room.name || old.version != room.version || old.protocol != room.protocol ||
        old.generation != room.generation || old.players != room.players || old.capacity != room.capacity || old.pvp != room.pvp;
    m_impl->advertisement = room; m_impl->instance = "mc-" + room.instance + "." + SERVICE;
    m_impl->hostname = "mc-" + room.instance + ".local."; m_impl->advertising = true; return true;
}
void LanDiscovery::stopAdvertising() { if (m_impl->advertising) m_impl->announce(true); m_impl->advertising = false; m_impl->releaseIdle(); }
void LanDiscovery::poll(double now) {
    m_impl->now = now; m_impl->answers = 0;
    if (!m_impl->browsing && !m_impl->advertising) return;
    if (m_impl->browsing && now - m_impl->lastQuery >= 5) {
        std::array<uint8_t, 1024> buffer{};
        for (const auto& nic : m_impl->interfaces) mdns_query_send(nic.socket, MDNS_RECORDTYPE_PTR, SERVICE, sizeof(SERVICE) - 1, buffer.data(), buffer.size(), 0);
        m_impl->lastQuery = now;
    }
    if (m_impl->advertising && (m_impl->dirty || now - m_impl->lastAnnouncement >= 5)) {
        m_impl->announce(false); m_impl->dirty = false; m_impl->lastAnnouncement = now;
    }
    std::array<uint8_t, 4096> buffer{};
    size_t packets = 0;
    for (const auto& nic : m_impl->interfaces) for (size_t i = 0; i < 8 && packets < 32; ++i) {
        if (!mdns_socket_listen(nic.socket, buffer.data(), buffer.size(), Impl::callback, m_impl.get())) break;
        ++packets;
    }
    for (auto it = m_impl->cache.begin(); it != m_impl->cache.end();) {
        if (it->second.expiry <= now) it = m_impl->cache.erase(it); else ++it;
    }
    for (auto it = m_impl->hosts.begin(); it != m_impl->hosts.end();) {
        if (it->second.expiry <= now) it = m_impl->hosts.erase(it); else ++it;
    }
    m_impl->rooms.clear();
    for (const auto& entry : m_impl->cache) {
        auto room = entry.second.room; const auto host = m_impl->hosts.find(entry.second.host);
        if (host == m_impl->hosts.end() || room.name.empty() || room.version.empty() || !room.port || !room.protocol || !room.generation ||
            room.capacity < 2 || room.capacity > 8 || room.players < 1 || room.players > room.capacity) continue;
        room.address = host->second.address; m_impl->rooms.push_back(std::move(room));
    }
}
const std::vector<LanDiscoveredRoom>& LanDiscovery::rooms() const { return m_impl->rooms; }
const std::string& LanDiscovery::error() const { return m_impl->error; }
}
#else
// Platform service adapters are implemented separately; do not require raw
// multicast entitlements or ship desktop DNS-SD assumptions on mobile.
#endif
