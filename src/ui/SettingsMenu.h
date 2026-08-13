#pragma once

#include "ui/Menu.h"
#include <functional>

struct RendererBackendAvailability {
    bool openGL = true;
    bool vulkan = false;
};

inline bool rendererBackendSwitchable(RendererBackendAvailability available) {
    return available.openGL && available.vulkan;
}

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

class SettingsMenu : public Menu {
public:
    SettingsMenu(ClientSettings& settings, std::function<void()> onChanged,
                 std::function<void()> onBack, const Localization& localization,
                 RendererBackendAvailability renderers = {});

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onScroll(double yOffset) override;
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
    RendererBackendAvailability m_renderers;
    SettingsPage m_page = SettingsPage::General;
    int m_controlOffset = 0;
    int m_captureAction = -1;
    int m_pressedButton = -1;

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
};
