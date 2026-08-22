#include "ui/Menu.h"
#include "ui/UIRenderer.h"

#include "core/Window.h"
#include "core/RuntimeClock.h"
#include <algorithm>
#include "game/Utf8.h"

// ── Button ────────────────────────────────────────────────────────────────

Button::Button(const std::string& label, std::function<void()> onClick)
    : m_label(label), m_onClick(std::move(onClick)) {}

bool Button::containsPoint(float px, float py) const {
    return px >= m_x && px <= m_x + m_w &&
           py >= m_y && py <= m_y + m_h;
}

void Button::render(UIRenderer& ui) const {
    // Choose color based on state
    glm::vec4 bgColor = m_pressed ? glm::vec4(0.16f, 0.16f, 0.20f, 0.98f)
                      : m_selected ? m_colors.selected
                      : m_hovered  ? m_colors.hover
                      :              m_colors.normal;

    glm::vec3 textColor = (m_hovered || m_selected)
                          ? m_colors.textHover
                          : m_colors.textNormal;

    // Draw button background
    ui.drawRect(m_x, m_y, m_w, m_h, bgColor);

    // Draw border when selected
    if (m_selected) {
        glm::vec4 borderCol(1.0f, 1.0f, 1.0f, 0.8f);
        float bw = 2.0f;
        ui.drawRect(m_x, m_y, m_w, bw, borderCol);             // bottom
        ui.drawRect(m_x, m_y + m_h - bw, m_w, bw, borderCol);  // top
        ui.drawRect(m_x, m_y, bw, m_h, borderCol);             // left
        ui.drawRect(m_x + m_w - bw, m_y, bw, m_h, borderCol);  // right
    }

    // Draw centered label text
    float fontSize = 1.8f;
    auto textSize = ui.measureText(m_label, fontSize);
    float textX = m_x + (m_w - textSize.x) * 0.5f;
    float textY = m_y + (m_h - textSize.y) * 0.5f;
    ui.renderText(m_label, textX, textY, fontSize, textColor);
}

void Button::activate() {
    if (m_onClick) m_onClick();
}

// ── Menu base ─────────────────────────────────────────────────────────────

void Menu::navigateUp(std::vector<Button>& buttons, int& selectedIdx) {
    if (buttons.empty()) return;
    buttons[selectedIdx].setSelected(false);
    selectedIdx = (selectedIdx - 1 + static_cast<int>(buttons.size())) % static_cast<int>(buttons.size());
    buttons[selectedIdx].setSelected(true);
}

void Menu::navigateDown(std::vector<Button>& buttons, int& selectedIdx) {
    if (buttons.empty()) return;
    buttons[selectedIdx].setSelected(false);
    selectedIdx = (selectedIdx + 1) % static_cast<int>(buttons.size());
    buttons[selectedIdx].setSelected(true);
}

void Menu::activateSelected(std::vector<Button>& buttons, int selectedIdx) {
    if (selectedIdx >= 0 && selectedIdx < static_cast<int>(buttons.size())) {
        buttons[selectedIdx].activate();
    }
}

// ── Main Menu ─────────────────────────────────────────────────────────────

MainMenu::MainMenu(const MenuCallbacks& callbacks, std::vector<WorldSummary> worlds,
                   ClientSettings& settings, Localization& localization,
                   platform::Clipboard* clipboard)
    : m_callbacks(callbacks), m_settings(settings), m_localization(localization),
      m_worlds(std::move(worlds)) {
    m_worldName.setClipboard(clipboard);
    m_seedText.setClipboard(clipboard);
    m_seedText.setFilter([](uint32_t codepoint,const std::string& current){
        return (codepoint>='0'&&codepoint<='9')||(codepoint=='-'&&current.empty());
    });
    showHome();
}

void MainMenu::showHome() {
    m_page = Page::Home;
    rebuildButtons();
}

void MainMenu::showWorlds() {
    m_page = Page::Worlds;
    rebuildButtons();
}

void MainMenu::showCreate() {
    m_page = Page::Create;
    m_worldName.setText(m_localization.text("menu.create.default_name"));
    m_seedText.setText({});
    m_createMode = GameMode::Survival;
    m_createWorldType = WorldType::Normal;
    m_createCheats = false;
    m_field = Field::Name;
    rebuildButtons();
}

void MainMenu::refreshWorlds() {
    std::string selectedId;
    if (m_selectedWorld >= 0 &&
        m_selectedWorld < static_cast<int>(m_worlds.size())) {
        selectedId = m_worlds[static_cast<size_t>(m_selectedWorld)].id;
    }
    if (m_callbacks.onRefreshWorlds)
        m_worlds = m_callbacks.onRefreshWorlds();
    m_selectedWorld = -1;
    if (!selectedId.empty()) {
        const auto selected = std::find_if(
            m_worlds.begin(), m_worlds.end(), [&selectedId](const WorldSummary& world) {
                return world.id == selectedId;
            });
        if (selected != m_worlds.end())
            m_selectedWorld = static_cast<int>(selected - m_worlds.begin());
    }
    const int maximum = std::max(0, static_cast<int>(m_worlds.size()) - 6);
    m_worldOffset = std::clamp(m_worldOffset, 0, maximum);
    if (m_selectedWorld >= 0) {
        if (m_selectedWorld < m_worldOffset) m_worldOffset = m_selectedWorld;
        if (m_selectedWorld >= m_worldOffset + 6)
            m_worldOffset = m_selectedWorld - 5;
    }
    m_pendingDeleteWorldId.clear();
    m_lastWorldClick = -1.0;
    m_lastWorldIndex = -1;
    rebuildButtons();
}

std::string MainMenu::fieldLabel(Field field, const std::string& value) const {
    const bool active = field == m_field;
    const std::string name = m_localization.text(
        field == Field::Name ? "menu.create.world_name" : "menu.create.seed");
    return std::string(active ? "> " : "") + name + ": " +
           (value.empty() && field == Field::Seed
                ? m_localization.text("menu.create.random") : value);
}

void MainMenu::rebuildButtons() {
    m_buttons.clear();
    m_deleteButtons.clear();
    if (m_page == Page::Home) {
        m_buttons.emplace_back(m_localization.text("menu.home.singleplayer"),
                               [this]() { showWorlds(); });
        m_buttons.emplace_back(m_localization.text("menu.home.settings"),
                               m_callbacks.onOpenSettings);
        m_buttons.emplace_back(m_localization.text("menu.home.language"), [this]() {
            m_settings.language = m_settings.language == Language::English
                ? Language::SimplifiedChinese : Language::English;
            m_localization.setLanguage(m_settings.language);
            if (m_callbacks.onSettingsChanged) m_callbacks.onSettingsChanged();
            rebuildButtons();
        });
        m_buttons.emplace_back(m_localization.text("menu.home.quit"), m_callbacks.onQuit);
    } else if (m_page == Page::Worlds) {
        const int visible = 6;
        const int end = std::min(static_cast<int>(m_worlds.size()), m_worldOffset + visible);
        for (int index = m_worldOffset; index < end; ++index) {
            const auto& world = m_worlds[static_cast<size_t>(index)];
            const std::string mode = m_localization.text(
                world.mode == GameMode::Survival ? "common.survival" :
                world.mode == GameMode::Creative ? "common.creative" : "common.spectator");
            m_buttons.emplace_back((index == m_selectedWorld ? "> " : "") +
                world.displayName + " [" + (world.compatible ? mode :
                    m_localization.format("menu.worlds.incompatible", {
                        std::to_string(world.generationVersion)})) + "]", [this, index]() {
                    m_selectedWorld = index;
                    rebuildButtons();
                });
            const std::string worldId = world.id;
            m_deleteButtons.emplace_back(
                m_localization.text(m_pendingDeleteWorldId == worldId
                    ? "menu.worlds.confirm_delete" : "menu.worlds.delete"),
                [this, worldId]() {
                    if (m_pendingDeleteWorldId != worldId) {
                        m_pendingDeleteWorldId = worldId;
                        rebuildButtons();
                        return;
                    }
                    if (m_callbacks.onDeleteWorld)
                        (void)m_callbacks.onDeleteWorld(worldId);
                    refreshWorlds();
                });
        }
        m_buttons.emplace_back(m_localization.text("menu.worlds.refresh"),
                               [this]() { refreshWorlds(); });
        m_buttons.emplace_back(m_localization.text("menu.worlds.play"), [this]() {
            if (m_selectedWorld >= 0 && m_selectedWorld < static_cast<int>(m_worlds.size()) &&
                m_worlds[static_cast<size_t>(m_selectedWorld)].compatible)
                m_callbacks.onOpenWorld(m_worlds[static_cast<size_t>(m_selectedWorld)].id);
        });
        m_buttons.emplace_back(m_localization.text("menu.worlds.create"),
                               [this]() { showCreate(); });
        m_buttons.emplace_back(m_localization.text("common.back"), [this]() { showHome(); });
    } else {
        m_buttons.emplace_back(fieldLabel(Field::Name, m_worldName.text()),
                               [this]() { selectField(Field::Name); });
        m_buttons.emplace_back(
            m_localization.format("menu.create.game_mode", {m_localization.text(
                m_createMode == GameMode::Survival
                    ? "common.survival" : "common.creative")}),
            [this]() {
                m_createMode = m_createMode == GameMode::Survival
                    ? GameMode::Creative : GameMode::Survival;
                rebuildButtons();
            });
        m_buttons.emplace_back(
            m_localization.format("menu.create.world_type", {m_localization.text(
                m_createWorldType == WorldType::Normal
                    ? "common.normal" : "common.superflat")}),
            [this]() {
                m_createWorldType = m_createWorldType == WorldType::Normal
                    ? WorldType::Superflat : WorldType::Normal;
                rebuildButtons();
            });
        m_buttons.emplace_back(fieldLabel(Field::Seed, m_seedText.text()),
                               [this]() { selectField(Field::Seed); });
        m_buttons.emplace_back(
            m_localization.format("menu.create.cheats", {m_localization.text(
                m_createCheats ? "common.on" : "common.off")}),
            [this]() {
                m_createCheats = !m_createCheats;
                rebuildButtons();
            });
        m_buttons.emplace_back(m_localization.text("menu.create.confirm"), [this]() {
            m_callbacks.onCreateWorld(
                m_worldName.text(), m_seedText.text(), m_createMode,
                m_createWorldType, m_createCheats);
        });
        m_buttons.emplace_back(m_localization.text("common.cancel"), [this]() { showWorlds(); });
    }
    m_selectedIdx = 0;
    if (!m_buttons.empty()) m_buttons[0].setSelected(true);
}

void MainMenu::selectField(Field field) {
    m_field = field;
    rebuildButtons();
    m_selectedIdx = field == Field::Name ? 0 : 3;
    m_buttons[0].setSelected(false);
    m_buttons[m_selectedIdx].setSelected(true);
}

void MainMenu::render(UIRenderer& ui, int screenWidth, int screenHeight) {
    ui.drawRect(0.0f, 0.0f, static_cast<float>(screenWidth),
                static_cast<float>(screenHeight), glm::vec4(.08f,.11f,.15f,1));
    constexpr float tile = 32.0f;
    for (float y=0;y<screenHeight;y+=tile) for (float x=0;x<screenWidth;x+=tile) {
        const bool alternate = (static_cast<int>(x/tile)+static_cast<int>(y/tile))%2;
        ui.drawRect(x,y,tile,tile,alternate ? glm::vec4(.15f,.12f,.09f,1)
                                            : glm::vec4(.18f,.14f,.10f,1));
        ui.drawRect(x+2,y+2,tile-4,tile-4,{.20f,.16f,.11f,1});
    }
    const float panelW = std::min(520.0f, screenWidth - 24.0f);
    ui.drawPanel((screenWidth-panelW)*.5f, 18.0f, panelW, screenHeight-36.0f,
                 {.07f,.075f,.09f,.94f});

    const std::string title = m_page == Page::Home ? "MINECRAFTC" :
        m_localization.text(m_page == Page::Worlds
            ? "menu.worlds.title" : "menu.create.title");
    float titleScale = 4.5f;
    auto titleSize = ui.measureText(title, titleScale);
    float titleX = (screenWidth - titleSize.x) * 0.5f;
    float titleY = screenHeight * 0.68f;
    ui.renderText(title, titleX, titleY, titleScale,
                  glm::vec3(1.0f, 0.85f, 0.3f));  // gold

    const std::string subtitle = m_localization.text(
        m_page == Page::Create ? "menu.create.subtitle" :
        m_page == Page::Worlds ? "menu.worlds.subtitle" : "menu.home.subtitle");
    float subScale = 1.5f;
    auto subSize = ui.measureText(subtitle, subScale);
    float subX = (screenWidth - subSize.x) * 0.5f;
    float subY = titleY - titleSize.y - 20.0f;
    ui.renderText(subtitle, subX, subY, subScale,
                  glm::vec3(0.6f, 0.6f, 0.7f));

    float detailsReserve = 0.0f;
    if (m_page == Page::Worlds && m_selectedWorld >= 0 &&
        m_selectedWorld < static_cast<int>(m_worlds.size())) {
        const auto& world = m_worlds[static_cast<size_t>(m_selectedWorld)];
        const std::string worldType = m_localization.text(
            world.worldType == WorldType::Normal
                ? "common.normal" : "common.superflat");
        const std::string details = world.compatible
            ? m_localization.format("menu.worlds.details", {
                std::to_string(world.seed),
                std::to_string(world.worldTicks / 1200), worldType})
            : m_localization.text("menu.worlds.unsupported");
        const auto size = ui.measureText(details, 1.0f);
        ui.renderText(details, (screenWidth - size.x) * 0.5f, subY - 24.0f,
                      1.0f, glm::vec3(0.78f));
        detailsReserve = size.y + 14.0f;
    }

    // Buttons
    float buttonStartY = subY - subSize.y - 38.0f - detailsReserve;
    float buttonX = (screenWidth - Config::UI_BUTTON_WIDTH) * 0.5f;
    const float buttonHeight = std::clamp(
        (buttonStartY - 16.0f) / std::max<size_t>(1, m_buttons.size()) - 5.0f,
        22.0f, Config::UI_BUTTON_HEIGHT);
    const float spacing = std::min(Config::UI_BUTTON_SPACING, 7.0f);

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        float by = buttonStartY - static_cast<float>(i) * (buttonHeight + spacing);
        const bool worldRow =
            m_page == Page::Worlds && i < m_deleteButtons.size();
        constexpr float deleteWidth = 84.0f;
        constexpr float deleteGap = 8.0f;
        const float rowX = (screenWidth -
            (Config::UI_BUTTON_WIDTH + deleteGap + deleteWidth)) * 0.5f;
        m_buttons[i].setPosition(worldRow ? rowX : buttonX, by);
        m_buttons[i].setSize(Config::UI_BUTTON_WIDTH, buttonHeight);
        m_buttons[i].render(ui);
        if (worldRow) {
            m_deleteButtons[i].setPosition(
                rowX + Config::UI_BUTTON_WIDTH + deleteGap, by);
            m_deleteButtons[i].setSize(deleteWidth, buttonHeight);
            m_deleteButtons[i].render(ui);
        }
    }

    ui.renderText(Config::GAME_VERSION, 8.0f, 8.0f, 1.0f,
                  glm::vec3(0.62f, 0.62f, 0.66f));
}

void MainMenu::onKeyPress(int key, int mods) {
    if (m_page == Page::Create && key == Key::Tab) {
        selectField(m_field == Field::Name ? Field::Seed : Field::Name);
        return;
    }
    if (m_page == Page::Create && key == Key::Backspace) {
        TextEditBuffer& value = m_field == Field::Name ? m_worldName : m_seedText;
        value.backspace();
        rebuildButtons();
        selectField(m_field);
        return;
    }
    if (m_page == Page::Create) {
        TextEditBuffer& value = m_field == Field::Name ? m_worldName : m_seedText;
        const bool selecting = (mods & KeyModifier::Shift) != 0;
        const bool control = (mods & KeyModifier::Control) != 0;
        bool edited = true;
        if (key == Key::Delete) value.eraseForward();
        else if (key == Key::Left) value.moveLeft(selecting);
        else if (key == Key::Right) value.moveRight(selecting);
        else if (key == Key::Home) value.moveHome(selecting);
        else if (key == Key::End) value.moveEnd(selecting);
        else if (control && key == Key::A) value.selectAll();
        else if (control && key == Key::C) value.copySelection();
        else if (control && key == Key::X) value.cutSelection();
        else if (control && key == Key::V) value.pasteClipboard();
        else edited = false;
        if (edited) { rebuildButtons(); selectField(m_field); return; }
    }
    // Printable keys remain text input on the create screen. Arrow keys and
    // Enter provide unambiguous keyboard navigation while a field is focused.
    if (m_page == Page::Create &&
        (key == Key::W || key == Key::S || key == Key::Space)) {
        return;
    }
    if (key == Key::Escape) {
        if (m_page == Page::Create) showWorlds();
        else if (m_page == Page::Worlds) showHome();
        return;
    }
    if (m_page == Page::Worlds && key == Key::Enter && m_selectedWorld >= 0 &&
        m_selectedIdx == m_selectedWorld - m_worldOffset) {
        if (m_worlds[static_cast<size_t>(m_selectedWorld)].compatible)
            m_callbacks.onOpenWorld(m_worlds[static_cast<size_t>(m_selectedWorld)].id);
        return;
    }
    switch (key) {
        case Key::Up:
        case Key::W:
            navigateUp(m_buttons, m_selectedIdx);
            break;
        case Key::Down:
        case Key::S:
            navigateDown(m_buttons, m_selectedIdx);
            break;
        case Key::Enter:
        case Key::Space:
            activateSelected(m_buttons, m_selectedIdx);
            break;
        default:
            break;
    }
}

void MainMenu::onChar(unsigned int codepoint) {
    if (m_page != Page::Create || codepoint < 32) return;
    TextEditBuffer& value = m_field == Field::Name ? m_worldName : m_seedText;
    if (m_field == Field::Seed) {
        const char character = static_cast<char>(codepoint);
        if ((character == '-' && value.text().empty()) ||
            (character >= '0' && character <= '9')) {
            std::string encoded(1, character); value.insert(encoded);
        }
    } else {
        std::string encoded; appendUtf8(encoded, codepoint); value.insert(encoded);
    }
    rebuildButtons();
    selectField(m_field);
}

void MainMenu::onMouseMove(double x, double y) {
    for (auto& btn : m_buttons) {
        btn.setHovered(btn.containsPoint(static_cast<float>(x),
                                          static_cast<float>(y)));
    }
    for (auto& btn : m_deleteButtons) {
        btn.setHovered(btn.containsPoint(static_cast<float>(x),
                                         static_cast<float>(y)));
    }
}

void MainMenu::onMouseButton(int button, ButtonAction action, double x, double y) {
    if (button != MouseButton::Left) return;
    if (action == ButtonAction::Press) {
        m_pressedButton = -1;
        m_pressedDeleteButton = -1;
        for (size_t i = 0; i < m_deleteButtons.size(); ++i) {
            if (m_deleteButtons[i].containsPoint(
                    static_cast<float>(x), static_cast<float>(y))) {
                m_pressedDeleteButton = static_cast<int>(i);
                m_deleteButtons[i].setPressed(true);
                return;
            }
        }
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
                m_pressedButton = static_cast<int>(i);
                m_buttons[i].setPressed(true);
                return;
            }
        }
    } else if (action == ButtonAction::Release && m_pressedDeleteButton >= 0) {
        const int captured = m_pressedDeleteButton;
        m_pressedDeleteButton = -1;
        m_deleteButtons[static_cast<size_t>(captured)].setPressed(false);
        if (m_deleteButtons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y))) {
            m_deleteButtons[static_cast<size_t>(captured)].activate();
        }
    } else if (action == ButtonAction::Release && m_pressedButton >= 0) {
        const int captured = m_pressedButton;
        m_pressedButton = -1;
        m_buttons[static_cast<size_t>(captured)].setPressed(false);
        if (m_buttons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y))) {
            const int visibleWorlds = std::min(6, static_cast<int>(m_worlds.size()) - m_worldOffset);
            if (m_page == Page::Worlds && captured < visibleWorlds) {
                const int worldIndex = m_worldOffset + captured;
                const double now = RuntimeClock::seconds(RuntimeClock{}.now());
                if (m_lastWorldIndex == worldIndex && m_lastWorldClick >= 0.0 &&
                    now - m_lastWorldClick <= 0.35) {
                    m_callbacks.onOpenWorld(m_worlds[static_cast<size_t>(worldIndex)].id);
                    return;
                }
                m_lastWorldIndex = worldIndex;
                m_lastWorldClick = now;
            }
            m_buttons[static_cast<size_t>(captured)].activate();
        }
    }
}

void MainMenu::onScroll(double yOffset) {
    if (m_page != Page::Worlds || m_worlds.size() <= 6) return;
    const int maximum = std::max(0, static_cast<int>(m_worlds.size()) - 6);
    m_worldOffset = std::clamp(m_worldOffset + (yOffset < 0 ? 1 : -1), 0, maximum);
    rebuildButtons();
}

// ── Pause Menu ────────────────────────────────────────────────────────────

PauseMenu::PauseMenu(
    const MenuCallbacks& callbacks, const Localization& localization) {
    m_buttons.emplace_back(localization.text("menu.pause.resume"), callbacks.onResume);
    m_buttons.emplace_back(localization.text("menu.home.settings"), callbacks.onOpenSettings);
    m_buttons.emplace_back(localization.text("menu.pause.back"), callbacks.onBackToMenu);
    m_buttons.emplace_back(localization.text("menu.home.quit"), callbacks.onQuit);

    if (!m_buttons.empty()) {
        m_buttons[0].setSelected(true);
    }
}

void PauseMenu::render(UIRenderer& ui, int screenWidth, int screenHeight) {
    // Semi-transparent overlay
    ui.drawRect(0.0f, 0.0f, static_cast<float>(screenWidth),
                static_cast<float>(screenHeight),
                glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));

    // "PAUSED" title
    const std::string title = ui.localization().text("menu.pause.title");
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

void PauseMenu::onKeyPress(int key, int) {
    switch (key) {
        case Key::Up:
        case Key::W:
            navigateUp(m_buttons, m_selectedIdx);
            break;
        case Key::Down:
        case Key::S:
            navigateDown(m_buttons, m_selectedIdx);
            break;
        case Key::Enter:
        case Key::Space:
            activateSelected(m_buttons, m_selectedIdx);
            break;
        case Key::Escape:
            // ESC acts as Resume in pause menu
            if (!m_buttons.empty()) {
                m_buttons[0].activate();  // "Resume" button
            }
            break;
        default:
            break;
    }
}

void PauseMenu::onMouseMove(double x, double y) {
    for (auto& btn : m_buttons) {
        btn.setHovered(btn.containsPoint(static_cast<float>(x),
                                          static_cast<float>(y)));
    }
}

void PauseMenu::onMouseButton(int button, ButtonAction action, double x, double y) {
    if (button != MouseButton::Left) return;
    if (action == ButtonAction::Press) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
                m_pressedButton = static_cast<int>(i);
                m_buttons[i].setPressed(true);
                return;
            }
        }
    } else if (action == ButtonAction::Release && m_pressedButton >= 0) {
        const int captured = m_pressedButton;
        m_pressedButton = -1;
        m_buttons[static_cast<size_t>(captured)].setPressed(false);
        if (m_buttons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y)))
            m_buttons[static_cast<size_t>(captured)].activate();
    }
}

// ── Sleep Menu ───────────────────────────────────────────────────────────

SleepMenu::SleepMenu(
    const MenuCallbacks& callbacks, const Localization& localization,
    bool heaven) {
    m_buttons.emplace_back(localization.text("sleep.until_morning"),
        [callbacks]() { if (callbacks.onSleepAction) callbacks.onSleepAction(0); });
    m_buttons.emplace_back(localization.text("sleep.leave_bed"),
        [callbacks]() { if (callbacks.onSleepAction) callbacks.onSleepAction(1); });
    if (!heaven) {
        m_buttons.emplace_back(localization.text("sleep.travel_heaven"),
            [callbacks]() { if (callbacks.onSleepAction) callbacks.onSleepAction(2); });
    }
    if (!m_buttons.empty()) m_buttons[0].setSelected(true);
}

void SleepMenu::render(UIRenderer& ui, int screenWidth, int screenHeight) {
    ui.drawRect(0.0f, 0.0f, static_cast<float>(screenWidth),
                static_cast<float>(screenHeight),
                glm::vec4(0.0f, 0.0f, 0.02f, 0.38f));
    const std::string title = ui.localization().text("sleep.title");
    constexpr float titleScale = 3.0f;
    const auto titleSize = ui.measureText(title, titleScale);
    const float titleX = (screenWidth - titleSize.x) * 0.5f;
    const float titleY = screenHeight * 0.62f;
    ui.renderText(title, titleX, titleY, titleScale,
                  glm::vec3(1.0f, 0.88f, 0.42f));
    const float startY = titleY - titleSize.y - 40.0f;
    const float buttonX = (screenWidth - Config::UI_BUTTON_WIDTH) * 0.5f;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const float y = startY - static_cast<float>(i) *
            (Config::UI_BUTTON_HEIGHT + Config::UI_BUTTON_SPACING);
        m_buttons[i].setPosition(buttonX, y);
        m_buttons[i].setSize(Config::UI_BUTTON_WIDTH, Config::UI_BUTTON_HEIGHT);
        m_buttons[i].render(ui);
    }
}

void SleepMenu::onKeyPress(int key, int) {
    switch (key) {
        case Key::Up:
        case Key::W: navigateUp(m_buttons, m_selectedIdx); break;
        case Key::Down:
        case Key::S: navigateDown(m_buttons, m_selectedIdx); break;
        case Key::Enter:
        case Key::Space: activateSelected(m_buttons, m_selectedIdx); break;
        case Key::Escape:
            if (m_buttons.size() > 1) m_buttons[1].activate();
            break;
        default: break;
    }
}

void SleepMenu::onMouseMove(double x, double y) {
    for (auto& button : m_buttons)
        button.setHovered(button.containsPoint(static_cast<float>(x),
                                               static_cast<float>(y)));
}

void SleepMenu::onMouseButton(
    int button, ButtonAction action, double x, double y) {
    if (button != MouseButton::Left) return;
    if (action == ButtonAction::Press) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (!m_buttons[i].containsPoint(static_cast<float>(x),
                                            static_cast<float>(y))) continue;
            m_pressedButton = static_cast<int>(i);
            m_buttons[i].setPressed(true);
            return;
        }
    } else if (action == ButtonAction::Release && m_pressedButton >= 0) {
        const int selected = m_pressedButton;
        m_pressedButton = -1;
        m_buttons[static_cast<size_t>(selected)].setPressed(false);
        if (m_buttons[static_cast<size_t>(selected)].containsPoint(
                static_cast<float>(x), static_cast<float>(y)))
            m_buttons[static_cast<size_t>(selected)].activate();
    }
}
