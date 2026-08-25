#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "core/InputCodes.h"

enum class InputAction : uint8_t {
    MoveForward, MoveBackward, MoveLeft, MoveRight,
    Jump, Sneak, Sprint, Inventory, Command, Attack, Use,
    Hotbar1, Hotbar2, Hotbar3, Hotbar4, Hotbar5,
    Hotbar6, Hotbar7, Hotbar8, Hotbar9,
    PreviousSlot, NextSlot, Perspective,
    DropItem,
    DirectCommand, PickBlock, SwapOffhand, Fullscreen,
    Count
};

enum class InputDevice : uint8_t { None, Keyboard, Mouse, Wheel };

enum class GamepadBindingType : uint8_t { None, Button, AxisPositive, AxisNegative };

struct GamepadBinding {
    GamepadBindingType type = GamepadBindingType::None;
    int code = 0;

    bool operator==(const GamepadBinding& other) const {
        return type == other.type && code == other.code;
    }
};

struct InputBinding {
    InputDevice device = InputDevice::None;
    int code = 0;

    bool operator==(const InputBinding& other) const {
        return device == other.device && code == other.code;
    }
};

constexpr size_t INPUT_ACTION_COUNT = static_cast<size_t>(InputAction::Count);

const char* inputActionName(InputAction action);
std::string inputBindingName(const InputBinding& binding);
std::string gamepadBindingName(const GamepadBinding& binding);
bool inputActionCanUnbind(InputAction action);
float normalizeGamepadAxis(float value, float deadzone);

class InputState {
public:
    void beginFrame();
    void keyEvent(int key, ButtonAction action);
    void mouseEvent(int button, ButtonAction action);
    void scrollEvent(double yOffset);
    void clearPhysical();
    void setVirtual(InputAction action, float strength);
    void clearVirtual();
    void update(const std::array<InputBinding, INPUT_ACTION_COUNT>& bindings);
    void updateGamepad(const std::array<GamepadBinding, INPUT_ACTION_COUNT>& bindings,
                       const std::array<bool, 32>& buttons,
                       const std::array<float, 16>& axes, float deadzone);

    float value(InputAction action) const;
    bool held(InputAction action) const;
    bool pressed(InputAction action) const;
    bool released(InputAction action) const;

private:
    std::array<bool, Key::Count> m_keys{};
    std::array<bool, MouseButton::Count> m_mouse{};
    std::array<bool, INPUT_ACTION_COUNT> m_held{};
    std::array<bool, INPUT_ACTION_COUNT> m_pressed{};
    std::array<bool, INPUT_ACTION_COUNT> m_released{};
    std::array<float, INPUT_ACTION_COUNT> m_virtual{};
    std::array<float, INPUT_ACTION_COUNT> m_values{};
    std::array<float, INPUT_ACTION_COUNT> m_gamepad{};
    int m_wheelDirection = 0;
};
