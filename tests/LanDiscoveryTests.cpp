#include "core/LanDiscovery.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        Platform::LanDiscovery host, browser;
        Platform::LanAdvertisement room{"123456789012345678901234567890ab", "Discovery regression", "test-version", 25565, 1, 20, 1, 8, false};
        if (!host.advertise(room) || !browser.browse()) {
            std::cerr << "SKIPPED: no usable multicast LAN interface: " << host.error() << browser.error() << '\n';
            return 77;
        }
        double now = 0;
        const auto pump = [&] { now += .01; host.poll(now); browser.poll(now); std::this_thread::sleep_for(std::chrono::milliseconds(2)); };
        const auto find = [&]() -> const Platform::LanDiscoveredRoom* {
            for (const auto& candidate : browser.rooms()) if (candidate.name == room.name) return &candidate;
            return nullptr;
        };
        for (int i = 0; i < 200 && !find(); ++i) pump();
        require(find() && find()->port == 25565 && find()->capacity == 8 && find()->players == 1 && !find()->address.empty(),
                "DNS-SD discovers complete numeric endpoint and capacity");
        require(find()->version == room.version && find()->protocol == 1 && find()->generation == 20,
                "DNS-SD carries compatibility metadata");
        room.players = 3; room.pvp = true;
        require(host.advertise(room), "room metadata refresh accepted");
        for (int i = 0; i < 200 && (!find() || find()->players != 3); ++i) pump();
        require(find() && find()->players == 3 && find()->pvp, "updated occupancy and PvP replace discovered metadata");
        host.stopAdvertising();
        for (int i = 0; i < 200 && find(); ++i) pump();
        require(!find(), "goodbye removes a closed LAN room");
        require(host.advertise(room), "room can advertise after closing");
        for (int i = 0; i < 200 && !find(); ++i) pump();
        require(find(), "reopened room becomes discoverable");
        for (int i = 0; i < 20; ++i) pump();
        browser.poll(now + 20);
        require(!find(), "unrefreshed LAN rooms expire without connection attempts");
        auto invalid = room; invalid.capacity = 9;
        require(!host.advertise(invalid), "invalid metadata never reaches multicast");
        browser.stopBrowsing(); require(browser.rooms().empty(), "closing browser releases cached room list");
        std::cout << "LAN DNS-SD discovery tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
