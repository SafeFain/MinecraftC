#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include "Config.h"

class UIRenderer;

// ── Game state ────────────────────────────────────────────────────────────

enum class GameState {
    MainMenu,
    Playing,
    Paused
};

// ── Menu callbacks ────────────────────────────────────────────────────────

struct MenuCallbacks {
    std::function<void()> onStartGame;
    std::function<void()> onResume;
    std::function<void()> onBackToMenu;
    std::function<void()> onQuit;
    std::function<void()> onOpenSettings;
};

// ── Button ────────────────────────────────────────────────────────────────

struct ButtonColors {
    glm::vec4 normal{0.25f, 0.25f, 0.35f, 0.85f};
    glm::vec4 hover{0.35f, 0.35f, 0.50f, 0.90f};
    glm::vec4 selected{0.45f, 0.45f, 0.60f, 0.90f};
    glm::vec3 textNormal{1.0f, 1.0f, 1.0f};
    glm::vec3 textHover{1.0f, 1.0f, 0.6f};
};

class Button {
public:
    Button(const std::string& label, std::function<void()> onClick);

    void setPosition(float x, float y) { m_x = x; m_y = y; }
    void setSize(float w, float h) { m_w = w; m_h = h; }

    bool containsPoint(float px, float py) const;
    void setHovered(bool h) { m_hovered = h; }
    void setSelected(bool s) { m_selected = s; }
    bool isHovered() const { return m_hovered; }
    bool isSelected() const { return m_selected; }

    void render(UIRenderer& ui) const;
    void activate();

    float x() const { return m_x; }
    float y() const { return m_y; }
    float width() const { return m_w; }
    float height() const { return m_h; }
    const std::string& label() const { return m_label; }

private:
    std::string m_label;
    std::function<void()> m_onClick;
    float m_x = 0, m_y = 0, m_w = Config::UI_BUTTON_WIDTH, m_h = Config::UI_BUTTON_HEIGHT;
    ButtonColors m_colors;
    bool m_hovered = false;
    bool m_selected = false;
};

// ── Menu base class ───────────────────────────────────────────────────────

class Menu {
public:
    virtual ~Menu() = default;

    virtual void render(UIRenderer& ui, int screenWidth, int screenHeight) = 0;
    virtual void onKeyPress(int key) = 0;
    virtual void onMouseMove(double x, double y) = 0;
    virtual void onMouseClick(int button) = 0;

protected:
    void navigateUp(std::vector<Button>& buttons, int& selectedIdx);
    void navigateDown(std::vector<Button>& buttons, int& selectedIdx);
    void activateSelected(std::vector<Button>& buttons, int selectedIdx);
};

// ── Main Menu ─────────────────────────────────────────────────────────────

class MainMenu : public Menu {
public:
    explicit MainMenu(const MenuCallbacks& callbacks);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key) override;
    void onMouseMove(double x, double y) override;
    void onMouseClick(int button) override;

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
};

// ── Pause Menu ────────────────────────────────────────────────────────────

class PauseMenu : public Menu {
public:
    explicit PauseMenu(const MenuCallbacks& callbacks);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key) override;
    void onMouseMove(double x, double y) override;
    void onMouseClick(int button) override;

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
};
