#include "core/Input.h"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace {
constexpr const char* ACTION_NAMES[] = {
    "Move Forward", "Move Backward", "Move Left", "Move Right",
    "Jump", "Sneak", "Sprint", "Inventory", "Command", "Attack", "Use",
    "Hotbar 1", "Hotbar 2", "Hotbar 3", "Hotbar 4", "Hotbar 5",
    "Hotbar 6", "Hotbar 7", "Hotbar 8", "Hotbar 9",
    "Previous Slot", "Next Slot"
};
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
    const char* name = glfwGetKeyName(binding.code, 0);
    if (name) {
        std::string result(name);
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }
    switch (binding.code) {
        case GLFW_KEY_SPACE: return "Space";
        case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
        case GLFW_KEY_LEFT_CONTROL: return "Left Ctrl";
        case GLFW_KEY_UP: return "Up";
        case GLFW_KEY_DOWN: return "Down";
        case GLFW_KEY_LEFT: return "Left";
        case GLFW_KEY_RIGHT: return "Right";
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

void InputState::keyEvent(int key, int action) {
    if (key >= 0 && key < static_cast<int>(m_keys.size()))
        m_keys[static_cast<size_t>(key)] = action != GLFW_RELEASE;
}

void InputState::mouseEvent(int button, int action) {
    if (button >= 0 && button < static_cast<int>(m_mouse.size()))
        m_mouse[static_cast<size_t>(button)] = action != GLFW_RELEASE;
}

void InputState::scrollEvent(double yOffset) {
    if (yOffset > 0.0) m_wheelDirection = 1;
    else if (yOffset < 0.0) m_wheelDirection = -1;
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
        m_values[i] = std::max(now ? 1.0f : 0.0f, m_virtual[i]);
        const bool combined = m_values[i] > 0.001f;
        m_pressed[i] = m_pressed[i] || (combined && !m_held[i]);
        m_released[i] = m_released[i] || (!combined && m_held[i]);
        m_held[i] = combined;
    }
}

bool InputState::held(InputAction action) const { return m_held[static_cast<size_t>(action)]; }
bool InputState::pressed(InputAction action) const { return m_pressed[static_cast<size_t>(action)]; }
bool InputState::released(InputAction action) const { return m_released[static_cast<size_t>(action)]; }
float InputState::value(InputAction action) const { return m_values[static_cast<size_t>(action)]; }
