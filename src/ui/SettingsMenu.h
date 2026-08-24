#pragma once

#include "ui/Menu.h"
#include <algorithm>
#include <cmath>
#include <functional>

enum class SettingsPage {
    General,
    Video,
    KeyBindings,
    KeyboardMouse,
    Controller,
    Touch
};

inline SettingsPage settingsParentPage(SettingsPage page) {
    switch (page) {
        case SettingsPage::KeyboardMouse:
        case SettingsPage::Controller:
        case SettingsPage::Touch:
            return SettingsPage::KeyBindings;
        case SettingsPage::Video:
        case SettingsPage::KeyBindings:
        case SettingsPage::General:
            return SettingsPage::General;
    }
    return SettingsPage::General;
}

struct SettingsButtonLayout {
    float helpY = 0.0f;
    float firstButtonY = 0.0f;
    float buttonHeight = 0.0f;
};

inline SettingsButtonLayout settingsButtonLayout(
    float titleY, size_t buttonCount, bool hasHelp) {
    const float helpY = titleY - 34.0f;
    const float contentTop = hasHelp ? helpY - 10.0f : titleY - 18.0f;
    const float buttonHeight = std::clamp(
        (contentTop - 14.0f) / std::max<size_t>(1, buttonCount) - 5.0f,
        22.0f, Config::UI_BUTTON_HEIGHT);
    return {helpY, contentTop - buttonHeight, buttonHeight};
}

inline int frameRateFromSlider(float x, float left, float width) {
    if (width <= 0.0f) return ClientSettings::MIN_FRAME_RATE;
    const float position = std::clamp((x - left) / width, 0.0f, 1.0f);
    return ClientSettings::MIN_FRAME_RATE + static_cast<int>(std::lround(
        position * (ClientSettings::MAX_FRAME_RATE - ClientSettings::MIN_FRAME_RATE)));
}

inline float frameRateSliderFraction(int frameRate) {
    return static_cast<float>(std::clamp(
        frameRate, ClientSettings::MIN_FRAME_RATE, ClientSettings::MAX_FRAME_RATE) -
        ClientSettings::MIN_FRAME_RATE) /
        static_cast<float>(ClientSettings::MAX_FRAME_RATE - ClientSettings::MIN_FRAME_RATE);
}

class SettingsMenu : public Menu {
public:
    SettingsMenu(ClientSettings& settings, std::function<void()> onChanged,
                 std::function<void()> onBack, const Localization& localization);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onScroll(double yOffset) override;
    bool capturesPointerDrag(double x, double y) const override;
    bool capturingGamepad() const {
        return m_page == SettingsPage::Controller && m_captureAction >= 0;
    }
    void onGamepadBinding(GamepadBinding binding);

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    std::function<void()> m_onBack;
    std::function<void()> m_onChanged;
    ClientSettings& m_settings;
    const Localization& m_localization;
    SettingsPage m_page = SettingsPage::General;
    int m_controlOffset = 0;
    int m_captureAction = -1;
    int m_pressedButton = -1;
    int m_frameRateButton = -1;
    bool m_frameRateDragging = false;

    void cycleRenderDistance();
    void toggleCloudRendering();
    void cycleCloudRenderDistance();
    void cycleDayCycle();
    void toggleAutoJump();
    std::string labelForRenderDist() const;
    std::string labelForCloudRenderDist() const;
    std::string labelForDayCycle() const;
    std::string labelForAutoJump() const;
    void showPage(SettingsPage page);
    void refreshButtons();
    void assignBinding(InputBinding binding);
    void assignGamepadBinding(GamepadBinding binding);
    std::string frameRateLabel() const;
    void setFrameRateFromPointer(double x);
};
