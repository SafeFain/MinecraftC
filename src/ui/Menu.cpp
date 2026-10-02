#include "ui/Menu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"

#include "core/Window.h"
#include "core/RuntimeClock.h"
#include <algorithm>
#include "game/Utf8.h"

namespace {
struct ThirdPartyCredit {
    const char* name;
    const char* repository;
    const char* license;
};

// Keep these aligned with CMake dependencies and the bundled upstream records.
constexpr ThirdPartyCredit THIRD_PARTY_CREDITS[] = {
    {"SDL", "https://github.com/libsdl-org/SDL", "Zlib"},
    {"GLM", "https://github.com/g-truc/glm", "MIT"},
    {"Vulkan Headers", "https://github.com/KhronosGroup/Vulkan-Headers", "Apache-2.0 OR MIT"},
    {"Vulkan Loader", "https://github.com/KhronosGroup/Vulkan-Loader", "Apache-2.0"},
    {"MoltenVK (macOS / iOS)", "https://github.com/KhronosGroup/MoltenVK", "Apache-2.0"},
    {"Vulkan Memory Allocator", "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator", "MIT"},
    {"FastNoiseLite", "https://github.com/Auburn/FastNoiseLite", "MIT"},
    {"cgltf", "https://github.com/jkuhlmann/cgltf", "MIT"},
    {"nlohmann/json", "https://github.com/nlohmann/json", "MIT"},
    {"stb_image / stb_truetype", "https://github.com/nothings/stb", "MIT"},
    {"Noto Sans CJK SC", "https://github.com/notofonts/noto-cjk", "SIL OFL 1.1"},
    {"Noto Naskh Arabic", "https://github.com/notofonts/arabic", "SIL OFL 1.1"},
};
constexpr int CREDITS_PER_PAGE = 4;
constexpr int CREDIT_COUNT = sizeof(THIRD_PARTY_CREDITS) / sizeof(ThirdPartyCredit);
constexpr int CREDIT_PAGE_COUNT = (CREDIT_COUNT + CREDITS_PER_PAGE - 1) / CREDITS_PER_PAGE;

float fittedTextScale(UIRenderer& ui, const std::string& text, float scale, float width) {
    const float measured = ui.measureText(text, scale).x;
    return measured > width && measured > 0.0f ? scale * width / measured : scale;
}
}

// ── Button ────────────────────────────────────────────────────────────────

Button::Button(const std::string& label, std::function<void()> onClick)
    : m_label(label), m_onClick(std::move(onClick)) {}

bool Button::containsPoint(float px, float py) const {
    return m_w>0 && m_h>0 && px >= m_x && px <= m_x + m_w &&
           py >= m_y && py <= m_y + m_h;
}

void Button::render(UIRenderer& ui) const {
    UiTheme::WidgetState state = UiTheme::WidgetState::Normal;
    if (m_pressed) state = UiTheme::WidgetState::Pressed;
    else if (m_selected) state = UiTheme::WidgetState::Selected;
    else if (m_hovered) state = UiTheme::WidgetState::Hover;
    if (m_animationFrame != ui.frameSerial()) {
        m_animationFrame=ui.frameSerial();
        m_hoverTransition.tick(ui.frameDelta(),m_hovered||m_selected,0.10f);
        m_pressTransition.tick(ui.frameDelta(),m_pressed,0.08f);
    }
    const float alpha=m_enabled?1.0f:.45f;
    if (m_detail.empty()) {
        UiTheme::button(ui, m_x, m_y, m_w, m_h, m_label, state, m_danger,0,alpha,m_hoverTransition.value,m_pressTransition.value,m_primary,m_bottomInset);
        return;
    }
    UiTheme::button(ui, m_x, m_y, m_w, m_h, "", state, m_danger,0,alpha,m_hoverTransition.value,m_pressTransition.value,m_primary,m_bottomInset);
    const float padding = std::min(5.0f, m_h * 0.12f);
    const float scale = std::min(1.4f, (m_h - 2.0f * padding - 2.0f) / 28.0f);
    const auto color = m_hovered || m_selected ? UiTheme::TEXT_HOVER : UiTheme::TEXT;
    const float labelScale = fittedTextScale(ui, m_label, scale, m_w - 20.0f);
    const float detailScale = fittedTextScale(ui, m_detail, scale * 0.85f, m_w - 20.0f);
    UiTheme::textWithShadow(ui, m_label, m_x + 10.0f, m_y + m_h * 0.5f + 1.0f,
                            labelScale,color,alpha);
    UiTheme::textWithShadow(ui, m_detail, m_x + 10.0f, m_y + padding,
                            detailScale,UiTheme::TEXT_DIM,alpha);
}

void Button::activate() {
    if (m_enabled && m_onClick) m_onClick();
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
    resetTransition();
    m_page = Page::Home;
    rebuildButtons();
}

void MainMenu::showWorlds() {
    resetTransition();
    m_page = Page::Worlds;
    rebuildButtons();
}

void MainMenu::showCreate() {
    resetTransition();
    m_page = Page::Create;
    m_formOffset=0;m_formSelection=-1;
    m_worldName.setText(m_localization.text("menu.create.default_name"));
    m_seedText.setText({});
    m_createMode = GameMode::Survival;
    m_createWorldType = WorldType::Normal;
    m_createCheats = false;
    m_field = Field::Name;
    rebuildButtons();
}

void MainMenu::showAbout() {
    resetTransition();
    m_page = Page::About;
    m_aboutPage = 0;
    rebuildButtons();
}

void MainMenu::changeAboutPage(int delta) {
    const int page = std::clamp(m_aboutPage + delta, 0, CREDIT_PAGE_COUNT - 1);
    if (page == m_aboutPage) return;
    m_aboutPage = page;
    m_pressedButton = -1;
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
    const int maximum = std::max(0, static_cast<int>(m_worlds.size()) - m_visibleWorlds);
    m_worldOffset = std::clamp(m_worldOffset, 0, maximum);
    if (m_selectedWorld >= 0) {
        if (m_selectedWorld < m_worldOffset) m_worldOffset = m_selectedWorld;
        if (m_selectedWorld >= m_worldOffset + m_visibleWorlds)
            m_worldOffset = m_selectedWorld-m_visibleWorlds+1;
    }
    m_pendingDeleteWorldId.clear();
    m_lastWorldClick = -1.0;
    m_lastWorldIndex = -1;
    rebuildButtons();
}

void MainMenu::rebuildButtons() {
    m_buttons.clear();
    m_deleteButtons.clear();
    if (m_page == Page::Home) {
        m_buttons.emplace_back(m_localization.text("menu.home.singleplayer"),
                               [this]() { showWorlds(); });
        m_buttons.emplace_back(m_localization.text("menu.home.settings"),
                               m_callbacks.onOpenSettings);
        m_buttons.emplace_back(m_localization.format("menu.home.language",
                               {std::string(languageNativeName(m_settings.language))}),
            [this]() {
            m_settings.language = nextLanguage(m_settings.language);
            m_localization.setLanguage(m_settings.language);
            if (m_callbacks.onSettingsChanged) m_callbacks.onSettingsChanged();
            rebuildButtons();
        });
        m_buttons.emplace_back(m_localization.text("menu.home.quit"), m_callbacks.onQuit);
        m_buttons.emplace_back(m_localization.text("menu.home.about"),
                               [this]() { showAbout(); });
    } else if (m_page == Page::Worlds) {
        const int visible=m_visibleWorlds;
        const int end = std::min(static_cast<int>(m_worlds.size()), m_worldOffset + visible);
        for (int index = m_worldOffset; index < end; ++index) {
            const auto& world = m_worlds[static_cast<size_t>(index)];
            const std::string mode = m_localization.text(
                world.mode == GameMode::Survival ? "common.survival" :
                world.mode == GameMode::Creative ? "common.creative" : "common.spectator");
            m_buttons.emplace_back(world.displayName, [this, index]() {
                    m_selectedWorld = index;
                    rebuildButtons();
                });
            m_buttons.back().setDetail(world.compatible ? mode + "  ·  " +
                std::to_string(world.seed) : m_localization.format("menu.worlds.incompatible", {
                    std::to_string(world.generationVersion)}));
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
            m_deleteButtons.back().setDanger(true);
        }
        m_buttons.emplace_back(m_localization.text("menu.worlds.refresh"),
                               [this]() { refreshWorlds(); });
        m_buttons.emplace_back(m_localization.text("menu.worlds.play"), [this]() {
            if (m_selectedWorld >= 0 && m_selectedWorld < static_cast<int>(m_worlds.size()) &&
                m_worlds[static_cast<size_t>(m_selectedWorld)].compatible)
                m_callbacks.onOpenWorld(m_worlds[static_cast<size_t>(m_selectedWorld)].id);
        });
        m_buttons.back().setEnabled(m_selectedWorld>=0 &&
            m_selectedWorld<static_cast<int>(m_worlds.size()) &&
            m_worlds[static_cast<size_t>(m_selectedWorld)].compatible);
        m_buttons.emplace_back(m_localization.text("menu.worlds.create"),
                               [this]() { showCreate(); });
        m_buttons.emplace_back(m_localization.text("common.back"), [this]() { showHome(); });
    } else if (m_page == Page::Create) {
        m_buttons.emplace_back(m_localization.text("menu.create.world_name"),
                               [this]() { selectField(Field::Name); });
        m_buttons.back().setDetail(m_worldName.text());
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
        m_buttons.emplace_back(m_localization.text("menu.create.seed"),
                               [this]() { selectField(Field::Seed); });
        m_buttons.back().setDetail(m_seedText.text().empty()?
            m_localization.text("menu.create.random"):m_seedText.text());
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
    } else {
        m_buttons.emplace_back("SafeFain/MinecraftC", [this]() {
            if (m_callbacks.onOpenUrl)
                m_callbacks.onOpenUrl("https://github.com/SafeFain/MinecraftC");
        });
        const int end = std::min(CREDIT_COUNT, (m_aboutPage + 1) * CREDITS_PER_PAGE);
        for (int index = m_aboutPage * CREDITS_PER_PAGE; index < end; ++index) {
            const auto& credit = THIRD_PARTY_CREDITS[index];
            const std::string repository = credit.repository;
            m_buttons.emplace_back(std::string(credit.name) + " | " + credit.license,
                [this, repository]() {
                    if (m_callbacks.onOpenUrl) m_callbacks.onOpenUrl(repository);
                });
            m_buttons.back().setDetail(repository);
        }
        m_buttons.emplace_back(m_localization.text("menu.about.previous"),
                               [this]() { changeAboutPage(-1); });
        m_buttons.emplace_back(m_localization.text("menu.about.next"),
                               [this]() { changeAboutPage(1); });
        m_buttons.emplace_back(m_localization.text("common.back"),
                               [this]() { showHome(); });
    }
    m_selectedIdx=m_page==Page::Worlds && m_selectedWorld>=m_worldOffset &&
        m_selectedWorld<m_worldOffset+m_visibleWorlds?m_selectedWorld-m_worldOffset:0;
    if (!m_buttons.empty()) {
        m_buttons[static_cast<size_t>(m_selectedIdx)].setSelected(true);
        if (m_page==Page::Home) m_buttons[0].setPrimary(true);
        if (m_page==Page::Create) m_buttons[5].setPrimary(true);
        if (m_page==Page::Worlds) m_buttons[m_deleteButtons.size()+1].setPrimary(true);
    }
}

void MainMenu::selectField(Field field) {
    m_field = field;
    rebuildButtons();
    m_selectedIdx = field == Field::Name ? 0 : 3;
    m_buttons[0].setSelected(false);
    m_buttons[m_selectedIdx].setSelected(true);
}

void MainMenu::render(UIRenderer& ui,int screenWidth,int screenHeight) {
    const float w=static_cast<float>(screenWidth),h=static_cast<float>(screenHeight);
    if (m_page==Page::Worlds) {
        const int visible=std::clamp(static_cast<int>((h-192)/46),1,6);
        if (visible!=m_visibleWorlds) {
            const int oldCards=static_cast<int>(m_deleteButtons.size()),oldFocus=m_selectedIdx;
            m_visibleWorlds=visible;
            m_worldOffset=std::clamp(m_worldOffset,0,std::max(0,static_cast<int>(m_worlds.size())-visible));
            if (m_selectedWorld>=m_worldOffset+visible) m_worldOffset=m_selectedWorld-visible+1;
            rebuildButtons();
            m_buttons[0].setSelected(false);
            m_selectedIdx=std::clamp(oldFocus>=oldCards?
                static_cast<int>(m_deleteButtons.size())+oldFocus-oldCards:oldFocus,
                0,static_cast<int>(m_buttons.size())-1);
            m_buttons[static_cast<size_t>(m_selectedIdx)].setSelected(true);
        }
    }
    UiTheme::menuBackground(ui,w,h);
    if (m_page==Page::About) { renderAbout(ui,screenWidth,screenHeight);return; }
    const bool home=m_page==Page::Home;
    const bool wideHome=home&&w>=800;
    const float panelW=std::min(home?400.0f:600.0f,w-32);
    const float panelX=wideHome?w-panelW-48:(w-panelW)*.5f;
    const float panelH=home?std::min(h-32,wideHome?280.0f:400.0f):h-32;
    const float panelY=(h-panelH)*.5f;
    UiTheme::panel(ui,panelX,panelY,panelW,panelH,UiTheme::PANEL);
    const std::string title=home?"MINECRAFTC":m_localization.text(
        m_page==Page::Worlds?"menu.worlds.title":"menu.create.title");
    const std::string subtitle=m_localization.text(home?"menu.home.subtitle":
        m_page==Page::Worlds?"menu.worlds.subtitle":"menu.create.subtitle");
    const float titleW=wideHome?panelX-64:panelW-40;
    const float titleScale=fittedTextScale(ui,title,h<400?2.0f:home?3.4f:2.4f,titleW);
    const float titleX=wideHome?40:panelX+20;
    const float titleY=wideHome?h*.60f:panelY+panelH-(h<400?20:32)-ui.measureText(title,titleScale).y;
    UiTheme::textWithShadow(ui,title,titleX,titleY,titleScale,UiTheme::TEXT);
    const float subScale=fittedTextScale(ui,subtitle,1.0f,titleW);
    const float subY=titleY-ui.measureText(subtitle,subScale).y-(h<400?8:12);
    UiTheme::textWithShadow(ui,subtitle,titleX,subY,subScale,UiTheme::TEXT_DIM);
    const float contentTop=wideHome?panelY+panelH-20:subY-(h<400?12:24);
    const float contentBottom=panelY+20;
    const float contentW=panelW-40;
    const float contentX=panelX+20;
    if (home) {
        const float gap=h<400?4:8;
        const float buttonH=std::min(48.0f,(contentTop-contentBottom-gap*3)/4);
        for (size_t i=0;i<m_buttons.size();++i) {
            if (i==4) {
                m_buttons[i].setPosition(w-116,4);m_buttons[i].setSize(100,24);
            } else {
                m_buttons[i].setPosition(contentX,contentTop-buttonH-i*(buttonH+gap));
                m_buttons[i].setSize(contentW,buttonH);
            }
            m_buttons[i].render(ui);
        }
    } else if (m_page==Page::Create) {
        const bool twoColumns=w>=440;
        const int rows=twoColumns?5:7;
        const float gap=8;
        if (m_formRows!=rows) m_formSelection=-1;
        m_formRows=rows;
        m_formVisibleRows=std::min(rows,std::max(1,static_cast<int>((contentTop-contentBottom+gap)/40)));
        const auto formRow=[&](int index){return twoColumns?(index==0?0:index<=2?1:index==3?2:index==4?3:4):index;};
        if (m_formSelection!=m_selectedIdx) {
            const int selectedRow=formRow(m_selectedIdx);
            if (selectedRow<m_formOffset) m_formOffset=selectedRow;
            if (selectedRow>=m_formOffset+m_formVisibleRows)
                m_formOffset=selectedRow-m_formVisibleRows+1;
            m_formSelection=m_selectedIdx;
        }
        m_formOffset=std::clamp(m_formOffset,0,rows-m_formVisibleRows);
        const float rowH=std::min(54.0f,(contentTop-contentBottom-gap*(m_formVisibleRows-1))/m_formVisibleRows);
        for (size_t i=0;i<m_buttons.size();++i) {
            const int row=formRow(static_cast<int>(i))-m_formOffset;
            if (row<0 || row>=m_formVisibleRows) {
                m_buttons[i].setPosition(-10000,-10000);m_buttons[i].setSize(0,0);continue;
            }
            const bool half=twoColumns&&(i==1||i==2||i>=5);
            const float bw=half?(contentW-gap)*.5f:contentW;
            const bool right=i==2||i==6;
            m_buttons[i].setPosition(contentX+(half&&right?bw+gap:0),
                                     contentTop-rowH-row*(rowH+gap));
            m_buttons[i].setSize(bw,rowH);m_buttons[i].render(ui);
        }
        if (rows>m_formVisibleRows) UiTheme::scrollBar(ui,panelX+panelW-10,contentBottom,4,
            contentTop-contentBottom,m_formOffset,m_formVisibleRows,rows);
    } else {
        const size_t cards=m_deleteButtons.size();
        const float gap=6;
        const float rowH=std::min(54.0f,
            (contentTop-contentBottom-gap*(cards+1))/(cards+2));
        const float deleteW=std::min(92.0f,contentW*.28f);
        for (size_t i=0;i<cards;++i) {
            const float y=contentTop-rowH-i*(rowH+gap);
            m_buttons[i].setPosition(contentX,y);
            m_buttons[i].setSize(contentW-deleteW-gap,rowH);
            m_buttons[i].render(ui);
            if (m_worldOffset+static_cast<int>(i)==m_selectedWorld)
                UiTheme::rounded(ui,contentX+4,y+6,2,std::max(1.0f,rowH-12),1,UiTheme::ACCENT);
            m_deleteButtons[i].setPosition(contentX+contentW-deleteW,y);
            m_deleteButtons[i].setSize(deleteW,rowH);m_deleteButtons[i].render(ui);
        }
        if (cards==0) {
            const std::string empty=m_localization.text("menu.worlds.subtitle");
            UiTheme::textWithShadow(ui,empty,contentX,contentTop-24,
                fittedTextScale(ui,empty,1,contentW),UiTheme::TEXT_DIM);
        }
        const float bw=(contentW-gap)*.5f;
        for (size_t i=cards;i<m_buttons.size();++i) {
            const size_t action=i-cards;
            m_buttons[i].setPosition(contentX+(action%2)*(bw+gap),
                                    contentBottom+(1-action/2)*(rowH+gap));
            m_buttons[i].setSize(bw,rowH);m_buttons[i].render(ui);
        }
    }
    UiTheme::textWithShadow(ui,Config::GAME_VERSION,16,8,.85f,UiTheme::TEXT_DIM);
}

void MainMenu::renderAbout(UIRenderer& ui, int screenWidth, int screenHeight) {
    // Explicit GUI scaling can leave a 320 x 180 logical canvas at 720p.
    const float layoutScale = std::clamp(screenHeight / 480.0f, 0.25f, 1.0f);
    const float panelW = std::min(720.0f, screenWidth - 24.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float contentX = panelX + 16.0f;
    const float contentW = panelW - 32.0f;
    UiTheme::panel(ui, panelX, 18.0f, panelW, screenHeight - 36.0f, UiTheme::PANEL);
    const std::string title = m_localization.text("menu.about.title");
    const float titleScale = fittedTextScale(ui, title, 3.0f * layoutScale, contentW);
    const auto titleSize = ui.measureText(title, titleScale);
    const float titleY = screenHeight - 38.0f * layoutScale - titleSize.y;
    UiTheme::textWithShadow(ui, title, (screenWidth - titleSize.x) * 0.5f,
                            titleY, titleScale, UiTheme::TEXT_TITLE);
    const std::string subtitle = m_localization.text("menu.about.subtitle");
    const float subScale = fittedTextScale(ui, subtitle, 1.2f * layoutScale, contentW);
    const auto subSize = ui.measureText(subtitle, subScale);
    const float subY = titleY - subSize.y - 12.0f * layoutScale;
    UiTheme::textWithShadow(ui, subtitle, (screenWidth - subSize.x) * 0.5f,
                            subY, subScale, UiTheme::TEXT_DIM);
    const float projectY = subY - 46.0f * layoutScale;
    m_buttons[0].setPosition(contentX, projectY);
    m_buttons[0].setSize(contentW, 32.0f * layoutScale);
    m_buttons[0].render(ui);

    const std::string heading = m_localization.format("menu.about.third_party", {
        std::to_string(m_aboutPage + 1), std::to_string(CREDIT_PAGE_COUNT)});
    const float headingScale = fittedTextScale(ui, heading, 1.2f * layoutScale, contentW);
    const auto headingSize = ui.measureText(heading, headingScale);
    const float headingY = projectY - headingSize.y - 12.0f * layoutScale;
    UiTheme::textWithShadow(ui, heading, contentX, headingY, headingScale, UiTheme::TEXT);
    UiTheme::rect(ui, contentX, headingY - 8.0f * layoutScale, contentW, 2.0f, UiTheme::ACCENT_DIM);

    const float navigationY = 24.0f;
    const float navigationH = 30.0f * layoutScale;
    const float gap = 6.0f * layoutScale;
    const float listTop = headingY - 16.0f * layoutScale;
    const float rowHeight = std::min(62.0f,
        (listTop - navigationY - navigationH - 12.0f * layoutScale) / CREDITS_PER_PAGE - gap);
    const size_t rows = m_buttons.size() - 4; // Project, previous, next, back.
    for (size_t i = 0; i < rows; ++i) {
        auto& button = m_buttons[i + 1];
        button.setPosition(contentX, listTop - (i + 1) * (rowHeight + gap));
        button.setSize(contentW, rowHeight);
        button.render(ui);
    }
    const float navigationW = (contentW - 2.0f * gap) / 3.0f;
    for (size_t i = 0; i < 3; ++i) {
        auto& button = m_buttons[rows + 1 + i];
        button.setPosition(contentX + i * (navigationW + gap), navigationY);
        button.setSize(navigationW, navigationH);
        button.render(ui);
    }
    UiTheme::textWithShadow(ui, Config::GAME_VERSION, 8.0f, 8.0f, 1.0f,
                            glm::vec3(0.62f, 0.62f, 0.66f));
}

void MainMenu::onKeyPress(int key, int mods) {
    if (m_page == Page::About && (key == Key::Left || key == Key::Right)) {
        changeAboutPage(key == Key::Left ? -1 : 1);
        return;
    }
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
        else if (m_page == Page::Worlds || m_page == Page::About) showHome();
        return;
    }
    if (m_page==Page::Worlds && (key==Key::Right || key==Key::Left)) {
        onScroll(key==Key::Right?-1:1);return;
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
            const int visibleWorlds = std::min(m_visibleWorlds, static_cast<int>(m_worlds.size()) - m_worldOffset);
            if (m_page == Page::Worlds && captured < visibleWorlds) {
                const int worldIndex = m_worldOffset + captured;
                const double now = RuntimeClock::seconds(RuntimeClock{}.now());
                if (m_lastWorldIndex == worldIndex && m_lastWorldClick >= 0.0 &&
                    now - m_lastWorldClick <= 0.35) {
                    if (m_worlds[static_cast<size_t>(worldIndex)].compatible)
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
    if (m_page == Page::About) {
        if (yOffset != 0.0) changeAboutPage(yOffset < 0.0 ? 1 : -1);
        return;
    }
    if (m_page==Page::Create && yOffset!=0) {
        m_formOffset=std::clamp(m_formOffset+(yOffset<0?1:-1),0,m_formRows-m_formVisibleRows);
        return;
    }
    if (m_page != Page::Worlds || m_worlds.size() <= static_cast<size_t>(m_visibleWorlds)) return;
    const int maximum = std::max(0, static_cast<int>(m_worlds.size()) - m_visibleWorlds);
    m_worldOffset = std::clamp(m_worldOffset + (yOffset < 0 ? 1 : -1), 0, maximum);
    rebuildButtons();
}

namespace {
void renderActionMenu(UIRenderer& ui,int width,int height,const std::string& title,
                      std::vector<Button>& buttons) {
    const float w=std::min(360.0f,width-32.0f);
    const float gap=8;
    const float bh=std::min(44.0f,(height-120.0f)/std::max<size_t>(1,buttons.size())-gap);
    const float ph=80+buttons.size()*(bh+gap);
    const float px=(width-w)*.5f,py=(height-ph)*.5f;
    UiTheme::panel(ui,px,py,w,ph);
    const float scale=fittedTextScale(ui,title,2.4f,w-40);
    UiTheme::textWithShadow(ui,title,px+20,py+ph-48,scale,UiTheme::TEXT);
    for (size_t i=0;i<buttons.size();++i) {
        buttons[i].setPosition(px+20,py+ph-72-bh-i*(bh+gap));
        buttons[i].setSize(w-40,bh);buttons[i].setPrimary(i==0);buttons[i].render(ui);
    }
}
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

void PauseMenu::render(UIRenderer& ui,int screenWidth,int screenHeight) {
    ui.drawRect(0,0,screenWidth,screenHeight,UiTheme::OVERLAY);
    renderActionMenu(ui,screenWidth,screenHeight,
                     ui.localization().text("menu.pause.title"),m_buttons);
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

void SleepMenu::render(UIRenderer& ui,int screenWidth,int screenHeight) {
    ui.drawRect(0,0,screenWidth,screenHeight,UiTheme::OVERLAY);
    renderActionMenu(ui,screenWidth,screenHeight,
                     ui.localization().text("sleep.title"),m_buttons);
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
