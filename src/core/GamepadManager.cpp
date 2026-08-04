#include "core/GamepadManager.h"

#include "debug/Log.h"
#include "core/RuntimeClock.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

GamepadManager::GamepadManager() {
    m_ownsSubsystem=(SDL_WasInit(SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK)==0);
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        LOG_WARN("Gamepad support unavailable: " << SDL_GetError());
        return;
    }
    if(!SDL_InitSubSystem(SDL_INIT_HAPTIC))LOG_WARN("Haptic support unavailable: "<<SDL_GetError());
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) add(ids[i]);
    SDL_free(ids);
}

GamepadManager::~GamepadManager() {
    for (const auto& entry : m_gamepads) SDL_CloseGamepad(entry.second);
    m_gamepads.clear();
    if (m_ownsSubsystem&&SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC))
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC);
}

void GamepadManager::add(uint32_t id) {
    if (m_gamepads.count(id)) return;
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (!gamepad) { LOG_WARN("Could not open gamepad: " << SDL_GetError()); return; }
    m_gamepads.emplace(id, gamepad);
    if (!m_active) m_active = id;
    LOG_INFO("Opened gamepad: " << SDL_GetGamepadName(gamepad));
}

void GamepadManager::remove(uint32_t id) {
    const auto found = m_gamepads.find(id);
    if (found == m_gamepads.end()) return;
    SDL_CloseGamepad(found->second);
    m_gamepads.erase(found);
    if (m_active == id) m_active = m_gamepads.empty() ? 0 : m_gamepads.begin()->first;
}

void GamepadManager::processEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) add(event.gdevice.which);
    else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) remove(event.gdevice.which);
    else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (m_gamepads.count(event.gbutton.which)) m_active = event.gbutton.which;
    } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               std::abs(event.gaxis.value) > 8000) {
        if (m_gamepads.count(event.gaxis.which)) m_active = event.gaxis.which;
    }
}

void GamepadManager::sample(std::array<bool, 32>& buttons,
                            std::array<float, 16>& axes) const {
    buttons.fill(false); axes.fill(0.0f);
    const auto found = m_gamepads.find(m_active);
    if (found == m_gamepads.end()) return;
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
    const auto found = m_gamepads.find(m_active);
    if (found == m_gamepads.end()) return;
    const float scaled = std::clamp(strength * configuredStrength, 0.0f, 1.0f);
    const uint64_t now = RuntimeClock::milliseconds(RuntimeClock{}.now());
    if (now < m_rumbleUntil && scaled < m_rumbleStrength) return;
    m_rumbleStrength = scaled;
    m_rumbleUntil = now + durationMs;
    const Uint16 amplitude = static_cast<Uint16>(std::lround(scaled * 65535.0f));
    if (!SDL_RumbleGamepad(found->second, amplitude, amplitude, durationMs))
        LOG_DEBUG("Gamepad rumble unavailable: " << SDL_GetError());
}
