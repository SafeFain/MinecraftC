#include "app/GameUiController.h"

#include "Config.h"
#include "ui/UIStyle.h"
#include "app/ApplicationInputController.h"
#include "app/GameSession.h"
#include "core/Window.h"
#include "game/ClientSettings.h"
#include "game/SurvivalRules.h"
#include "game/TextWrap.h"
#include "platform/Clipboard.h"
#include "player/Player.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float commandLineHeight = 25.0f;
constexpr float commandBottom = 18.0f;
}

std::vector<std::string> GameUiController::commandInputLines(
    int uiWidth, bool touchTabVisible) {
    const float width = std::max(1.0f,
        static_cast<float>(uiWidth - (touchTabVisible ? 122 : 40)));
    return wrapTextPixels("> " + commandInput.text() + "_", width,
        [&](const std::string& text) { return renderer.measureText(text, 1.25f).x; });
}

void GameUiController::updateTextInputArea(
    Window& window, const ClientSettings& settings,
    const ApplicationInputController& inputs) {
    // Calculate with the current surface size before opening the keyboard,
    // including the first frame and frames following a rotation or GUI change.
    guiScale = effectiveGuiScale(window.width(), window.height(), settings.guiScale);
    if (!commandOpen) {
        window.setTextInputArea(nullptr);
        return;
    }
    const WindowSafeArea safe = window.safeArea();
    const int uiWidth = std::max(1, safe.width / guiScale);
    const bool touchTabVisible = settings.controlMode == ControlMode::Touch ||
        (settings.controlMode == ControlMode::Auto && inputs.touchHudVisible);
    const auto lines = commandInputLines(uiWidth, touchTabVisible);
    const int capacity=std::max(1,uiTextLineCapacity(safe.height/guiScale,
        commandBottom+12,commandLineHeight));
    float height=11.0f+commandLineHeight*std::min(static_cast<int>(lines.size()),capacity);
    if (touchTabVisible) {
        const TouchRect tab = touchCommandTabRect(uiWidth, safe.height / guiScale);
        height = std::max(height, tab.y + tab.h - commandBottom);
    }
    const WindowSafeArea pixels{
        safe.x + 12 * guiScale,
        safe.y + static_cast<int>(commandBottom) * guiScale,
        std::max(1, (uiWidth - 24) * guiScale),
        static_cast<int>(std::ceil(height * guiScale))};
    const auto area = projectTextInputArea(
        pixels, window.windowWidth(), window.windowHeight(),
        window.width(), window.height());
    window.setTextInputArea(&area);
}

void GameUiController::render(
    const GameSession& session, const ClientSettings& settings,
    ApplicationInputController& inputs, Window& window, GameState state,
    bool showCrosshair) {
    // ── UI Rendering ──────────────────────────────────────────
    const int fbWidth=window.width(),fbHeight=window.height();
    const WindowSafeArea safe = window.safeArea();
    guiScale = effectiveGuiScale(fbWidth, fbHeight, settings.guiScale);
    const int uiWidth = std::max(1, safe.width / guiScale);
    const int uiHeight = std::max(1, safe.height / guiScale);
    renderer.setCanvas(
        static_cast<float>(safe.x) / guiScale,
        static_cast<float>(safe.y) / guiScale,
        static_cast<float>(fbWidth) / guiScale,
        static_cast<float>(fbHeight) / guiScale);

    // Phase 1: Inventory overlay (on top of 3D world)
    if (inventoryOpen) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        renderer.setOpacity(inventoryFade.value);
        if (tradeOpen) {
            tradeScreen.render(renderer, uiWidth, uiHeight,
                static_cast<int>(mouseScreenX), static_cast<int>(mouseScreenY));
        } else if (containerOpen) {
            containerScreen.render(
                renderer, uiWidth, uiHeight, static_cast<int>(mouseScreenX),
                static_cast<int>(mouseScreenY));
        } else if (playerInventoryViewOpen(session.playerState())) {
            survivalInventory.render(renderer, uiWidth, uiHeight,
                static_cast<int>(mouseScreenX), static_cast<int>(mouseScreenY));
        } else {
            inventory.render(renderer, uiWidth, uiHeight,
                               static_cast<int>(mouseScreenX),
                               static_cast<int>(mouseScreenY));
        }
        if ((settings.controlMode == ControlMode::Touch || (settings.controlMode == ControlMode::Auto && inputs.touchHudVisible))) {
            const TouchRect close = touchInventoryCloseRect(uiWidth,uiHeight);
            UiTheme::button(renderer,close.x,close.y,close.w,close.h,
                localization.text("touch.close"),UiTheme::WidgetState::Normal,false,.9f);
        }
        renderer.endUIFrame();
    }

    renderer.setOpacity(1.0f);

    // Phase 2: Hotbar HUD (Playing, no inventory, no menu)
    if (state == GameState::Playing && !inventoryOpen && !activeMenu) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        if (!session.playerState().isSpectator()) {
            hotbar.render(renderer, uiWidth, uiHeight);
            if (session.playerState().isSurvival()) renderSurvivalHud(session.playerState(), uiWidth);
            if (showCrosshair)
                renderCrosshairAndMiningProgress(session.playerState(), uiWidth, uiHeight);
            renderAttackIndicator(session.playerState(), settings.attackIndicator,
                                  uiWidth, uiHeight);
            if (itemNameSeconds > 0.0f) renderSelectedItemName(session.playerState(), uiWidth);
        }
        if ((settings.controlMode == ControlMode::Touch || (settings.controlMode == ControlMode::Auto && inputs.touchHudVisible)))
            inputs.touchControls.render(renderer);
        renderer.endUIFrame();
    }

    if (commandOpen || chatVisibleSeconds > 0.0f) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        constexpr float lineHeight = commandLineHeight;
        const float textWidth = std::max(1.0f, static_cast<float>(uiWidth - 40));
        auto wrap = [&](const std::string& text, float scale, float width) {
            return wrapTextPixels(text, width,
                [&](const std::string& candidate) {
                    return renderer.measureText(candidate, scale).x;
                });
        };
        std::vector<std::string> inputLines;
        if (commandOpen) {
            const bool touchTabVisible =
                settings.controlMode == ControlMode::Touch ||
                (settings.controlMode == ControlMode::Auto &&
                 inputs.touchHudVisible);
            inputLines = commandInputLines(uiWidth, touchTabVisible);
        }
        const size_t inputCapacity=static_cast<size_t>(std::max(1,
            uiTextLineCapacity(uiHeight,commandBottom+12,lineHeight)));
        if (inputLines.size()>inputCapacity)
            inputLines.erase(inputLines.begin(),inputLines.end()-static_cast<std::ptrdiff_t>(inputCapacity));
        const float inputHeight = commandOpen
            ? 11.0f + lineHeight * static_cast<float>(inputLines.size()) : 0.0f;

        const size_t historyCapacity=static_cast<size_t>(std::min(8,
            uiTextLineCapacity(uiHeight,commandBottom+inputHeight+8,lineHeight)));
        std::vector<std::string> visibleHistory;
        for (auto message = chatHistory.rbegin();
             message != chatHistory.rend() && visibleHistory.size() < historyCapacity;
             ++message) {
            const auto lines = wrap(*message, 1.0f, textWidth);
            for (auto line = lines.rbegin();
                 line != lines.rend() && visibleHistory.size() < historyCapacity; ++line)
                visibleHistory.push_back(*line);
        }
        if (!visibleHistory.empty()) {
            UiTheme::panel(renderer, 12.0f, commandBottom + inputHeight,
                static_cast<float>(uiWidth - 24),
                lineHeight * static_cast<float>(visibleHistory.size()) + 8.0f,
                UiTheme::PANEL, {}, 1.0f, 0.88f);
            for (size_t i = 0; i < visibleHistory.size(); ++i)
                UiTheme::textWithShadow(renderer, visibleHistory[i], 20.0f,
                    23.0f + inputHeight + lineHeight * static_cast<float>(i),
                    1.0f, glm::vec3(1.0f, 0.88f, 0.58f), 0.95f);
        }
        if (commandOpen) {
            UiTheme::panel(renderer, 12.0f, commandBottom,
                static_cast<float>(uiWidth - 24), inputHeight,
                UiTheme::PANEL, {}, 1.0f, 0.92f);
            for (size_t i = 0; i < inputLines.size(); ++i)
                UiTheme::textWithShadow(renderer,
                    inputLines[inputLines.size() - 1 - i],
                    20.0f, 25.0f + lineHeight * static_cast<float>(i),
                    1.25f, glm::vec3(1.0f));
            if (settings.controlMode == ControlMode::Touch ||
                (settings.controlMode == ControlMode::Auto &&
                 inputs.touchHudVisible)) {
                const TouchRect tab = touchCommandTabRect(uiWidth, uiHeight);
                UiTheme::button(renderer, tab.x, tab.y, tab.w, tab.h,
                    localization.text("touch.tab"),
                    UiTheme::WidgetState::Normal, false, 0.0f, 0.92f);
            }
        }
        renderer.endUIFrame();
    }

    // Phase 3: Active menu (overlays everything)
    if (activeMenu) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        renderer.setOpacity(activeMenu->opacity());
        activeMenu->render(renderer, uiWidth, uiHeight);
        renderer.setOpacity(1.0f);
        renderer.endUIFrame();
    }

    if (state == GameState::LoadingWorld) {
        const auto loading = session.loadingSnapshot();
        const auto& progress = loading.progress;
        const float fraction = loading.fraction;
        renderer.beginUIFrame(uiWidth, uiHeight);
        UiTheme::dirtBackground(renderer, static_cast<float>(uiWidth),
                                static_cast<float>(uiHeight));
        const char* loadingTitleKey = "loading.title";
        if (loading.reason == GameSession::LoadingReason::EnteringHeaven)
            loadingTitleKey = "loading.enter_heaven";
        else if (loading.reason == GameSession::LoadingReason::ReturningOverworld)
            loadingTitleKey = "loading.return_overworld";
        const std::string title = localization.text(loadingTitleKey);
        const std::string status = localization.format(
            loading.preparing ? "loading.preparing" :
            (loading.newWorld ? "loading.generating"
                               : "loading.cached"), {
            std::to_string(progress.completed), std::to_string(progress.total)});
        const float barWidth=std::max(1.0f,std::min(420.0f,uiWidth-80.0f));
        const float panelW = barWidth + 64.0f;
        const float panelX = (uiWidth - panelW) * 0.5f;
        const float panelY=std::max(8.0f,(uiHeight-150.0f)*.5f);
        const float panelH = 150.0f;
        UiTheme::panel(renderer, panelX, panelY, panelW, panelH,
                       UiTheme::PANEL, {}, 1.0f);
        const float titleScale=UiTheme::fittedScale(renderer,title,2.4f,panelW-40);
        const auto titleSize=renderer.measureText(title,titleScale);
        UiTheme::textWithShadow(renderer, title,
            (uiWidth - titleSize.x) * 0.5f, panelY + panelH - 48.0f,titleScale,
            UiTheme::TEXT_TITLE, 1.0f, 2.0f, -2.0f);
        const float statusScale=UiTheme::fittedScale(renderer,status,1.1f,panelW-40);
        const auto statusSize=renderer.measureText(status,statusScale);
        UiTheme::textWithShadow(renderer, status,
            (uiWidth - statusSize.x) * 0.5f, panelY + 60.0f,statusScale,
            glm::vec3(0.82f));
        const float barX = panelX + 32.0f;
        const float barY = panelY + 26.0f;
        UiTheme::progressBar(renderer, barX, barY, barWidth, 16.0f, fraction,
                             UiTheme::ACCENT);
        renderer.endUIFrame();
    }

    if (session.isPlayerDead()) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        renderer.drawRect(0, 0, static_cast<float>(uiWidth),
                              static_cast<float>(uiHeight),
                              glm::vec4(0.28f, 0.0f, 0.0f, 0.62f));
        UiTheme::vignette(renderer, static_cast<float>(uiWidth),
                          static_cast<float>(uiHeight),
                          glm::vec4(0.40f, 0.0f, 0.0f, 0.55f));
        const std::string title = localization.text("death.title");
        const float titleScale=UiTheme::fittedScale(renderer,title,3.4f,uiWidth-40.0f);
        auto titleSize=renderer.measureText(title,titleScale);
        UiTheme::textWithShadow(renderer, title,
            (uiWidth - titleSize.x) * 0.5f, uiHeight * 0.58f,titleScale,
            glm::vec3(1.0f, 0.82f, 0.82f), 1.0f, 2.0f, -2.0f);
        const std::string prompt = localization.text("death.respawn");
        const float promptScale=UiTheme::fittedScale(renderer,prompt,1.2f,uiWidth-40.0f);
        auto promptSize=renderer.measureText(prompt,promptScale);
        UiTheme::textWithShadow(renderer, prompt,
            (uiWidth - promptSize.x) * 0.5f, uiHeight * 0.46f,promptScale,
            glm::vec3(1.0f));
        renderer.endUIFrame();
    }
}


GameUiController::GameUiController(
    InventoryModel& inventory, platform::Clipboard& clipboard)
    : survivalInventory(inventory),
      containerScreen(inventory),
      tradeScreen(inventory),
      commandInput({}, 80, &clipboard) {}

void GameUiController::tick(float dt) {
    renderer.advanceTime(dt);
    if (activeMenu) activeMenu->tick(dt);
    if (inventoryOpen) inventoryFade.tick(dt,true,0.16f);
    else inventoryFade.value=0;
    hudTime += std::max(0.0f, dt);
    if (chatVisibleSeconds > 0.0f)
        chatVisibleSeconds = std::max(0.0f, chatVisibleSeconds - dt);
    if (itemNameSeconds > 0.0f)
        itemNameSeconds = std::max(0.0f, itemNameSeconds - dt);
    if (hotbar.getSelectedSlot() != lastHudSlot) {
        lastHudSlot = hotbar.getSelectedSlot();
        itemNameSeconds = 2.0f;
    }
}

void GameUiController::showMessage(const std::string& message) {
    if (message.empty()) return;
    chatHistory.push_back(message);
    while (chatHistory.size() > 100) chatHistory.pop_front();
    chatVisibleSeconds = 8.0f;
}

void GameUiController::openCommand() {
    commandOpen = true;
    commandInput.setText({});
    resetCommandCompletion();
}

void GameUiController::openInventory(bool creativeCatalog) {
    containerOpen = false;
    tradeOpen = false;
    creativeCatalogOpen = creativeCatalog;
    inventoryFade.value=0;
    inventoryOpen = true;
}

void GameUiController::openCreativeCatalog() {
    survivalInventory.onClose();
    creativeCatalogOpen = true;
    inventoryFade.value=0;
}

void GameUiController::openPlayerInventoryTab() {
    creativeCatalogOpen = false;
    inventoryFade.value=0;
    survivalInventory.setCraftingTable(false);
}

void GameUiController::closeCommand() {
    commandOpen = false;
    commandInput.setText({});
    resetCommandCompletion();
}

bool GameUiController::completeCommand(bool reverse) {
    if (!commandCompletion.active) {
        commandCompletion.suggestions = commandSuggestions(
            commandInput.text(), commandInput.cursor());
        if (commandCompletion.suggestions.empty()) return false;
        commandCompletion.baseText = commandInput.text();
        commandCompletion.index = reverse
            ? commandCompletion.suggestions.size() - 1 : 0;
        commandCompletion.active = true;
    } else if (reverse) {
        commandCompletion.index = commandCompletion.index == 0
            ? commandCompletion.suggestions.size() - 1
            : commandCompletion.index - 1;
    } else {
        commandCompletion.index =
            (commandCompletion.index + 1) % commandCompletion.suggestions.size();
    }

    const CommandSuggestion& suggestion =
        commandCompletion.suggestions[commandCompletion.index];
    std::string completed = commandCompletion.baseText;
    completed.replace(suggestion.start, suggestion.end - suggestion.start,
                      suggestion.text);
    commandInput.setText(
        std::move(completed), suggestion.start + suggestion.text.size());
    return true;
}

void GameUiController::resetCommandCompletion() {
    commandCompletion = {};
}

bool GameUiController::playerInventoryViewOpen(const Player& player) const {
    return player.isSurvival() ||
        (player.gameMode() == GameMode::Creative && !creativeCatalogOpen);
}

void GameUiController::renderSurvivalHud(const Player& player, int screenWidth) {
    const auto& stats = player.survivalStats();
    const UiHotbarLayout bar(screenWidth);
    const float s=bar.scale;
    const float unitW=12*s,gap=2*s;
    const float y=bar.y+bar.height+12*s;
    const float groupW = 10.0f * unitW + 9.0f * gap;
    const float leftX = screenWidth * 0.5f - groupW - 10.0f*s;
    const float rightX = screenWidth * 0.5f + 10.0f*s;
    const float px=1.5f*s;  // pixel size for the icon sprites
    for (int i = 0; i < 10; ++i) {
        const float healthFill = std::clamp(stats.health() - i * 2.0f, 0.0f, 2.0f) * 0.5f;
        const float hungerFill = std::clamp(
            static_cast<float>(stats.hunger()) - i * 2.0f, 0.0f, 2.0f) * 0.5f;
        const float hx = leftX + i * (unitW + gap);
        const float hungerJitter = stats.saturation() <= 0.0f &&
            std::fmod(hudTime * 20.0f + i * 7.0f,
                      std::max(1.0f, stats.hunger() * 3.0f + 1.0f)) < 1.0f
            ? std::sin(hudTime * 91.0f + i * 3.1f) * 1.5f : 0.0f;
        const float fx = rightX + (9 - i) * (unitW + gap);
        auto heart = [&](float x, float fill) {
            if (fill >= 1.0f)
                UiTheme::sprite(renderer, x, y, px, UiTheme::HEART_FULL,
                                UiTheme::HEART_PALETTE);
            else if (fill > 0.0f)
                UiTheme::sprite(renderer, x, y, px, UiTheme::HEART_HALF,
                                UiTheme::HEART_PALETTE);
            else
                UiTheme::sprite(renderer, x, y, px, UiTheme::HEART_EMPTY,
                                UiTheme::HEART_PALETTE);
        };
        auto food = [&](float x, float fill) {
            if (fill >= 1.0f)
                UiTheme::sprite(renderer, x, y + hungerJitter, px, UiTheme::HUNGER_FULL,
                                UiTheme::HUNGER_PALETTE);
            else if (fill > 0.0f)
                UiTheme::sprite(renderer, x, y + hungerJitter, px, UiTheme::HUNGER_HALF,
                                UiTheme::HUNGER_PALETTE);
            else
                UiTheme::sprite(renderer, x, y + hungerJitter, px, UiTheme::HUNGER_EMPTY,
                                UiTheme::HUNGER_PALETTE);
        };
        heart(hx, healthFill);
        food(fx, hungerFill);
    }
    const int armor = totalArmorPoints(player.inventory());
    if (armor > 0) {
        const float armorY=y+16*s;
        for (int i = 0; i < 10; ++i) {
            const float fill = std::clamp((armor - i * 2) * 0.5f, 0.0f, 1.0f);
            const float x = leftX + i * (unitW + gap);
            if (fill>=1)
                UiTheme::sprite(renderer,x,armorY,px,UiTheme::ARMOR_FULL,UiTheme::ARMOR_PALETTE);
            else if (fill>0)
                UiTheme::sprite(renderer,x,armorY,px,UiTheme::ARMOR_HALF,UiTheme::ARMOR_PALETTE);
            else
                UiTheme::sprite(renderer,x,armorY,px,UiTheme::ARMOR_EMPTY,UiTheme::ARMOR_PALETTE);
        }
    }
    if (player.underwater()) {
        const int bubbles = static_cast<int>(std::ceil(player.airFraction() * 10.0f));
        for (int i=0;i<10;++i) {
            const float x = rightX + (9-i)*(unitW+gap);
            if (i < bubbles)
                UiTheme::sprite(renderer, x+s,y+16*s, px,
                                UiTheme::BUBBLE_FULL, UiTheme::BUBBLE_PALETTE);
            else
                UiTheme::sprite(renderer, x+s,y+16*s, px,
                                UiTheme::BUBBLE_EMPTY, UiTheme::BUBBLE_PALETTE);
        }
    }
}

void GameUiController::renderSelectedItemName(const Player& player, int screenWidth) {
    std::string name;
    const auto& stack = player.inventory().slot(
        static_cast<size_t>(hotbar.getSelectedSlot()));
    if (!stack.empty()) name = localization.itemName(stack.id);
    if (name.empty()) return;
    const UiHotbarLayout bar(screenWidth);
    const float scale=UiTheme::fittedScale(renderer,name,1.0f,screenWidth-40.0f);
    const auto size=renderer.measureText(name,scale);
    const float x=(screenWidth-size.x)*.5f,y=bar.y+bar.height+48*bar.scale;
    const float alpha=std::min(1.0f,itemNameSeconds/.25f);
    UiTheme::rounded(renderer,x-8,y-4,size.x+16,size.y+8,4,
                     UiTheme::withAlpha(UiTheme::PANEL_DEEP,.85f*alpha));
    UiTheme::textWithShadow(renderer,name,x,y,scale,UiTheme::TEXT,alpha);
}

void GameUiController::renderCrosshairAndMiningProgress(const Player& player, int screenWidth, int screenHeight) {
    const float centerX = static_cast<float>(screenWidth) * 0.5f;
    const float centerY = static_cast<float>(screenHeight) * 0.5f;
    constexpr float armLength = 9.0f;
    constexpr float thickness = 2.0f;
    constexpr float centerGap = 2.0f;

    auto drawCrossPart = [&](float x, float y, float width, float height) {
        renderer.drawRect(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f,
                          UiTheme::INK);
        renderer.drawRect(x, y, width, height,
                          glm::vec4(1.0f, 1.0f, 1.0f, 0.95f));
    };
    drawCrossPart(centerX - centerGap - armLength, centerY - thickness * 0.5f,
                  armLength, thickness);
    drawCrossPart(centerX + centerGap, centerY - thickness * 0.5f,
                  armLength, thickness);
    drawCrossPart(centerX - thickness * 0.5f, centerY + centerGap,
                  thickness, armLength);
    drawCrossPart(centerX - thickness * 0.5f,
                  centerY - centerGap - armLength, thickness, armLength);

    const float progress = player.getMiningProgress();
    if (progress <= 0.0f) return;
    constexpr float barWidth = 112.0f;
    constexpr float barHeight = 8.0f;
    const float barX = centerX - barWidth * 0.5f;
    const float barY = centerY - 42.0f;
    UiTheme::progressBar(renderer, barX, barY, barWidth, barHeight, progress,
                         UiTheme::ACCENT);
}

void GameUiController::renderAttackIndicator(
    const Player& player, AttackIndicator mode,
    int screenWidth, int screenHeight) {
    if (mode == AttackIndicator::Off) return;
    const float strength = player.attackStrength();
    const bool readyTarget = player.hasChargedAttackTarget();
    if (strength >= 1.0f && !readyTarget) return;
    const glm::vec4 background(0.04f, 0.04f, 0.05f, 0.86f);
    const glm::vec4 fill = readyTarget
        ? glm::vec4(0.95f, 0.95f, 0.95f, 1.0f)
        : glm::vec4(0.78f, 0.80f, 0.84f, 1.0f);
    if (mode == AttackIndicator::Crosshair) {
        constexpr float width = 22.0f;
        constexpr float height = 5.0f;
        const float x = screenWidth * 0.5f - width * 0.5f;
        const float y = screenHeight * 0.5f - 29.0f;
        renderer.drawRect(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f,
                          UiTheme::INK);
        renderer.drawRect(x, y, width, height, background);
        renderer.drawRect(x, y, std::floor(width * strength), height, fill);
        if (readyTarget) {
            renderer.drawRect(x + width * 0.5f - 1.0f, y + 8.0f,
                              2.0f, 7.0f, fill);
            renderer.drawRect(x + width * 0.5f - 3.5f, y + 10.5f,
                              7.0f, 2.0f, fill);
        }
        return;
    }

    const UiHotbarLayout bar(screenWidth);
    const float width=6,height=bar.slot;
    if (bar.x<16) {
        UiTheme::progressBar(renderer,bar.x+bar.width-36,bar.y+bar.height+3,
                             28,4,strength,readyTarget?UiTheme::ACCENT:fill);
    } else {
        const float x=bar.x-12,y=bar.y+bar.padY;
        UiTheme::rounded(renderer,x,y,width,height,3,background);
        UiTheme::rounded(renderer,x,y,width,height*strength,3,readyTarget?UiTheme::ACCENT:fill);
    }
}

void GameUiController::updateMouseScreenPosition(Window& window) {
    double windowX = 0.0;
    double windowY = 0.0;
    window.getCursorPos(windowX, windowY);

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    windowWidth = window.windowWidth();
    windowHeight = window.windowHeight();
    framebufferWidth = window.width();
    framebufferHeight = window.height();

    const double scaleX = windowWidth > 0
        ? static_cast<double>(framebufferWidth) / windowWidth : 1.0;
    const double scaleY = windowHeight > 0
        ? static_cast<double>(framebufferHeight) / windowHeight : 1.0;
    const double uiScale = std::max(1, guiScale);
    const WindowSafeArea safe = window.safeArea();
    mouseScreenX = (windowX * scaleX - safe.x) / uiScale;
    mouseScreenY =
        (static_cast<double>(framebufferHeight) - windowY * scaleY - safe.y) / uiScale;
}

glm::vec2 GameUiController::touchToUi(Window& window, double x, double y) const {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    windowWidth = window.windowWidth();
    windowHeight = window.windowHeight();
    framebufferWidth = window.width();
    framebufferHeight = window.height();
    const double scaleX = windowWidth > 0
        ? static_cast<double>(framebufferWidth) / windowWidth : 1.0;
    const double scaleY = windowHeight > 0
        ? static_cast<double>(framebufferHeight) / windowHeight : 1.0;
    const double uiScale = std::max(1, guiScale);
    const WindowSafeArea safe = window.safeArea();
    return {static_cast<float>((x * scaleX - safe.x) / uiScale),
            static_cast<float>((framebufferHeight - y * scaleY - safe.y) / uiScale)};
}
