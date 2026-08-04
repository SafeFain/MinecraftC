#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

union SDL_Event;
struct SDL_Gamepad;

class GamepadManager {
public:
    GamepadManager();
    ~GamepadManager();
    GamepadManager(const GamepadManager&) = delete;
    GamepadManager& operator=(const GamepadManager&) = delete;

    bool available() const { return m_active != 0; }
    void processEvent(const SDL_Event& event);
    void sample(std::array<bool, 32>& buttons, std::array<float, 16>& axes) const;
    void rumble(float strength, uint32_t durationMs, float configuredStrength = 1.0f);

private:
    std::unordered_map<uint32_t, SDL_Gamepad*> m_gamepads;
    uint32_t m_active = 0;
    float m_rumbleStrength = 0.0f;
    uint64_t m_rumbleUntil = 0;
    bool m_ownsSubsystem=false;
    void add(uint32_t id);
    void remove(uint32_t id);
};
