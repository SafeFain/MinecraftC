#pragma once

#include <array>
#include <cstdint>
#include <string>

enum class InputAction : uint8_t {
    MoveForward, MoveBackward, MoveLeft, MoveRight,
    Jump, Sneak, Sprint, Inventory, Command, Attack, Use,
    Hotbar1, Hotbar2, Hotbar3, Hotbar4, Hotbar5,
    Hotbar6, Hotbar7, Hotbar8, Hotbar9,
    PreviousSlot, NextSlot,
    Count
};

enum class InputDevice : uint8_t { None, Keyboard, Mouse, Wheel };

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
bool inputActionCanUnbind(InputAction action);

class InputState {
public:
    void beginFrame();
    void keyEvent(int key, int action);
    void mouseEvent(int button, int action);
    void scrollEvent(double yOffset);
    void setVirtual(InputAction action, float strength);
    void clearVirtual();
    void update(const std::array<InputBinding, INPUT_ACTION_COUNT>& bindings);

    float value(InputAction action) const;
    bool held(InputAction action) const;
    bool pressed(InputAction action) const;
    bool released(InputAction action) const;

private:
    std::array<bool, 512> m_keys{};
    std::array<bool, 16> m_mouse{};
    std::array<bool, INPUT_ACTION_COUNT> m_held{};
    std::array<bool, INPUT_ACTION_COUNT> m_pressed{};
    std::array<bool, INPUT_ACTION_COUNT> m_released{};
    std::array<float, INPUT_ACTION_COUNT> m_virtual{};
    std::array<float, INPUT_ACTION_COUNT> m_values{};
    int m_wheelDirection = 0;
};
