#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Platform {
struct LanAdvertisement {
    std::string instance, name, version;
    uint16_t port = 0, protocol = 0;
    uint32_t generation = 0;
    uint8_t players = 1, capacity = 8;
    bool pvp = false;
};
struct LanDiscoveredRoom {
    std::string instance, name, address, version;
    uint16_t port = 0, protocol = 0;
    uint32_t generation = 0;
    uint8_t players = 0, capacity = 0;
    bool pvp = false;
};
// Nonblocking DNS-SD _minecraftc._tcp. Discovery failure leaves direct TCP join
// usable. Results are bounded, expire, and never authorize a connection.
class LanDiscovery {
public:
    LanDiscovery();
    ~LanDiscovery();
    LanDiscovery(const LanDiscovery&) = delete;
    LanDiscovery& operator=(const LanDiscovery&) = delete;
    bool browse();
    void stopBrowsing();
    bool advertise(const LanAdvertisement& room);
    void stopAdvertising();
    void poll(double now);
    const std::vector<LanDiscoveredRoom>& rooms() const;
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
