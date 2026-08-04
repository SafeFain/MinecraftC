#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include "Config.h"
#include "core/TextEditBuffer.h"
#include "game/SaveStore.h"
#include "game/WorldCatalog.h"
#include "game/ClientSettings.h"
#include "game/Localization.h"

class UIRenderer;

// ── Game state ────────────────────────────────────────────────────────────

enum class GameState {
    MainMenu,
    LoadingWorld,
    Playing,
    Paused
};

// ── Menu callbacks ────────────────────────────────────────────────────────

struct MenuCallbacks {
    std::function<void(const std::string&)> onOpenWorld;
    std::function<void(const std::string&, const std::string&, GameMode, bool)> onCreateWorld;
    std::function<std::vector<WorldSummary>()> onRefreshWorlds;
    std::function<bool(const std::string&)> onDeleteWorld;
    std::function<void()> onResume;
    std::function<void()> onBackToMenu;
    std::function<void()> onQuit;
    std::function<void()> onOpenSettings;
    std::function<void()> onSettingsChanged;
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
    void setPressed(bool p) { m_pressed = p; }
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
    bool m_pressed = false;
};

// ── Menu base class ───────────────────────────────────────────────────────

class Menu {
public:
    virtual ~Menu() = default;

    virtual void render(UIRenderer& ui, int screenWidth, int screenHeight) = 0;
    virtual void onKeyPress(int key, int mods = 0) = 0;
    virtual void onMouseMove(double x, double y) = 0;
    virtual void onMouseButton(int button, ButtonAction action, double x, double y) = 0;
    virtual void onScroll(double) {}
    virtual void onChar(unsigned int) {}
    virtual bool wantsTextInput() const { return false; }

protected:
    void navigateUp(std::vector<Button>& buttons, int& selectedIdx);
    void navigateDown(std::vector<Button>& buttons, int& selectedIdx);
    void activateSelected(std::vector<Button>& buttons, int selectedIdx);
};

// ── Main Menu ─────────────────────────────────────────────────────────────

class MainMenu : public Menu {
public:
    MainMenu(const MenuCallbacks& callbacks, std::vector<WorldSummary> worlds,
             ClientSettings& settings, Localization& localization);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onScroll(double yOffset) override;
    void onChar(unsigned int codepoint) override;
    bool wantsTextInput() const override { return m_page == Page::Create; }

private:
    enum class Page { Home, Worlds, Create };
    enum class Field { Name, Seed };

    MenuCallbacks m_callbacks;
    ClientSettings& m_settings;
    Localization& m_localization;
    std::vector<WorldSummary> m_worlds;
    std::vector<Button> m_buttons;
    std::vector<Button> m_deleteButtons;
    int m_selectedIdx = 0;
    Page m_page = Page::Home;
    Field m_field = Field::Name;
    TextEditBuffer m_worldName{{}, 32};
    TextEditBuffer m_seedText{{}, 20};
    GameMode m_createMode = GameMode::Survival;
    bool m_createCheats = false;
    int m_worldOffset = 0;
    int m_selectedWorld = -1;
    int m_pressedButton = -1;
    int m_pressedDeleteButton = -1;
    double m_lastWorldClick = -1.0;
    int m_lastWorldIndex = -1;
    std::string m_pendingDeleteWorldId;

    void showHome();
    void showWorlds();
    void showCreate();
    void refreshWorlds();
    void rebuildButtons();
    void selectField(Field field);
    std::string fieldLabel(Field field, const std::string& value) const;
};

// ── Pause Menu ────────────────────────────────────────────────────────────

class PauseMenu : public Menu {
public:
    PauseMenu(const MenuCallbacks& callbacks, const Localization& localization);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    int m_pressedButton = -1;
};
