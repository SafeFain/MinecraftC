#include "core/Input.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* ACTION_NAMES[] = {
    "Move Forward", "Move Backward", "Move Left", "Move Right",
    "Jump", "Sneak", "Sprint", "Inventory", "Command", "Attack", "Use",
    "Hotbar 1", "Hotbar 2", "Hotbar 3", "Hotbar 4", "Hotbar 5",
    "Hotbar 6", "Hotbar 7", "Hotbar 8", "Hotbar 9",
    "Previous Slot", "Next Slot", "Change Perspective", "Drop Item"
};
}

std::string gamepadBindingName(const GamepadBinding& binding) {
    if (binding.type == GamepadBindingType::None) return "Unbound";
    if (binding.type == GamepadBindingType::Button)
        return "Button " + std::to_string(binding.code);
    return "Axis " + std::to_string(binding.code) +
        (binding.type == GamepadBindingType::AxisPositive ? " +" : " -");
}

float normalizeGamepadAxis(float value, float deadzone) {
    deadzone = std::clamp(deadzone, 0.0f, 0.95f);
    const float magnitude = std::abs(value);
    if (magnitude <= deadzone) return 0.0f;
    return std::copysign(std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone)), value);
}

const char* inputActionName(InputAction action) {
    const size_t index = static_cast<size_t>(action);
    return index < INPUT_ACTION_COUNT ? ACTION_NAMES[index] : "Unknown";
}

std::string inputBindingName(const InputBinding& binding) {
    if (binding.device == InputDevice::None) return "Unbound";
    if (binding.device == InputDevice::Mouse)
        return "Mouse " + std::to_string(binding.code + 1);
    if (binding.device == InputDevice::Wheel)
        return binding.code > 0 ? "Wheel Up" : "Wheel Down";
    const char* name = physicalKeyName(binding.code);
    if (name) {
        std::string result(name);
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }
    switch (binding.code) {
        case Key::Space: return "Space";
        case Key::LeftShift: return "Left Shift";
        case Key::LeftControl: return "Left Ctrl";
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        default: return "Key " + std::to_string(binding.code);
    }
}

bool inputActionCanUnbind(InputAction action) {
    return action != InputAction::MoveForward && action != InputAction::MoveBackward &&
           action != InputAction::MoveLeft && action != InputAction::MoveRight &&
           action != InputAction::Jump;
}

void InputState::beginFrame() {
    m_pressed.fill(false);
    m_released.fill(false);
    m_wheelDirection = 0;
}

void InputState::keyEvent(int key, ButtonAction action) {
    if (key >= 0 && key < static_cast<int>(m_keys.size()))
        m_keys[static_cast<size_t>(key)] = action != ButtonAction::Release;
}

void InputState::mouseEvent(int button, ButtonAction action) {
    if (button >= 0 && button < static_cast<int>(m_mouse.size()))
        m_mouse[static_cast<size_t>(button)] = action != ButtonAction::Release;
}

void InputState::scrollEvent(double yOffset) {
    if (yOffset > 0.0) m_wheelDirection = 1;
    else if (yOffset < 0.0) m_wheelDirection = -1;
}

void InputState::clearPhysical() {
    m_keys.fill(false);
    m_mouse.fill(false);
    m_wheelDirection = 0;
}

void InputState::setVirtual(InputAction action, float strength) {
    m_virtual[static_cast<size_t>(action)] = std::clamp(strength, 0.0f, 1.0f);
}

void InputState::clearVirtual() { m_virtual.fill(0.0f); }

void InputState::update(const std::array<InputBinding, INPUT_ACTION_COUNT>& bindings) {
    for (size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        bool now = false;
        if (binding.device == InputDevice::Keyboard && binding.code >= 0 &&
            binding.code < static_cast<int>(m_keys.size()))
            now = m_keys[static_cast<size_t>(binding.code)];
        else if (binding.device == InputDevice::Mouse && binding.code >= 0 &&
                 binding.code < static_cast<int>(m_mouse.size()))
            now = m_mouse[static_cast<size_t>(binding.code)];
        else if (binding.device == InputDevice::Wheel)
            now = m_wheelDirection == binding.code;
        m_values[i] = std::max({now ? 1.0f : 0.0f, m_virtual[i], m_gamepad[i]});
        const bool combined = m_values[i] > 0.001f;
        m_pressed[i] = m_pressed[i] || (combined && !m_held[i]);
        m_released[i] = m_released[i] || (!combined && m_held[i]);
        m_held[i] = combined;
    }
}

void InputState::updateGamepad(
        const std::array<GamepadBinding, INPUT_ACTION_COUNT>& bindings,
        const std::array<bool, 32>& buttons, const std::array<float, 16>& axes,
        float deadzone) {
    for (size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        float value = 0.0f;
        if (binding.type == GamepadBindingType::Button && binding.code >= 0 &&
            binding.code < static_cast<int>(buttons.size())) {
            value = buttons[static_cast<size_t>(binding.code)] ? 1.0f : 0.0f;
        } else if ((binding.type == GamepadBindingType::AxisPositive ||
                    binding.type == GamepadBindingType::AxisNegative) &&
                   binding.code >= 0 && binding.code < static_cast<int>(axes.size())) {
            float axis = normalizeGamepadAxis(axes[static_cast<size_t>(binding.code)], deadzone);
            if (binding.type == GamepadBindingType::AxisNegative) axis = -axis;
            value = std::max(0.0f, axis);
        }
        m_gamepad[i] = value;
    }
}

bool InputState::held(InputAction action) const { return m_held[static_cast<size_t>(action)]; }
bool InputState::pressed(InputAction action) const { return m_pressed[static_cast<size_t>(action)]; }
bool InputState::released(InputAction action) const { return m_released[static_cast<size_t>(action)]; }
float InputState::value(InputAction action) const { return m_values[static_cast<size_t>(action)]; }
