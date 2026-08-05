#include "core/GamepadManager.h"

#include "debug/Log.h"
#include "core/RuntimeClock.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

struct GamepadManager::Impl {
    std::unordered_map<uint32_t, SDL_Gamepad*> gamepads;
    uint32_t active = 0;
    float rumbleStrength = 0.0f;
    uint64_t rumbleUntil = 0;
    bool ownsSubsystem = false;
};

GamepadManager::GamepadManager() : m_impl(std::make_unique<Impl>()) {
    m_impl->ownsSubsystem=(SDL_WasInit(SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK)==0);
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        LOG_WARN("Gamepad support unavailable: " << SDL_GetError());
        return;
    }
    if(!SDL_InitSubSystem(SDL_INIT_HAPTIC))LOG_WARN("Haptic support unavailable: "<<SDL_GetError());
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) deviceAdded(ids[i]);
    SDL_free(ids);
}

GamepadManager::~GamepadManager() {
    for (const auto& entry : m_impl->gamepads) SDL_CloseGamepad(entry.second);
    m_impl->gamepads.clear();
    if (m_impl->ownsSubsystem&&SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC))
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
}

bool GamepadManager::available() const { return m_impl->active != 0; }

void GamepadManager::deviceAdded(uint32_t id) {
    if (m_impl->gamepads.count(id)) return;
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (!gamepad) { LOG_WARN("Could not open gamepad: " << SDL_GetError()); return; }
    m_impl->gamepads.emplace(id, gamepad);
    if (!m_impl->active) m_impl->active = id;
    LOG_INFO("Opened gamepad: " << SDL_GetGamepadName(gamepad));
}

void GamepadManager::deviceRemoved(uint32_t id) {
    const auto found = m_impl->gamepads.find(id);
    if (found == m_impl->gamepads.end()) return;
    SDL_CloseGamepad(found->second);
    m_impl->gamepads.erase(found);
    if (m_impl->active == id)
        m_impl->active = m_impl->gamepads.empty() ? 0 : m_impl->gamepads.begin()->first;
}

void GamepadManager::deviceActive(uint32_t id) {
    if (m_impl->gamepads.count(id)) m_impl->active = id;
}

void GamepadManager::sample(std::array<bool, 32>& buttons,
                            std::array<float, 16>& axes) const {
    buttons.fill(false); axes.fill(0.0f);
    const auto found = m_impl->gamepads.find(m_impl->active);
    if (found == m_impl->gamepads.end()) return;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT && i < static_cast<int>(buttons.size()); ++i)
        buttons[static_cast<size_t>(i)] = SDL_GetGamepadButton(
            found->second, static_cast<SDL_GamepadButton>(i));
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT && i < static_cast<int>(axes.size()); ++i) {
        const Sint16 value = SDL_GetGamepadAxis(found->second, static_cast<SDL_GamepadAxis>(i));
        axes[static_cast<size_t>(i)] = value < 0 ? static_cast<float>(value) / 32768.0f
                                                : static_cast<float>(value) / 32767.0f;
    }
}

void GamepadManager::rumble(float strength, uint32_t durationMs, float configuredStrength) {
    const auto found = m_impl->gamepads.find(m_impl->active);
    if (found == m_impl->gamepads.end()) return;
    const float scaled = std::clamp(strength * configuredStrength, 0.0f, 1.0f);
    const uint64_t now = RuntimeClock::milliseconds(RuntimeClock{}.now());
    if (now < m_impl->rumbleUntil && scaled < m_impl->rumbleStrength) return;
    m_impl->rumbleStrength = scaled;
    m_impl->rumbleUntil = now + durationMs;
    const Uint16 amplitude = static_cast<Uint16>(std::lround(scaled * 65535.0f));
    if (!SDL_RumbleGamepad(found->second, amplitude, amplitude, durationMs))
        LOG_DEBUG("Gamepad rumble unavailable: " << SDL_GetError());
}
