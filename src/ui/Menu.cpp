#include "ui/Menu.h"
#include "ui/UIRenderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>

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

MainMenu::MainMenu(const MenuCallbacks& callbacks, std::vector<WorldSummary> worlds)
    : m_callbacks(callbacks), m_worlds(std::move(worlds)) {
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
    m_worldName = "New World";
    m_seedText.clear();
    m_createMode = GameMode::Survival;
    m_createCheats = false;
    m_field = Field::Name;
    rebuildButtons();
}

std::string MainMenu::fieldLabel(const char* name, const std::string& value) const {
    const bool active = (name == std::string("World Name") && m_field == Field::Name) ||
                        (name == std::string("Seed") && m_field == Field::Seed);
    return std::string(active ? "> " : "") + name + ": " +
           (value.empty() ? (name == std::string("Seed") ? "<random>" : "") : value);
}

void MainMenu::rebuildButtons() {
    m_buttons.clear();
    if (m_page == Page::Home) {
        m_buttons.emplace_back("Singleplayer", [this]() { showWorlds(); });
        m_buttons.emplace_back("Settings", m_callbacks.onOpenSettings);
        m_buttons.emplace_back("Quit", m_callbacks.onQuit);
    } else if (m_page == Page::Worlds) {
        const int visible = 6;
        const int end = std::min(static_cast<int>(m_worlds.size()), m_worldOffset + visible);
        for (int index = m_worldOffset; index < end; ++index) {
            const auto& world = m_worlds[static_cast<size_t>(index)];
            const std::string mode =
                world.mode == GameMode::Survival ? "Survival" :
                world.mode == GameMode::Creative ? "Creative" : "Spectator";
            m_buttons.emplace_back((index == m_selectedWorld ? "> " : "") +
                world.displayName + " [" + (world.compatible ? mode : "Incompatible v" +
                    std::to_string(world.generationVersion)) + "]", [this, index]() {
                    m_selectedWorld = index;
                    rebuildButtons();
                });
        }
        m_buttons.emplace_back("Play Selected World", [this]() {
            if (m_selectedWorld >= 0 && m_selectedWorld < static_cast<int>(m_worlds.size()) &&
                m_worlds[static_cast<size_t>(m_selectedWorld)].compatible)
                m_callbacks.onOpenWorld(m_worlds[static_cast<size_t>(m_selectedWorld)].id);
        });
        m_buttons.emplace_back("Create New World", [this]() { showCreate(); });
        m_buttons.emplace_back("Back", [this]() { showHome(); });
    } else {
        m_buttons.emplace_back(fieldLabel("World Name", m_worldName),
                               [this]() { selectField(Field::Name); });
        m_buttons.emplace_back(
            std::string("Game Mode: ") +
                (m_createMode == GameMode::Survival ? "Survival" : "Creative"),
            [this]() {
                m_createMode = m_createMode == GameMode::Survival
                    ? GameMode::Creative : GameMode::Survival;
                rebuildButtons();
            });
        m_buttons.emplace_back(fieldLabel("Seed", m_seedText),
                               [this]() { selectField(Field::Seed); });
        m_buttons.emplace_back(
            std::string("Allow Cheats: ") + (m_createCheats ? "ON" : "OFF"),
            [this]() {
                m_createCheats = !m_createCheats;
                rebuildButtons();
            });
        m_buttons.emplace_back("Create World", [this]() {
            m_callbacks.onCreateWorld(
                m_worldName, m_seedText, m_createMode, m_createCheats);
        });
        m_buttons.emplace_back("Cancel", [this]() { showWorlds(); });
    }
    m_selectedIdx = 0;
    if (!m_buttons.empty()) m_buttons[0].setSelected(true);
}

void MainMenu::selectField(Field field) {
    m_field = field;
    rebuildButtons();
    m_selectedIdx = field == Field::Name ? 0 : 2;
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

    const char* title = m_page == Page::Home ? "MINECRAFTC" :
                        m_page == Page::Worlds ? "SELECT WORLD" : "CREATE NEW WORLD";
    float titleScale = 4.5f;
    auto titleSize = ui.measureText(title, titleScale);
    float titleX = (screenWidth - titleSize.x) * 0.5f;
    float titleY = screenHeight * 0.68f;
    ui.renderText(title, titleX, titleY, titleScale,
                  glm::vec3(1.0f, 0.85f, 0.3f));  // gold

    const char* subtitle = m_page == Page::Create
        ? "Choose a mode and enter an optional numeric seed"
        : (m_page == Page::Worlds ? "Select a saved world" : "A C++ Voxel Engine");
    float subScale = 1.5f;
    auto subSize = ui.measureText(subtitle, subScale);
    float subX = (screenWidth - subSize.x) * 0.5f;
    float subY = titleY - titleSize.y - 20.0f;
    ui.renderText(subtitle, subX, subY, subScale,
                  glm::vec3(0.6f, 0.6f, 0.7f));

    if (m_page == Page::Worlds && m_selectedWorld >= 0 &&
        m_selectedWorld < static_cast<int>(m_worlds.size())) {
        const auto& world = m_worlds[static_cast<size_t>(m_selectedWorld)];
        const std::string details = world.compatible
            ? "Seed " + std::to_string(world.seed) + "  |  Played " +
                std::to_string(world.worldTicks / 1200) + " min"
            : "This generation version is not supported";
        const auto size = ui.measureText(details, 1.0f);
        ui.renderText(details, (screenWidth - size.x) * 0.5f, subY - 24.0f,
                      1.0f, glm::vec3(0.78f));
    }

    // Buttons
    float buttonStartY = subY - subSize.y - 38.0f;
    float buttonX = (screenWidth - Config::UI_BUTTON_WIDTH) * 0.5f;
    const float buttonHeight = std::clamp(
        (buttonStartY - 16.0f) / std::max<size_t>(1, m_buttons.size()) - 5.0f,
        22.0f, Config::UI_BUTTON_HEIGHT);
    const float spacing = std::min(Config::UI_BUTTON_SPACING, 7.0f);

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        float by = buttonStartY - static_cast<float>(i) * (buttonHeight + spacing);
        m_buttons[i].setPosition(buttonX, by);
        m_buttons[i].setSize(Config::UI_BUTTON_WIDTH, buttonHeight);
        m_buttons[i].render(ui);
    }
}

void MainMenu::onKeyPress(int key) {
    if (m_page == Page::Create && key == GLFW_KEY_TAB) {
        selectField(m_field == Field::Name ? Field::Seed : Field::Name);
        return;
    }
    if (m_page == Page::Create && key == GLFW_KEY_BACKSPACE) {
        std::string& value = m_field == Field::Name ? m_worldName : m_seedText;
        if (!value.empty()) value.pop_back();
        rebuildButtons();
        selectField(m_field);
        return;
    }
    // Printable keys remain text input on the create screen. Arrow keys and
    // Enter provide unambiguous keyboard navigation while a field is focused.
    if (m_page == Page::Create &&
        (key == GLFW_KEY_W || key == GLFW_KEY_S || key == GLFW_KEY_SPACE)) {
        return;
    }
    if (key == GLFW_KEY_ESCAPE) {
        if (m_page == Page::Create) showWorlds();
        else if (m_page == Page::Worlds) showHome();
        return;
    }
    if (m_page == Page::Worlds && key == GLFW_KEY_ENTER && m_selectedWorld >= 0 &&
        m_selectedIdx == m_selectedWorld - m_worldOffset) {
        if (m_worlds[static_cast<size_t>(m_selectedWorld)].compatible)
            m_callbacks.onOpenWorld(m_worlds[static_cast<size_t>(m_selectedWorld)].id);
        return;
    }
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

void MainMenu::onChar(unsigned int codepoint) {
    if (m_page != Page::Create || codepoint < 32 || codepoint > 126) return;
    char character = static_cast<char>(codepoint);
    std::string& value = m_field == Field::Name ? m_worldName : m_seedText;
    if (m_field == Field::Seed) {
        if (character == '-' && value.empty()) value.push_back(character);
        else if (character >= '0' && character <= '9' && value.size() < 20)
            value.push_back(character);
    } else if (value.size() < 32) {
        value.push_back(character);
    }
    rebuildButtons();
    selectField(m_field);
}

void MainMenu::onMouseMove(double x, double y) {
    for (auto& btn : m_buttons) {
        btn.setHovered(btn.containsPoint(static_cast<float>(x),
                                          static_cast<float>(y)));
    }
}

void MainMenu::onMouseButton(int button, int action, double x, double y) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
                m_pressedButton = static_cast<int>(i);
                m_buttons[i].setPressed(true);
                return;
            }
        }
    } else if (action == GLFW_RELEASE && m_pressedButton >= 0) {
        const int captured = m_pressedButton;
        m_pressedButton = -1;
        m_buttons[static_cast<size_t>(captured)].setPressed(false);
        if (m_buttons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y))) {
            const int visibleWorlds = std::min(6, static_cast<int>(m_worlds.size()) - m_worldOffset);
            if (m_page == Page::Worlds && captured < visibleWorlds) {
                const int worldIndex = m_worldOffset + captured;
                const double now = glfwGetTime();
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

PauseMenu::PauseMenu(const MenuCallbacks& callbacks) {
    m_buttons.emplace_back("Resume",        callbacks.onResume);
    m_buttons.emplace_back("Settings",      callbacks.onOpenSettings);
    m_buttons.emplace_back("Back to Menu",  callbacks.onBackToMenu);
    m_buttons.emplace_back("Quit",          callbacks.onQuit);

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
    const char* title = "PAUSED";
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

void PauseMenu::onKeyPress(int key) {
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
        case GLFW_KEY_ESCAPE:
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

void PauseMenu::onMouseButton(int button, int action, double x, double y) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
                m_pressedButton = static_cast<int>(i);
                m_buttons[i].setPressed(true);
                return;
            }
        }
    } else if (action == GLFW_RELEASE && m_pressedButton >= 0) {
        const int captured = m_pressedButton;
        m_pressedButton = -1;
        m_buttons[static_cast<size_t>(captured)].setPressed(false);
        if (m_buttons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y)))
            m_buttons[static_cast<size_t>(captured)].activate();
    }
}
