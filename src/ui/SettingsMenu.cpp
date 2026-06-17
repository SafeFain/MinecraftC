#include "ui/SettingsMenu.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <GLFW/glfw3.h>
#include <random>
#include <sstream>

SettingsMenu::SettingsMenu(std::function<void()> onBack)
    : m_onBack(std::move(onBack))
{
    m_buttons.emplace_back(labelForRenderDist(), [this]() { cycleRenderDistance(); });
    m_buttons.emplace_back(labelForSeed(),       [this]() { randomizeSeed(); });
    m_buttons.emplace_back("Back", m_onBack);

    if (!m_buttons.empty()) {
        m_buttons[0].setSelected(true);
    }
}

std::string SettingsMenu::labelForRenderDist() const {
    return "Render Distance: " + std::to_string(Config::RENDER_DISTANCE);
}

std::string SettingsMenu::labelForSeed() const {
    return "World Seed: " + std::to_string(Config::WORLD_SEED);
}

void SettingsMenu::cycleRenderDistance() {
    int idx = 0;
    for (int i = 0; i < Config::RENDER_DISTANCE_OPTION_COUNT; ++i) {
        if (Config::RENDER_DISTANCE_OPTIONS[i] == Config::RENDER_DISTANCE) {
            idx = (i + 1) % Config::RENDER_DISTANCE_OPTION_COUNT;
            break;
        }
    }
    Config::RENDER_DISTANCE = Config::RENDER_DISTANCE_OPTIONS[idx];
    refreshButtons();
}

void SettingsMenu::randomizeSeed() {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    Config::WORLD_SEED = rng();
    refreshButtons();
}

void SettingsMenu::refreshButtons() {
    // Recreate buttons with updated labels, preserving selection
    int oldIdx = m_selectedIdx;
    m_buttons.clear();
    m_buttons.emplace_back(labelForRenderDist(), [this]() { cycleRenderDistance(); });
    m_buttons.emplace_back(labelForSeed(),       [this]() { randomizeSeed(); });
    m_buttons.emplace_back("Back", m_onBack);

    if (oldIdx >= 0 && oldIdx < static_cast<int>(m_buttons.size())) {
        m_selectedIdx = oldIdx;
        m_buttons[m_selectedIdx].setSelected(true);
    }
}

void SettingsMenu::render(UIRenderer& ui, int screenWidth, int screenHeight) {
    // Full-screen dark background
    ui.drawRect(0.0f, 0.0f, static_cast<float>(screenWidth),
                static_cast<float>(screenHeight),
                glm::vec4(0.08f, 0.08f, 0.12f, 1.0f));

    // Title: "SETTINGS"
    const char* title = "SETTINGS";
    float titleScale = 3.0f;
    auto titleSize = ui.measureText(title, titleScale);
    float titleX = (screenWidth - titleSize.x) * 0.5f;
    float titleY = screenHeight * 0.62f;
    ui.renderText(title, titleX, titleY, titleScale,
                  glm::vec3(1.0f, 0.85f, 0.3f));

    // Buttons
    float buttonStartY = titleY - titleSize.y - 40.0f;
    float buttonX = (screenWidth - Config::UI_BUTTON_WIDTH) * 0.5f;

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        float by = buttonStartY - static_cast<float>(i) * (Config::UI_BUTTON_HEIGHT + Config::UI_BUTTON_SPACING);
        m_buttons[i].setPosition(buttonX, by);
        m_buttons[i].setSize(Config::UI_BUTTON_WIDTH, Config::UI_BUTTON_HEIGHT);
        m_buttons[i].render(ui);
    }
}

void SettingsMenu::onKeyPress(int key) {
    switch (key) {
        case GLFW_KEY_UP:
        case GLFW_KEY_W:
            navigateUp(m_buttons, m_selectedIdx);
            break;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_S:
            navigateDown(m_buttons, m_selectedIdx);
            break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_SPACE:
            activateSelected(m_buttons, m_selectedIdx);
            break;
        default:
            break;
    }
}

void SettingsMenu::onMouseMove(double x, double y) {
    for (auto& btn : m_buttons) {
        btn.setHovered(btn.containsPoint(static_cast<float>(x),
                                          static_cast<float>(y)));
    }
}

void SettingsMenu::onMouseClick(int button) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    for (auto& btn : m_buttons) {
        if (btn.isHovered()) {
            btn.activate();
            return;
        }
    }
}
