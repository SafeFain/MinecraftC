#pragma once

#include "ui/Menu.h"
#include <functional>

class SettingsMenu : public Menu {
public:
    explicit SettingsMenu(std::function<void()> onBack);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key) override;
    void onMouseMove(double x, double y) override;
    void onMouseClick(int button) override;

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    std::function<void()> m_onBack;

    void cycleRenderDistance();
    void randomizeSeed();
    std::string labelForRenderDist() const;
    std::string labelForSeed() const;
    void refreshButtons();
};
