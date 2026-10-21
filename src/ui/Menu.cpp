#include "ui/Menu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "plugins/ContentRegistry.h"

#include "core/Window.h"
#include "core/RuntimeClock.h"
#include <algorithm>
#include <stdexcept>
#include "game/Utf8.h"
#include "game/ArabicShaper.h"
#include "game/TextWrap.h"

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
    {"mjansson/mdns", "https://github.com/mjansson/mdns", "Unlicense / public domain"},
};
constexpr int CREDIT_COUNT = sizeof(THIRD_PARTY_CREDITS) / sizeof(ThirdPartyCredit);

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
    if (m_enabled) {
        if (m_pressed && m_hovered) state = UiTheme::WidgetState::Pressed;
        else if (m_selected && m_focusVisible) state = UiTheme::WidgetState::Selected;
        else if (m_hovered) state = UiTheme::WidgetState::Hover;
    }
    const float alpha=m_enabled?1.0f:.45f;
    if (m_detail.empty()) {
        UiTheme::button(ui, m_x, m_y, m_w, m_h, m_label, state, m_danger,0,alpha,-1,-1,m_primary,m_bottomInset,&m_feedback);
        return;
    }
    UiTheme::button(ui, m_x, m_y, m_w, m_h, "", state, m_danger,0,alpha,-1,-1,m_primary,m_bottomInset,&m_feedback);
    const float padding = std::min(5.0f, m_h * 0.12f);
    const float scale = std::min(1.4f, (m_h - 2.0f * padding - 2.0f) / 28.0f);
    const auto color = glm::mix(UiTheme::TEXT,UiTheme::TEXT_HOVER,m_feedback.hoverAmount());
    const float labelScale = fittedTextScale(ui, m_label, scale, m_w - 20.0f);
    const float detailScale = fittedTextScale(ui, m_detail, scale * 0.85f, m_w - 20.0f);
    UiTheme::textWithShadow(ui, m_label, m_x + 10.0f, m_y + m_h * 0.5f + 1.0f,
                            labelScale,color,alpha);
    UiTheme::textWithShadow(ui, m_detail, m_x + 10.0f, m_y + padding,
                            detailScale,UiTheme::TEXT_DIM,alpha);
}

void Button::activate() {
    if (!m_enabled || !m_onClick) return;
    m_feedback.activate();
    // Menu callbacks can rebuild the vector or close the menu itself.
    const auto callback = m_onClick;
    callback();
}

// ── Menu base ─────────────────────────────────────────────────────────────

void Menu::prepareButton(Button& button) {
    button.setFocusVisible(!m_pointerFocus);
    button.setHovered(m_pointerFocus && button.isEnabled() &&
        button.containsPoint(static_cast<float>(m_pointer.x),static_cast<float>(m_pointer.y)));
}

void Menu::renderFooter(UIRenderer& ui, Button& button, int width, int height) {
    const UiMenuFooter footer(static_cast<float>(width), static_cast<float>(height));
    button.setPosition(footer.x, footer.y);
    button.setSize(footer.width, footer.height);
    prepareButton(button);
    button.render(ui);
}

void Menu::cancelButtons(std::vector<Button>& buttons) {
    for (auto& button : buttons) button.setPressed(false);
}

void Menu::navigateUp(std::vector<Button>& buttons, int& selectedIdx) {
    navigationFocus();
    if (buttons.empty()) return;
    buttons[selectedIdx].setSelected(false);
    selectedIdx = (selectedIdx - 1 + static_cast<int>(buttons.size())) % static_cast<int>(buttons.size());
    buttons[selectedIdx].setSelected(true);
}

void Menu::navigateDown(std::vector<Button>& buttons, int& selectedIdx) {
    navigationFocus();
    if (buttons.empty()) return;
    buttons[selectedIdx].setSelected(false);
    selectedIdx = (selectedIdx + 1) % static_cast<int>(buttons.size());
    buttons[selectedIdx].setSelected(true);
}

void Menu::activateSelected(std::vector<Button>& buttons, int selectedIdx) {
    navigationFocus();
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
    m_clipboard=clipboard;
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
    m_generation.clear();m_parameters.clear();
    for(const auto& entry:Plugins::content().extensions.dimensions) {
        m_generation[entry.first]=entry.second.generation;
        auto& buffer=m_parameters.try_emplace(entry.first,entry.second.generation.parameters,4096).first->second;
        buffer.setClipboard(m_clipboard);
    }
    m_createCheats = false;
    m_field = Field::Name;
    rebuildButtons();
}

void MainMenu::showLanguage() {
    resetTransition();
    m_page = Page::Language;
    m_formOffset = 0;
    m_formSelection = -1;
    rebuildButtons();
}

void MainMenu::showAbout() {
    resetTransition();
    m_page = Page::About;
    m_aboutPage = 0;
    rebuildButtons();
}

void MainMenu::changeAboutPage(int delta) {
    const int pages = (CREDIT_COUNT + m_creditsPerPage - 1) / m_creditsPerPage;
    const int page = std::clamp(m_aboutPage + delta, 0, pages - 1);
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
    const bool preserve = m_page == m_buttonPage;
    m_buttonPage = m_page;
    auto previous = std::move(m_buttons);
    m_buttons.clear();
    m_pressedButton = -1;
    m_pressedDeleteButton = -1;
    m_deleteButtons.clear();
    if (m_page == Page::Home) {
        m_buttons.emplace_back(m_localization.text("menu.home.singleplayer"),
                               [this]() { showWorlds(); });
        m_buttons.emplace_back(m_localization.text("menu.home.settings"),
                               m_callbacks.onOpenSettings);
        if (m_callbacks.onOpenLanJoin) m_buttons.emplace_back(m_localization.text("lan.join_title"), m_callbacks.onOpenLanJoin);
        m_buttons.emplace_back(m_localization.format("menu.home.language",
                               {std::string(languageNativeName(m_settings.language))}),
                               [this]() { showLanguage(); });
        if(m_callbacks.onOpenPlugins) m_buttons.emplace_back(m_localization.text("plugins.title"),m_callbacks.onOpenPlugins);
        m_buttons.emplace_back(m_localization.text("menu.home.quit"), m_callbacks.onQuit);
        m_buttons.emplace_back(m_localization.text("menu.home.about"),
                               [this]() { showAbout(); });
    } else if (m_page == Page::Language) {
        for (const auto language : languagesByEnglishName()) {
            m_buttons.emplace_back(language == Language::Arabic?
                shapeArabic(std::string(languageNativeName(language))):
                std::string(languageNativeName(language)), [this, language]() {
                if (m_settings.language == language) return;
                m_settings.language = language;
                m_localization.setLanguage(language);
                if (m_callbacks.onSettingsChanged) m_callbacks.onSettingsChanged();
                rebuildButtons();
            });
            m_buttons.back().setPrimary(language == m_settings.language);
        }
        m_buttons.emplace_back(m_localization.text("common.back"), [this]() { showHome(); });
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
            if(world.pluginCompatibility.recoverable())m_buttons.back().setDetail(m_localization.text("menu.plugins.title"));
            else if(!world.incompatibilityReason.empty())m_buttons.back().setDetail(world.incompatibilityReason);
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
        m_buttons.emplace_back(m_localization.text("menu.worlds.play"),[this]() {requestOpenWorld(m_selectedWorld);});
        m_buttons.back().setEnabled(m_selectedWorld>=0 && m_selectedWorld<static_cast<int>(m_worlds.size()) &&
            (m_worlds[static_cast<size_t>(m_selectedWorld)].compatible ||
             m_worlds[static_cast<size_t>(m_selectedWorld)].pluginCompatibility.recoverable()));
        m_buttons.emplace_back(m_localization.text("menu.worlds.create"),
                               [this]() { showCreate(); });
        m_buttons.emplace_back(m_localization.text("common.back"), [this]() { showHome(); });
    } else if (m_page==Page::PluginWarning || m_page==Page::PluginRecovery || m_page==Page::PluginResult) {
        m_buttons.emplace_back(m_localization.text(m_page==Page::PluginResult?"common.back":"common.cancel"),[this]() {
            if(m_recovery)m_recovery->cancel();
            m_recovery.reset();refreshWorlds();showWorlds();
        });
        if(m_page==Page::PluginWarning) {
            m_buttons.emplace_back(m_localization.text("menu.plugins.force"),[this]() {beginRecovery(true);});
            m_buttons.back().setDanger(true);
            m_buttons.emplace_back(m_localization.text("menu.plugins.force_no_backup"),[this]() {beginRecovery(false);});
            m_buttons.back().setDanger(true);
        }else if(m_page==Page::PluginResult && m_recoverySucceeded) {
            m_buttons.emplace_back(m_localization.text("menu.worlds.play"),[this]() {
                const auto callback=m_callbacks.onOpenWorld;const auto id=m_pendingPluginWorld.id;
                if(callback)callback(id);
            });
        }
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
        const bool extensions=!Plugins::content().extensions.generators.empty()||!Plugins::content().extensions.modifiers.empty();
        m_parameterRows.clear();
        if(extensions)for(const auto& entry:Plugins::content().extensions.dimensions) {
            const uint32_t dimension=entry.first;
            const auto& selected=m_generation[dimension];
            std::string name=selected.generator.empty()?m_localization.text("common.normal"):Plugins::content().extensions.generators.at(selected.generator).name;
            m_buttons.emplace_back(m_localization.format("menu.create.generator_dimension",{m_localization.dimensionName(dimension),name}),[this,dimension]{
                std::vector<std::string> keys;if(dimension==0)keys.emplace_back();for(const auto& g:Plugins::content().extensions.generators)if(g.second.dimension==dimension)keys.push_back(g.first);
                auto& g=m_generation[dimension];auto it=std::find(keys.begin(),keys.end(),g.generator);g.generator=keys[(static_cast<size_t>(it-keys.begin())+1)%keys.size()];
                g.revision=g.generator.empty()?"":Plugins::content().extensions.generators.at(g.generator).revision;rebuildButtons();});
            m_parameterRows[dimension]=static_cast<int>(m_buttons.size());
            m_buttons.emplace_back(m_localization.text("menu.create.generator_parameters"),[this,dimension]{m_parameterDimension=dimension;selectField(Field::Parameters);});
            m_buttons.back().setDetail(m_parameters[dimension].text());
            for(const auto& m:Plugins::content().extensions.modifiers)if(m.second.dimension==dimension) {
                const auto key=m.first;const bool active=std::any_of(selected.modifiers.begin(),selected.modifiers.end(),[&](const auto& value){return value.first==key;});
                m_buttons.emplace_back(key+": "+m_localization.text(active?"common.on":"common.off"),[this,dimension,key]{auto& entries=m_generation[dimension].modifiers;
                    auto it=std::find_if(entries.begin(),entries.end(),[&](const auto& value){return value.first==key;});
                    if(it==entries.end())entries.emplace_back(key,Plugins::content().extensions.modifiers.at(key).revision);else entries.erase(it);rebuildButtons();});
            }
        }
        m_buttons.emplace_back(m_localization.text("menu.create.confirm"), [this]() {
            for(auto& entry:m_generation)entry.second.parameters=m_parameters.at(entry.first).text();
            if(m_callbacks.onGenerationConfig)m_callbacks.onGenerationConfig(m_generation);
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
        const int end = std::min(CREDIT_COUNT, (m_aboutPage + 1) * m_creditsPerPage);
        for (int index = m_aboutPage * m_creditsPerPage; index < end; ++index) {
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
    if (preserve && previous.size()==m_buttons.size())
        for (size_t i=0;i<m_buttons.size();++i)
            if (m_page==Page::Create || previous[i].label()==m_buttons[i].label())
                m_buttons[i].inheritFeedback(previous[i]);
    m_selectedIdx=m_page==Page::Worlds && m_selectedWorld>=m_worldOffset &&
        m_selectedWorld<m_worldOffset+m_visibleWorlds?m_selectedWorld-m_worldOffset:0;
    if (m_page == Page::Language) {
        const auto& languages = languagesByEnglishName();
        m_selectedIdx = static_cast<int>(std::find(languages.begin(), languages.end(),
                                                 m_settings.language) - languages.begin());
        m_selectedIdx = std::clamp(m_selectedIdx, 0, static_cast<int>(languages.size()) - 1);
    }
    if (!m_buttons.empty()) {
        m_buttons[static_cast<size_t>(m_selectedIdx)].setSelected(true);
        if (m_page==Page::Home) m_buttons[0].setPrimary(true);
        if (m_page==Page::Create) m_buttons[5].setPrimary(true);
        if (m_page==Page::Worlds) m_buttons[m_deleteButtons.size()+1].setPrimary(true);
    }
}

void MainMenu::requestOpenWorld(int index) {
    if(index<0 || index>=static_cast<int>(m_worlds.size()))return;
    const auto world=m_worlds[static_cast<size_t>(index)];
    if(world.compatible){if(m_callbacks.onOpenWorld)m_callbacks.onOpenWorld(world.id);return;}
    if(!world.pluginCompatibility.recoverable() || !m_callbacks.onBeginWorldRecovery)return;
    m_pendingPluginWorld=world;m_page=Page::PluginWarning;m_warningOffset=0;
    m_lastWorldClick=-1;m_lastWorldIndex=-1;rebuildButtons();
}

void MainMenu::beginRecovery(bool keepBackup) {
    if(m_page!=Page::PluginWarning)return;
    try {
        m_recovery=m_callbacks.onBeginWorldRecovery(m_pendingPluginWorld.id,m_pendingPluginWorld.pluginCompatibility.token,keepBackup);
        if(!m_recovery)throw std::runtime_error("Recovery could not start");
        m_recoveryMessage.clear();m_recoverySucceeded=false;m_page=Page::PluginRecovery;
    }catch(const std::exception& error){m_recoveryMessage=error.what();m_recoverySucceeded=false;m_page=Page::PluginResult;}
    m_warningOffset=0;rebuildButtons();
}

void MainMenu::tick(float dt) {
    Menu::tick(dt);
    if(m_page!=Page::PluginRecovery || !m_recovery)return;
    m_recovery->step();
    if(!m_recovery->done())return;
    m_recoverySucceeded=m_recovery->succeeded();
    m_recoveryMessage=m_recoverySucceeded?m_recovery->backupPath().u8string():m_recovery->error();
    m_page=Page::PluginResult;m_warningOffset=0;rebuildButtons();
}

void MainMenu::renderPluginWarning(UIRenderer& ui,int width,int height) {
    const float w=static_cast<float>(width),h=static_cast<float>(height);
    const float x=16,y=16,panelW=std::max(1.0f,w-32),panelH=std::max(1.0f,h-32),contentW=std::max(1.0f,panelW-32);
    UiTheme::panel(ui,x,y,panelW,panelH,UiTheme::PANEL);
    const std::string title=m_localization.text(m_page==Page::PluginWarning?"menu.plugins.title":m_page==Page::PluginRecovery?"menu.plugins.progress":m_recoverySucceeded?"menu.plugins.success":"menu.plugins.failed");
    const float titleScale=fittedTextScale(ui,title,1.5f,contentW);
    UiTheme::textWithShadow(ui,title,x+16,h-48,titleScale,UiTheme::TEXT);
    std::string text=m_pendingPluginWorld.displayName+"\n";
    if(m_page==Page::PluginWarning) {
        text+=m_localization.text("menu.plugins.rules")+"\n"+m_localization.format("menu.plugins.backup",{m_pendingPluginWorld.backupDirectory})+"\n\n";
        for(const auto& issue:m_pendingPluginWorld.pluginCompatibility.issues) {
            text+=m_localization.text("menu.plugins."+issue.kind)+": "+issue.key+"\n";
            text+=m_localization.format("menu.plugins.values",{issue.saved.empty()?"-":issue.saved,issue.current.empty()?"-":issue.current})+"\n\n";
        }
    }else if(m_page==Page::PluginRecovery)text+=std::to_string(static_cast<int>(m_recovery->progress()*100))+"%\n"+m_localization.text("menu.plugins.wait");
    else if(!m_recoverySucceeded)text+=m_recoveryMessage;
    else if(m_recoveryMessage.empty())text+=m_localization.text("menu.plugins.no_backup");
    else text+=m_localization.format(m_recovery && m_recovery->backupCleanupPending()?
        "menu.plugins.cleanup_pending":"menu.plugins.backup",{m_recoveryMessage});
    const bool stacked=w<720;
    const UiMenuFooter footer(width,height);
    const size_t actionCount=m_buttons.size()-1;
    const float buttonArea=actionCount==0?0:stacked?static_cast<float>(actionCount)*38-6:32;
    const float scale=1.0f,lineH=18.0f;
    const auto lines=wrapTextPixels(text,contentW,[&](const std::string& line){return ui.measureText(line,scale).x;});
    m_warningLines=static_cast<int>(lines.size());m_warningVisible=std::max(1,static_cast<int>((h-90-footer.contentBottom-buttonArea)/lineH));
    m_warningOffset=std::clamp(m_warningOffset,0,std::max(0,m_warningLines-m_warningVisible));
    for(int i=0;i<m_warningVisible && i+m_warningOffset<m_warningLines;++i)
        UiTheme::textWithShadow(ui,lines[static_cast<size_t>(i+m_warningOffset)],x+16,h-78-i*lineH,scale,UiTheme::TEXT_DIM);
    const float gap=6,buttonW=stacked?contentW:(contentW-gap*static_cast<float>(actionCount>0?actionCount-1:0))/std::max<size_t>(1,actionCount);
    for(size_t i=1;i<m_buttons.size();++i) {
        auto& button=m_buttons[i];
        button.setPosition(x+16+(stacked?0:static_cast<float>(i-1)*(buttonW+gap)),
            footer.contentBottom+(stacked?static_cast<float>(m_buttons.size()-1-i)*38:0));
        button.setSize(buttonW,32);prepareButton(button);button.render(ui);
    }
    renderFooter(ui,m_buttons[0],width,height);
    if(m_warningLines>m_warningVisible)UiTheme::scrollBar(ui,w-23,footer.contentBottom+buttonArea+8,4,std::max(1.0f,h-90-footer.contentBottom-buttonArea),m_warningOffset,m_warningVisible,m_warningLines);
}

void MainMenu::selectField(Field field) {
    m_field = field;
    rebuildButtons();
    m_selectedIdx = field==Field::Name?0:field==Field::Seed?3:m_parameterRows.at(m_parameterDimension);
    if(static_cast<size_t>(m_selectedIdx)>=m_buttons.size())m_selectedIdx=0;
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
    if (m_page==Page::PluginWarning || m_page==Page::PluginRecovery || m_page==Page::PluginResult) {renderPluginWarning(ui,screenWidth,screenHeight);return;}
    const bool home=m_page==Page::Home;
    const bool wideHome=home&&w>=800;
    const float panelW=std::min(home?400.0f:600.0f,w-32);
    const float panelX=wideHome?w-panelW-48:(w-panelW)*.5f;
    const float panelH=home?std::min(h-32,wideHome?280.0f:400.0f):h-32;
    const float panelY=(h-panelH)*.5f;
    UiTheme::panel(ui,panelX,panelY,panelW,panelH,UiTheme::PANEL);
    const std::string title=home?"MINECRAFTC":m_localization.text(
        m_page==Page::Language?"menu.language.title":
        m_page==Page::Worlds?"menu.worlds.title":"menu.create.title");
    const std::string subtitle=m_page==Page::Language?
        m_localization.format("menu.home.language", {std::string(languageNativeName(m_settings.language))}):
        m_localization.text(home?"menu.home.subtitle":
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
    const float contentBottom=home?panelY+20:std::max(panelY+20, UiMenuFooter(w,h).contentBottom);
    const float contentW=panelW-40;
    const float contentX=panelX+20;
    if (home) {
        const float gap=h<400?4:8;
        const float rows=static_cast<float>(m_buttons.size()-1);
        const float buttonH=std::min(48.0f,(contentTop-contentBottom-gap*(rows-1))/rows);
        for (size_t i=0;i<m_buttons.size();++i) {
            if (i+1==m_buttons.size()) {
                m_buttons[i].setPosition(w-116,4);m_buttons[i].setSize(100,24);
            } else {
                m_buttons[i].setPosition(contentX,contentTop-buttonH-i*(buttonH+gap));
                m_buttons[i].setSize(contentW,buttonH);
            }
            prepareButton(m_buttons[i]);m_buttons[i].render(ui);
        }
    } else if (m_page==Page::Create || m_page==Page::Language) {
        const bool languagePage=m_page==Page::Language;
        const bool twoColumns=!languagePage && w>=440 && m_buttons.size()==7;
        const int rows=twoColumns?5:static_cast<int>(m_buttons.size())-1;
        const float gap=8;
        if (m_formRows!=rows) m_formSelection=-1;
        m_formRows=rows;
        m_formVisibleRows=std::min(rows,std::max(1,static_cast<int>((contentTop-contentBottom+gap)/40)));
        const auto formRow=[&](int index){return twoColumns?(index==0?0:index<=2?1:index==3?2:index==4?3:4):index;};
        if (m_formSelection!=m_selectedIdx && m_selectedIdx+1!=static_cast<int>(m_buttons.size())) {
            const int selectedRow=formRow(m_selectedIdx);
            if (selectedRow<m_formOffset) m_formOffset=selectedRow;
            if (selectedRow>=m_formOffset+m_formVisibleRows)
                m_formOffset=selectedRow-m_formVisibleRows+1;
            m_formSelection=m_selectedIdx;
        }
        m_formOffset=std::clamp(m_formOffset,0,rows-m_formVisibleRows);
        const float rowH=std::min(54.0f,(contentTop-contentBottom-gap*(m_formVisibleRows-1))/m_formVisibleRows);
        for (size_t i=0;i<m_buttons.size();++i) {
            if (i+1==m_buttons.size()) {
                renderFooter(ui,m_buttons[i],screenWidth,screenHeight);
                continue;
            }
            const int row=formRow(static_cast<int>(i))-m_formOffset;
            if (row<0 || row>=m_formVisibleRows) {
                m_buttons[i].setPosition(-10000,-10000);m_buttons[i].setSize(0,0);continue;
            }
            const bool half=twoColumns&&(i==1||i==2||i>=5);
            const float bw=half?(contentW-gap)*.5f:contentW;
            const bool right=i==2||i==6;
            m_buttons[i].setPosition(contentX+(half&&right?bw+gap:0),
                                     contentTop-rowH-row*(rowH+gap));
            m_buttons[i].setSize(bw,rowH);prepareButton(m_buttons[i]);m_buttons[i].render(ui);
            if (languagePage && i<languagesByEnglishName().size() &&
                languagesByEnglishName()[i]==m_settings.language)
                UiTheme::rounded(ui,contentX+4,m_buttons[i].y()+6,2,
                                 std::max(1.0f,rowH-12),1,UiTheme::ACCENT);
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
            prepareButton(m_buttons[i]);m_buttons[i].render(ui);
            if (m_worldOffset+static_cast<int>(i)==m_selectedWorld)
                UiTheme::rounded(ui,contentX+4,y+6,2,std::max(1.0f,rowH-12),1,UiTheme::ACCENT);
            m_deleteButtons[i].setPosition(contentX+contentW-deleteW,y);
            m_deleteButtons[i].setSize(deleteW,rowH);prepareButton(m_deleteButtons[i]);m_deleteButtons[i].render(ui);
        }
        if (cards==0) {
            const std::string empty=m_localization.text("menu.worlds.subtitle");
            UiTheme::textWithShadow(ui,empty,contentX,contentTop-24,
                fittedTextScale(ui,empty,1,contentW),UiTheme::TEXT_DIM);
        }
        const float bw=(contentW-gap)*.5f;
        for (size_t i=cards;i<m_buttons.size();++i) {
            if (i+1==m_buttons.size()) {
                renderFooter(ui,m_buttons[i],screenWidth,screenHeight);
                continue;
            }
            const size_t action=i-cards;
            m_buttons[i].setPosition(contentX+(action%2)*(bw+gap),
                                    contentBottom+(1-action/2)*(rowH+gap));
            m_buttons[i].setSize(bw,rowH);prepareButton(m_buttons[i]);m_buttons[i].render(ui);
        }
    }
    UiTheme::textWithShadow(ui,Config::GAME_VERSION,16,8,.85f,UiTheme::TEXT_DIM);
}

void MainMenu::renderAbout(UIRenderer& ui, int screenWidth, int screenHeight) {
    const int perPage = screenHeight < 300 ? 1 : 4;
    if (perPage != m_creditsPerPage) {
        m_aboutPage = m_aboutPage * m_creditsPerPage / perPage;
        m_creditsPerPage = perPage;
        rebuildButtons();
    }
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
    prepareButton(m_buttons[0]);
    m_buttons[0].render(ui);

    const std::string heading = m_localization.format("menu.about.third_party", {
        std::to_string(m_aboutPage + 1),
        std::to_string((CREDIT_COUNT + m_creditsPerPage - 1) / m_creditsPerPage)});
    const float headingScale = fittedTextScale(ui, heading, 1.2f * layoutScale, contentW);
    const auto headingSize = ui.measureText(heading, headingScale);
    const float headingY = projectY - headingSize.y - 12.0f * layoutScale;
    UiTheme::textWithShadow(ui, heading, contentX, headingY, headingScale, UiTheme::TEXT);
    UiTheme::rect(ui, contentX, headingY - 8.0f * layoutScale, contentW, 2.0f, UiTheme::ACCENT_DIM);

    const float navigationY = UiMenuFooter(screenWidth,screenHeight).contentBottom;
    const float navigationH = std::max(22.0f,30.0f * layoutScale);
    const float gap = 6.0f * layoutScale;
    const float listTop = headingY - 16.0f * layoutScale;
    const float rowHeight = std::min(62.0f,
        (listTop - navigationY - navigationH - 12.0f * layoutScale) / m_creditsPerPage - gap);
    const size_t rows = m_buttons.size() - 4; // Project, previous, next, back.
    for (size_t i = 0; i < rows; ++i) {
        auto& button = m_buttons[i + 1];
        button.setPosition(contentX, listTop - (i + 1) * (rowHeight + gap));
        button.setSize(contentW, rowHeight);
        prepareButton(button);
        button.render(ui);
    }
    const float navigationW = (contentW - gap) / 2.0f;
    for (size_t i = 0; i < 2; ++i) {
        auto& button = m_buttons[rows + 1 + i];
        button.setPosition(contentX + i * (navigationW + gap), navigationY);
        button.setSize(navigationW, navigationH);
        prepareButton(button);
        button.render(ui);
    }
    renderFooter(ui,m_buttons.back(),screenWidth,screenHeight);
    UiTheme::textWithShadow(ui, Config::GAME_VERSION, 8.0f, 8.0f, 1.0f,
                            glm::vec3(0.62f, 0.62f, 0.66f));
}

void MainMenu::onKeyPress(int key, int mods) {
    navigationFocus();
    if (key==Key::Tab && m_page!=Page::Create) {
        if (mods&KeyModifier::Shift) navigateUp(m_buttons,m_selectedIdx);
        else navigateDown(m_buttons,m_selectedIdx);
        return;
    }
    if (m_page == Page::About && (key == Key::Left || key == Key::Right)) {
        changeAboutPage(key == Key::Left ? -1 : 1);
        return;
    }
    if (m_page == Page::Create && key == Key::Tab) {
        std::vector<std::pair<Field,uint32_t>> fields{{Field::Name,0},{Field::Seed,0}};
        for(const auto& entry:m_parameterRows)fields.emplace_back(Field::Parameters,entry.first);
        size_t index=0;for(size_t i=0;i<fields.size();++i)if(fields[i].first==m_field&&(m_field!=Field::Parameters||fields[i].second==m_parameterDimension))index=i;
        index=(index+((mods&KeyModifier::Shift)?fields.size()-1:1))%fields.size();
        m_parameterDimension=fields[index].second;selectField(fields[index].first);
        return;
    }
    if (m_page == Page::Create && key == Key::Backspace) {
        TextEditBuffer& value = fieldBuffer();
        value.backspace();
        rebuildButtons();
        selectField(m_field);
        return;
    }
    if (m_page == Page::Create) {
        TextEditBuffer& value = fieldBuffer();
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
    if((m_page==Page::PluginWarning || m_page==Page::PluginResult) && (key==Key::Right || key==Key::Left)) {
        onScroll((key==Key::Right)?-1:1);return;
    }
    if (key == Key::Escape) {
        if(m_page==Page::PluginWarning || m_page==Page::PluginRecovery || m_page==Page::PluginResult){m_buttons[0].activate();return;}
        if (m_page == Page::Create) showWorlds();
        else if (m_page == Page::Worlds || m_page == Page::About || m_page == Page::Language) showHome();
        return;
    }
    if (m_page==Page::Worlds && (key==Key::Right || key==Key::Left)) {
        onScroll(key==Key::Right?-1:1);return;
    }
    if (m_page == Page::Worlds && key == Key::Enter && m_selectedWorld >= 0 &&
        m_selectedIdx == m_selectedWorld - m_worldOffset) {
        requestOpenWorld(m_selectedWorld);
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
    TextEditBuffer& value = fieldBuffer();
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
    pointerFocus(x,y);
    for (auto& button : m_buttons) prepareButton(button);
    for (auto& button : m_deleteButtons) prepareButton(button);
}

void MainMenu::onMouseButton(int button, ButtonAction action, double x, double y) {
    onMouseMove(x,y);
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
            if (m_buttons[i].isEnabled() && m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
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
                    requestOpenWorld(worldIndex);
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
    if(m_page==Page::PluginWarning || m_page==Page::PluginRecovery || m_page==Page::PluginResult) {
        m_warningOffset=std::clamp(m_warningOffset+(yOffset<0?3:yOffset>0?-3:0),0,std::max(0,m_warningLines-m_warningVisible));return;
    }
    if (m_page == Page::About) {
        if (yOffset != 0.0) changeAboutPage(yOffset < 0.0 ? 1 : -1);
        return;
    }
    if ((m_page==Page::Create || m_page==Page::Language) && yOffset!=0) {
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
                      std::vector<Button>& buttons,int footerIndex,int secondFooter=-1) {
    const UiMenuFooter footer(width,height);
    const size_t bodyCount=buttons.size()-1-(secondFooter>=0?1:0);
    const float w=std::min(360.0f,width-32.0f);
    const float gap=8;
    const float bh=std::min(44.0f,(height-footer.contentBottom-76.0f)/std::max<size_t>(1,bodyCount)-gap);
    const float ph=72+bodyCount*(bh+gap);
    const float px=(width-w)*.5f,py=footer.contentBottom+(height-footer.contentBottom-ph)*.5f;
    UiTheme::panel(ui,px,py,w,ph);
    const float scale=fittedTextScale(ui,title,2.4f,w-40);
    UiTheme::textWithShadow(ui,title,px+20,py+ph-48,scale,UiTheme::TEXT);
    size_t row=0;
    for (size_t i=0;i<buttons.size();++i) {
        if (static_cast<int>(i)==footerIndex || static_cast<int>(i)==secondFooter) continue;
        buttons[i].setPosition(px+20,py+ph-64-bh-row++*(bh+gap));
        buttons[i].setSize(w-40,bh);buttons[i].setPrimary(i==0);buttons[i].render(ui);
    }
    const float footerW=secondFooter>=0?std::min(600.0f,width-32.0f):footer.width;
    const float bw=secondFooter>=0?(footerW-gap)*.5f:footerW;
    auto& back=buttons[static_cast<size_t>(footerIndex)];
    back.setPosition((width-footerW)*.5f,footer.y);back.setSize(bw,footer.height);back.render(ui);
    if (secondFooter>=0) {
        auto& other=buttons[static_cast<size_t>(secondFooter)];
        other.setPosition((width-footerW)*.5f+bw+gap,footer.y);other.setSize(bw,footer.height);other.render(ui);
    }
}
}

// ── Pause Menu ────────────────────────────────────────────────────────────

PauseMenu::PauseMenu(
    const MenuCallbacks& callbacks, const Localization& localization) {
    m_buttons.emplace_back(localization.text("menu.pause.resume"), callbacks.onResume);
    m_buttons.emplace_back(localization.text("menu.home.settings"), callbacks.onOpenSettings);
    if (callbacks.onOpenLanHost && (!callbacks.canHostLan || callbacks.canHostLan()))
        m_buttons.emplace_back(localization.text("lan.host_title"), callbacks.onOpenLanHost);
    m_buttons.emplace_back(localization.text("menu.pause.back"), callbacks.onBackToMenu);
    m_buttons.emplace_back(localization.text("menu.home.quit"), callbacks.onQuit);

    if (!m_buttons.empty()) {
        m_buttons[0].setSelected(true);
    }
}

void PauseMenu::render(UIRenderer& ui,int screenWidth,int screenHeight) {
    ui.drawRect(0,0,screenWidth,screenHeight,UiTheme::OVERLAY);
    for (auto& button : m_buttons) prepareButton(button);
    renderActionMenu(ui,screenWidth,screenHeight,
                     ui.localization().text("menu.pause.title"),m_buttons,0,static_cast<int>(m_buttons.size())-2);
}

void PauseMenu::onKeyPress(int key, int mods) {
    navigationFocus();
    if (key==Key::Tab) {
        if (mods&KeyModifier::Shift) navigateUp(m_buttons,m_selectedIdx);
        else navigateDown(m_buttons,m_selectedIdx);
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
    pointerFocus(x,y);
    for (auto& button : m_buttons) prepareButton(button);
}

void PauseMenu::onMouseButton(int button, ButtonAction action, double x, double y) {
    onMouseMove(x,y);
    if (button != MouseButton::Left) return;
    if (action == ButtonAction::Press) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i].isEnabled() && m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
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
    DimensionId dimension) {
    m_buttons.emplace_back(localization.text("sleep.until_morning"),
        [callbacks]() { if (callbacks.onSleepAction) callbacks.onSleepAction(0); });
    m_buttons.emplace_back(localization.text("sleep.leave_bed"),
        [callbacks]() { if (callbacks.onSleepAction) callbacks.onSleepAction(1); });
    for(const auto& entry:Plugins::content().extensions.bedTravel) {
        const auto& travel=entry.second;
        if(travel.source!=static_cast<uint32_t>(dimension)||!Plugins::validDimension(travel.target))continue;
        m_buttons.emplace_back(localization.text(travel.nameKey),[callbacks,key=travel.key]() {
            if(callbacks.onBedTravel)callbacks.onBedTravel(key);
        });
    }
    if (!m_buttons.empty()) m_buttons[0].setSelected(true);
}

void SleepMenu::render(UIRenderer& ui,int screenWidth,int screenHeight) {
    ui.drawRect(0,0,screenWidth,screenHeight,UiTheme::OVERLAY);
    for (auto& button : m_buttons) prepareButton(button);
    renderActionMenu(ui,screenWidth,screenHeight,
                     ui.localization().text("sleep.title"),m_buttons,1);
}

void SleepMenu::onKeyPress(int key, int mods) {
    navigationFocus();
    if (key==Key::Tab) {
        if (mods&KeyModifier::Shift) navigateUp(m_buttons,m_selectedIdx);
        else navigateDown(m_buttons,m_selectedIdx);
        return;
    }
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
    pointerFocus(x,y);
    for (auto& button : m_buttons) prepareButton(button);
}

void SleepMenu::onMouseButton(
    int button, ButtonAction action, double x, double y) {
    onMouseMove(x,y);
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

void MainMenu::onPointerCancel() {
    m_pressedButton = -1;
    cancelButtons(m_buttons);
    m_pressedDeleteButton = -1;
    cancelButtons(m_deleteButtons);
}

void PauseMenu::onPointerCancel() {
    m_pressedButton = -1;
    cancelButtons(m_buttons);
}

void SleepMenu::onPointerCancel() {
    m_pressedButton = -1;
    cancelButtons(m_buttons);
}
