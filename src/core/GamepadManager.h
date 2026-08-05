#pragma once

#include <array>
#include <cstdint>
#include <memory>

class GamepadManager {
public:
    GamepadManager();
    ~GamepadManager();
    GamepadManager(const GamepadManager&) = delete;
    GamepadManager& operator=(const GamepadManager&) = delete;

    bool available() const;
    void deviceAdded(uint32_t id);
    void deviceRemoved(uint32_t id);
    void deviceActive(uint32_t id);
    void sample(std::array<bool, 32>& buttons, std::array<float, 16>& axes) const;
    void rumble(float strength, uint32_t durationMs, float configuredStrength = 1.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
