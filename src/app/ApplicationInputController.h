#pragma once

#include "core/Input.h"
#include "core/RuntimeClock.h"
#include "core/Touch.h"
#include "ui/TouchControls.h"

#include <array>
#include <functional>
#include <string_view>
#include <unordered_map>

struct ClientSettings;
class Window;

class ApplicationInputController {
public:
    struct Callbacks {
        std::function<void(int, int, ButtonAction, int)> key;
        std::function<void(std::string_view)> text;
        std::function<void(int, ButtonAction, int)> mouseButton;
        std::function<void(double, double)> scroll;
        std::function<void(const TouchEvent&)> touch;
        std::function<void(bool)> screenKeyboard;
    };
    struct UiTouchState {
        TouchContactId id;
        glm::vec2 position{0.0f};
        glm::vec2 origin{0.0f};
        RuntimeClock::Tick started = 0;
        bool active = false;
        bool buttonDown = false;
        bool rightButton = false;
        bool scrolling = false;
    };

    void beginFrame(Window& window, const ClientSettings& settings,
                    const TouchControlConfig& touchConfig,
                    int guiScale, bool textInput);
    void clearPhysical(const ClientSettings& settings);
    bool altPressed() const;
    void bind(Window& window, const ClientSettings& settings,
              Callbacks callbacks);

    InputState state;
    std::array<bool, 32> gamepadButtons{};
    std::array<float, 16> gamepadAxes{};
    std::array<bool, 32> previousGamepadButtons{};
    int gamepadNavX = 0;
    int gamepadNavY = 0;
    RuntimeClock::Tick gamepadRepeatTick = 0;
    bool gamepadCaptureArmed = false;
    TouchControls touchControls;
    bool touchHudVisible = false;
    std::unordered_map<TouchContactId, bool, TouchContactHash> touchGameplay;
    UiTouchState uiTouch;
    std::array<bool, Key::Count> keys{};

private:
    const ClientSettings* m_settings = nullptr;
    Callbacks m_callbacks;
};
