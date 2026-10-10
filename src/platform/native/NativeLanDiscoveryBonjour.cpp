#include "core/LanDiscovery.h"
#if defined(__APPLE__)
#include "platform/native/LanDiscoveryValidation.h"
#include <dns_sd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <array>
#include <map>

namespace Platform {
namespace {
void release(DNSServiceRef& ref) { if (ref) DNSServiceRefDeallocate(ref); ref = nullptr; }
std::string txt(uint16_t length, const void* data, const char* key) {
    uint8_t size = 0;
    const auto* value = static_cast<const char*>(TXTRecordGetValuePtr(length, data, key, &size));
    return value ? std::string(value, size) : std::string{};
}
}
struct LanDiscovery::Impl {
    struct Candidate {
        Impl* owner = nullptr;
        LanDiscoveredRoom room;
        DNSServiceRef resolve = nullptr, address = nullptr;
        uint32_t interface = 0;
        bool removed = false;
        ~Candidate() { release(resolve); release(address); }
    };
    DNSServiceRef browse = nullptr, registration = nullptr;
    std::map<std::string, std::unique_ptr<Candidate>> cache;
    std::vector<LanDiscoveredRoom> rooms;
    std::string error, advertisedInstance;
    std::vector<uint8_t> advertisedTxt;
    uint16_t advertisedPort = 0;
    ~Impl() { release(browse); release(registration); }
    void failure(DNSServiceErrorType code) { if (code) error = "Bonjour error " + std::to_string(code); }
    static void addressReply(DNSServiceRef, DNSServiceFlags flags, uint32_t interface, DNSServiceErrorType error,
                             const char*, const sockaddr* address, uint32_t, void* context) {
        auto& candidate = *static_cast<Candidate*>(context);
        if (error || !address) { candidate.owner->failure(error); return; }
        std::array<char, INET6_ADDRSTRLEN> buffer{};
        std::string numeric;
        if (address->sa_family == AF_INET) {
            const auto& ipv4 = *reinterpret_cast<const sockaddr_in*>(address);
            if (inet_ntop(AF_INET, &ipv4.sin_addr, buffer.data(), buffer.size())) numeric = buffer.data();
        } else if (address->sa_family == AF_INET6) {
            const auto& ipv6 = *reinterpret_cast<const sockaddr_in6*>(address);
            if (inet_ntop(AF_INET6, &ipv6.sin6_addr, buffer.data(), buffer.size())) numeric = buffer.data();
            if (IN6_IS_ADDR_LINKLOCAL(&ipv6.sin6_addr)) numeric += "%" + std::to_string(interface);
        }
        if (!(flags & kDNSServiceFlagsAdd)) {
            if (candidate.room.address == numeric) candidate.room.address.clear();
        } else if (!numeric.empty() && (candidate.room.address.empty() || address->sa_family == AF_INET)) candidate.room.address = numeric;
    }
    static void resolveReply(DNSServiceRef, DNSServiceFlags, uint32_t interface, DNSServiceErrorType error,
                             const char*, const char* host, uint16_t port, uint16_t txtLength, const unsigned char* data, void* context) {
        auto& candidate = *static_cast<Candidate*>(context);
        if (error || !host) { candidate.owner->failure(error); return; }
        auto& room = candidate.room;
        room.port = ntohs(port); room.name = txt(txtLength, data, "name"); room.version = txt(txtLength, data, "version");
        room.protocol = static_cast<uint16_t>(DiscoveryValidation::number(txt(txtLength, data, "protocol"), 65535));
        room.generation = DiscoveryValidation::number(txt(txtLength, data, "generation"), UINT32_MAX);
        room.players = static_cast<uint8_t>(DiscoveryValidation::number(txt(txtLength, data, "players"), 8));
        room.capacity = static_cast<uint8_t>(DiscoveryValidation::number(txt(txtLength, data, "capacity"), 8));
        room.pvp = txt(txtLength, data, "pvp") == "1";
        if (!candidate.address) candidate.owner->failure(DNSServiceGetAddrInfo(&candidate.address, 0, interface,
            kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, host, addressReply, &candidate));
    }
    static void browseReply(DNSServiceRef, DNSServiceFlags flags, uint32_t interface, DNSServiceErrorType error,
                            const char* name, const char* type, const char* domain, void* context) {
        auto& owner = *static_cast<Impl*>(context);
        if (error || !name || !type || !domain) { owner.failure(error); return; }
        std::array<char, kDNSServiceMaxDomainName> full{};
        if (DNSServiceConstructFullName(full.data(), name, type, domain)) return;
        const auto key = std::string(full.data()) + "@" + std::to_string(interface);
        if (!(flags & kDNSServiceFlagsAdd)) { const auto found = owner.cache.find(key); if (found != owner.cache.end()) found->second->removed = true; return; }
        if (owner.cache.count(key) || owner.cache.size() >= 64) return;
        auto candidate = std::make_unique<Candidate>(); candidate->owner = &owner; candidate->interface = interface;
        candidate->room.instance = key;
        owner.failure(DNSServiceResolve(&candidate->resolve, 0, interface, name, type, domain, resolveReply, candidate.get()));
        owner.cache.emplace(key, std::move(candidate));
    }
    static void registrationReply(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType error, const char*, const char*, const char*, void* context) {
        static_cast<Impl*>(context)->failure(error);
    }
    void process(DNSServiceRef ref) {
        if (!ref) return;
        pollfd descriptor{DNSServiceRefSockFD(ref), POLLIN, 0};
        for (int i = 0; i < 4; ++i) {
            if (::poll(&descriptor, 1, 0) <= 0 || !(descriptor.revents & POLLIN)) break;
            const auto result = DNSServiceProcessResult(ref); if (result) { failure(result); break; }
        }
    }
};
LanDiscovery::LanDiscovery() : m_impl(std::make_unique<Impl>()) {}
LanDiscovery::~LanDiscovery() = default;
bool LanDiscovery::browse() {
    stopBrowsing(); m_impl->error.clear();
    const auto result = DNSServiceBrowse(&m_impl->browse, 0, 0, "_minecraftc._tcp", "local.", Impl::browseReply, m_impl.get());
    m_impl->failure(result); return result == kDNSServiceErr_NoError;
}
void LanDiscovery::stopBrowsing() { release(m_impl->browse); m_impl->cache.clear(); m_impl->rooms.clear(); }
bool LanDiscovery::advertise(const LanAdvertisement& room) {
    if (!DiscoveryValidation::valid(room)) { m_impl->error = "Invalid LAN advertisement"; return false; }
    const std::array<std::pair<const char*, std::string>, 7> fields{{{"name", room.name}, {"version", room.version},
        {"protocol", std::to_string(room.protocol)}, {"generation", std::to_string(room.generation)},
        {"players", std::to_string(room.players)}, {"capacity", std::to_string(room.capacity)}, {"pvp", room.pvp ? "1" : "0"}}};
    TXTRecordRef record; TXTRecordCreate(&record, 0, nullptr);
    for (const auto& field : fields) {
        const auto result = TXTRecordSetValue(&record, field.first, static_cast<uint8_t>(field.second.size()), field.second.data());
        if (result) { TXTRecordDeallocate(&record); m_impl->failure(result); return false; }
    }
    const auto length = TXTRecordGetLength(&record);
    const auto* bytes = static_cast<const uint8_t*>(TXTRecordGetBytesPtr(&record));
    const std::vector<uint8_t> next(bytes, bytes + length); TXTRecordDeallocate(&record);
    if (m_impl->registration && m_impl->advertisedInstance == room.instance && m_impl->advertisedPort == room.port) {
        if (next != m_impl->advertisedTxt) {
            const auto result = DNSServiceUpdateRecord(m_impl->registration, nullptr, 0, static_cast<uint16_t>(next.size()), next.data(), 0);
            if (result) { m_impl->failure(result); return false; }
        }
    } else {
        stopAdvertising(); const std::string name = "mc-" + room.instance;
        const auto result = DNSServiceRegister(&m_impl->registration, 0, 0, name.c_str(), "_minecraftc._tcp", "local.", nullptr,
            htons(room.port), static_cast<uint16_t>(next.size()), next.data(), Impl::registrationReply, m_impl.get());
        if (result) { m_impl->failure(result); return false; }
    }
    m_impl->advertisedInstance = room.instance; m_impl->advertisedPort = room.port; m_impl->advertisedTxt = next; return true;
}
void LanDiscovery::stopAdvertising() { release(m_impl->registration); m_impl->advertisedInstance.clear(); m_impl->advertisedTxt.clear(); }
void LanDiscovery::poll(double) {
    m_impl->process(m_impl->browse); m_impl->process(m_impl->registration);
    for (auto& entry : m_impl->cache) { if (!entry.second->removed) { m_impl->process(entry.second->resolve); m_impl->process(entry.second->address); } }
    m_impl->rooms.clear();
    for (auto it = m_impl->cache.begin(); it != m_impl->cache.end();) {
        if (it->second->removed) it = m_impl->cache.erase(it);
        else { if (DiscoveryValidation::valid(it->second->room)) m_impl->rooms.push_back(it->second->room); ++it; }
    }
}
const std::vector<LanDiscoveredRoom>& LanDiscovery::rooms() const { return m_impl->rooms; }
const std::string& LanDiscovery::error() const { return m_impl->error; }
}
#endif
