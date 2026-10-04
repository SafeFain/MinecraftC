#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include "Config.h"
#include "ui/UILayout.h"
#include "core/TextEditBuffer.h"
#include "game/SaveStore.h"
#include "game/WorldCatalog.h"
#include "game/ClientSettings.h"
#include "game/Localization.h"

class UIRenderer;
namespace platform { class Clipboard; }

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
    std::function<void(const std::string&, const std::string&, GameMode, WorldType, bool)> onCreateWorld;
    std::function<std::vector<WorldSummary>()> onRefreshWorlds;
    std::function<bool(const std::string&)> onDeleteWorld;
    std::function<void()> onResume;
    std::function<void()> onBackToMenu;
    std::function<void()> onQuit;
    std::function<void()> onOpenSettings;
    std::function<void()> onOpenPlugins;
    std::function<void()> onSettingsChanged;
    std::function<void(const std::string&)> onOpenUrl;
    // Sleep actions use stable integer values so the UI layer remains
    // independent of GameSession's gameplay headers.
    std::function<void(int)> onSleepAction;
};

// ── Button ────────────────────────────────────────────────────────────────

class Button {
public:
    Button(const std::string& label, std::function<void()> onClick);

    void setPosition(float x, float y) { m_x = x; m_y = y; }
    void setSize(float w, float h) { m_w = w; m_h = h; }

    bool containsPoint(float px, float py) const;
    void setHovered(bool h) { m_hovered = h; }
    void setSelected(bool s) { m_selected = s; }
    void setFocusVisible(bool visible) { m_focusVisible = visible; }
    void setPressed(bool p) { m_pressed = p; }
    void setLabel(std::string label) { m_label = std::move(label); }
    void setDetail(std::string detail) { m_detail = std::move(detail); }
    void setDanger(bool danger) { m_danger = danger; }
    void setPrimary(bool primary) { m_primary = primary; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setBottomInset(float inset) { m_bottomInset = inset; }
    bool isHovered() const { return m_hovered; }
    bool isSelected() const { return m_selected; }
    bool isEnabled() const { return m_enabled; }
    void inheritFeedback(const Button& previous) { m_feedback = previous.m_feedback; }

    void render(UIRenderer& ui) const;
    void activate();

    float x() const { return m_x; }
    float y() const { return m_y; }
    float width() const { return m_w; }
    float height() const { return m_h; }
    const std::string& label() const { return m_label; }

private:
    std::string m_label;
    std::string m_detail;
    std::function<void()> m_onClick;
    float m_x = 0, m_y = 0, m_w = Config::UI_BUTTON_WIDTH, m_h = Config::UI_BUTTON_HEIGHT;
    bool m_hovered = false;
    bool m_selected = false;
    bool m_pressed = false;
    bool m_danger = false;
    bool m_primary = false;
    bool m_enabled = true;
    bool m_focusVisible = true;
    float m_bottomInset = 0;
    mutable UiFeedback m_feedback;
};

// ── Menu base class ───────────────────────────────────────────────────────

class Menu {
public:
    virtual ~Menu() = default;
    void tick(float dt) { m_fade.tick(dt,true,0.16f); }
    float opacity() const { return m_fade.value; }
    void resetTransition() { m_fade.value=0; }

    virtual void render(UIRenderer& ui, int screenWidth, int screenHeight) = 0;
    virtual void onKeyPress(int key, int mods = 0) = 0;
    virtual void onMouseMove(double x, double y) = 0;
    virtual void onMouseButton(int button, ButtonAction action, double x, double y) = 0;
    virtual void onScroll(double) {}
    virtual void onChar(unsigned int) {}
    virtual bool wantsTextInput() const { return false; }
    virtual bool capturesPointerDrag(double, double) const { return false; }
    virtual void onPointerCancel() {}

private:
    UiTransition m_fade;

protected:
    void navigationFocus() { m_pointerFocus = false; }
    void pointerFocus(double x, double y) { m_pointerFocus = true; m_pointer = {x,y}; }
    void prepareButton(Button& button);
    void cancelButtons(std::vector<Button>& buttons);
    void navigateUp(std::vector<Button>& buttons, int& selectedIdx);
    void navigateDown(std::vector<Button>& buttons, int& selectedIdx);
    void activateSelected(std::vector<Button>& buttons, int selectedIdx);
    bool m_pointerFocus = false;
    glm::dvec2 m_pointer{0};
};

// ── Main Menu ─────────────────────────────────────────────────────────────

class MainMenu : public Menu {
public:
    MainMenu(const MenuCallbacks& callbacks, std::vector<WorldSummary> worlds,
             ClientSettings& settings, Localization& localization,
             platform::Clipboard* clipboard);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onScroll(double yOffset) override;
    void onChar(unsigned int codepoint) override;
    void onPointerCancel() override;
    bool wantsTextInput() const override { return m_page == Page::Create; }

private:
    enum class Page { Home, Worlds, Create, Language, About };
    enum class Field { Name, Seed };

    MenuCallbacks m_callbacks;
    ClientSettings& m_settings;
    Localization& m_localization;
    std::vector<WorldSummary> m_worlds;
    std::vector<Button> m_buttons;
    std::vector<Button> m_deleteButtons;
    int m_selectedIdx = 0;
    Page m_page = Page::Home;
    Page m_buttonPage = Page::Home;
    Field m_field = Field::Name;
    TextEditBuffer m_worldName{{}, 32};
    TextEditBuffer m_seedText{{}, 20};
    GameMode m_createMode = GameMode::Survival;
    WorldType m_createWorldType = WorldType::Normal;
    bool m_createCheats = false;
    int m_worldOffset = 0;
    int m_visibleWorlds = 6;
    int m_formOffset = 0;
    int m_formVisibleRows = 7;
    int m_formRows = 7;
    int m_formSelection = -1;
    int m_aboutPage = 0;
    int m_selectedWorld = -1;
    int m_pressedButton = -1;
    int m_pressedDeleteButton = -1;
    double m_lastWorldClick = -1.0;
    int m_lastWorldIndex = -1;
    std::string m_pendingDeleteWorldId;

    void showHome();
    void showWorlds();
    void showCreate();
    void showLanguage();
    void showAbout();
    void changeAboutPage(int delta);
    void renderAbout(UIRenderer& ui, int screenWidth, int screenHeight);
    void refreshWorlds();
    void rebuildButtons();
    void selectField(Field field);
};

// ── Pause Menu ────────────────────────────────────────────────────────────

class PauseMenu : public Menu {
public:
    PauseMenu(const MenuCallbacks& callbacks, const Localization& localization);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onPointerCancel() override;

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    int m_pressedButton = -1;
};

// Bed interaction overlay.  The world keeps simulating behind this menu;
// callers decide whether the action changes time or starts a dimension load.
class SleepMenu : public Menu {
public:
    SleepMenu(const MenuCallbacks& callbacks, const Localization& localization,
              bool heaven);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onPointerCancel() override;

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    int m_pressedButton = -1;
};
