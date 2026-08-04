#pragma once

#include "ui/Menu.h"
#include <functional>

class SettingsMenu : public Menu {
public:
    SettingsMenu(ClientSettings& settings, std::function<void()> onChanged,
                 std::function<void()> onBack, const Localization& localization);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onScroll(double yOffset) override;
    bool capturingGamepad() const { return m_page == Page::Gamepad && m_captureAction >= 0; }
    void onGamepadBinding(GamepadBinding binding);

private:
    enum class Page { General, Video, Controls, Gamepad, Touch };

    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    std::function<void()> m_onBack;
    std::function<void()> m_onChanged;
    ClientSettings& m_settings;
    const Localization& m_localization;
    Page m_page = Page::General;
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
    void refreshButtons();
    void assignBinding(InputBinding binding);
    void assignGamepadBinding(GamepadBinding binding);
};
