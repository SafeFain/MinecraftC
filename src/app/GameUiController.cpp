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

void GameUiController::render(
    GameSession& session, const ClientSettings& settings,
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
        if (containerOpen) {
            containerScreen.render(
                renderer, uiWidth, uiHeight, static_cast<int>(mouseScreenX),
                static_cast<int>(mouseScreenY));
        } else if (playerInventoryViewOpen(session.player)) {
            survivalInventory.render(renderer, uiWidth, uiHeight,
                static_cast<int>(mouseScreenX), static_cast<int>(mouseScreenY));
        } else {
            inventory.render(renderer, uiWidth, uiHeight,
                               static_cast<int>(mouseScreenX),
                               static_cast<int>(mouseScreenY));
        }
        if ((settings.controlMode == ControlMode::Touch || (settings.controlMode == ControlMode::Auto && inputs.touchHudVisible))) {
            const TouchRect close = touchInventoryCloseRect(uiWidth,uiHeight);
            renderer.drawRect(close.x,close.y,close.w,close.h,
                                  glm::vec4(.08f,.09f,.12f,.88f));
            const std::string label=localization.text("touch.close");
            const glm::vec2 labelSize=renderer.measureText(label,.8f);
            renderer.renderText(label,close.x+(close.w-labelSize.x)*.5f,
                close.y+(close.h-labelSize.y)*.5f,.8f,glm::vec3(1.0f));
        }
        renderer.endUIFrame();
    }

    // Phase 2: Hotbar HUD (Playing, no inventory, no menu)
    if (state == GameState::Playing && !inventoryOpen && !activeMenu) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        if (!session.player.isSpectator()) {
            hotbar.render(renderer, uiWidth, uiHeight);
            if (session.player.isSurvival()) renderSurvivalHud(session.player, uiWidth);
            if (showCrosshair)
                renderCrosshairAndMiningProgress(session.player, uiWidth, uiHeight);
            renderAttackIndicator(session.player, settings.attackIndicator,
                                  uiWidth, uiHeight);
            if (itemNameSeconds > 0.0f) renderSelectedItemName(session.player, uiWidth);
        }
        if ((settings.controlMode == ControlMode::Touch || (settings.controlMode == ControlMode::Auto && inputs.touchHudVisible)))
            inputs.touchControls.render(renderer);
        renderer.endUIFrame();
    }

    if (commandOpen || chatVisibleSeconds > 0.0f) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        constexpr float lineHeight = 25.0f;
        const float textWidth = std::max(1.0f, static_cast<float>(uiWidth - 40));
        auto wrap = [&](const std::string& text, float scale) {
            return wrapTextPixels(text, textWidth,
                [&](const std::string& candidate) {
                    return renderer.measureText(candidate, scale).x;
                });
        };
        std::vector<std::string> inputLines;
        if (commandOpen) {
            inputLines = wrap("> " + commandInput.text() + "_", 1.25f);
        }
        const float inputHeight = commandOpen
            ? 11.0f + lineHeight * static_cast<float>(inputLines.size()) : 0.0f;

        std::vector<std::string> visibleHistory;
        for (auto message = chatHistory.rbegin();
             message != chatHistory.rend() && visibleHistory.size() < 8;
             ++message) {
            const auto lines = wrap(*message, 1.0f);
            for (auto line = lines.rbegin();
                 line != lines.rend() && visibleHistory.size() < 8; ++line)
                visibleHistory.push_back(*line);
        }
        if (!visibleHistory.empty()) {
            UiTheme::panel(renderer, 12.0f, 18.0f + inputHeight,
                static_cast<float>(uiWidth - 24),
                lineHeight * static_cast<float>(visibleHistory.size()) + 8.0f,
                UiTheme::PANEL, {}, 1.0f, 0.88f);
            for (size_t i = 0; i < visibleHistory.size(); ++i)
                UiTheme::textWithShadow(renderer, visibleHistory[i], 20.0f,
                    23.0f + inputHeight + lineHeight * static_cast<float>(i),
                    1.0f, glm::vec3(1.0f, 0.88f, 0.58f), 0.95f);
        }
        if (commandOpen) {
            UiTheme::panel(renderer, 12.0f, 18.0f,
                static_cast<float>(uiWidth - 24), inputHeight,
                UiTheme::PANEL, {}, 1.0f, 0.92f);
            for (size_t i = 0; i < inputLines.size(); ++i)
                UiTheme::textWithShadow(renderer,
                    inputLines[inputLines.size() - 1 - i],
                    20.0f, 25.0f + lineHeight * static_cast<float>(i),
                    1.25f, glm::vec3(1.0f));
        }
        renderer.endUIFrame();
    }

    // Phase 3: Active menu (overlays everything)
    if (activeMenu) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        activeMenu->render(renderer, uiWidth, uiHeight);
        renderer.endUIFrame();
    }

    if (state == GameState::LoadingWorld) {
        const auto progress = session.loadingGenerationComplete
            ? session.world.loadingProgress() : session.world.generationProgress();
        const float phaseFraction = progress.total == 0 ? 0.0f :
            static_cast<float>(progress.completed) /
            static_cast<float>(progress.total);
        const float fraction = session.loadingGenerationComplete
            ? 0.75f + phaseFraction * 0.25f : phaseFraction * 0.75f;
        renderer.beginUIFrame(uiWidth, uiHeight);
        UiTheme::dirtBackground(renderer, static_cast<float>(uiWidth),
                                static_cast<float>(uiHeight));
        const char* loadingTitleKey = "loading.title";
        if (session.loadingReason == GameSession::LoadingReason::EnteringHeaven)
            loadingTitleKey = "loading.enter_heaven";
        else if (session.loadingReason == GameSession::LoadingReason::ReturningOverworld)
            loadingTitleKey = "loading.return_overworld";
        const std::string title = localization.text(loadingTitleKey);
        const std::string status = localization.format(
            session.loadingGenerationComplete ? "loading.preparing" :
            (session.loadingNewWorld ? "loading.generating"
                               : "loading.cached"), {
            std::to_string(progress.completed), std::to_string(progress.total)});
        const float barWidth = std::min(420.0f, uiWidth - 80.0f);
        const float panelW = barWidth + 64.0f;
        const float panelX = (uiWidth - panelW) * 0.5f;
        const float panelY = std::max(20.0f, uiHeight * 0.38f);
        const float panelH = 150.0f;
        UiTheme::panel(renderer, panelX, panelY, panelW, panelH,
                       UiTheme::PANEL, {}, 1.0f);
        const auto titleSize = renderer.measureText(title, 3.0f);
        UiTheme::textWithShadow(renderer, title,
            (uiWidth - titleSize.x) * 0.5f, panelY + panelH - 44.0f, 3.0f,
            UiTheme::TEXT_TITLE, 1.0f, 2.0f, -2.0f);
        const auto statusSize = renderer.measureText(status, 1.25f);
        UiTheme::textWithShadow(renderer, status,
            (uiWidth - statusSize.x) * 0.5f, panelY + 60.0f, 1.25f,
            glm::vec3(0.82f));
        const float barX = panelX + 32.0f;
        const float barY = panelY + 26.0f;
        UiTheme::progressBar(renderer, barX, barY, barWidth, 16.0f, fraction,
                             glm::vec4(0.36f, 0.72f, 0.30f, 1.0f));
        renderer.endUIFrame();
    }

    if (session.playerDead) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        renderer.drawRect(0, 0, static_cast<float>(uiWidth),
                              static_cast<float>(uiHeight),
                              glm::vec4(0.28f, 0.0f, 0.0f, 0.62f));
        UiTheme::vignette(renderer, static_cast<float>(uiWidth),
                          static_cast<float>(uiHeight),
                          glm::vec4(0.40f, 0.0f, 0.0f, 0.55f));
        const std::string title = localization.text("death.title");
        auto titleSize = renderer.measureText(title, 4.0f);
        UiTheme::textWithShadow(renderer, title,
            (uiWidth - titleSize.x) * 0.5f, uiHeight * 0.58f, 4.0f,
            glm::vec3(1.0f, 0.82f, 0.82f), 1.0f, 2.0f, -2.0f);
        const std::string prompt = localization.text("death.respawn");
        auto promptSize = renderer.measureText(prompt, 1.5f);
        UiTheme::textWithShadow(renderer, prompt,
            (uiWidth - promptSize.x) * 0.5f, uiHeight * 0.46f, 1.5f,
            glm::vec3(1.0f));
        renderer.endUIFrame();
    }
}


GameUiController::GameUiController(
    Player& player, platform::Clipboard& clipboard)
    : survivalInventory(player.inventory()),
      containerScreen(player.inventory()),
      commandInput({}, 80, &clipboard) {}

void GameUiController::tick(float dt) {
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
}

void GameUiController::openInventory(bool creativeCatalog) {
    containerOpen = false;
    creativeCatalogOpen = creativeCatalog;
    inventoryOpen = true;
}

void GameUiController::openCreativeCatalog() {
    survivalInventory.onClose();
    creativeCatalogOpen = true;
}

void GameUiController::openPlayerInventoryTab() {
    creativeCatalogOpen = false;
    survivalInventory.setCraftingTable(false);
}

void GameUiController::closeCommand() {
    commandOpen = false;
    commandInput.setText({});
}

bool GameUiController::playerInventoryViewOpen(const Player& player) const {
    return player.isSurvival() ||
        (player.gameMode() == GameMode::Creative && !creativeCatalogOpen);
}

void GameUiController::renderSurvivalHud(const Player& player, int screenWidth) {
    const auto& stats = player.survivalStats();
    constexpr float unitW = 12.0f;
    constexpr float unitH = 10.0f;
    constexpr float gap = 2.0f;
    constexpr float y = 76.0f;
    const float groupW = 10.0f * unitW + 9.0f * gap;
    const float leftX = screenWidth * 0.5f - groupW - 10.0f;
    const float rightX = screenWidth * 0.5f + 10.0f;
    constexpr float px = 1.5f;  // pixel size for the icon sprites
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
        constexpr float armorY = y + 14.0f;
        for (int i = 0; i < 10; ++i) {
            const float fill = std::clamp((armor - i * 2) * 0.5f, 0.0f, 1.0f);
            const float x = leftX + i * (unitW + gap);
            UiTheme::progressBar(renderer, x, armorY, unitW, unitH, fill,
                                 glm::vec4(0.62f, 0.72f, 0.82f, 1.0f));
        }
    }
    if (player.underwater()) {
        const int bubbles = static_cast<int>(std::ceil(player.airFraction() * 10.0f));
        for (int i=0;i<10;++i) {
            const float x = rightX + (9-i)*(unitW+gap);
            if (i < bubbles)
                UiTheme::sprite(renderer, x+1.0f, y+14.0f, px,
                                UiTheme::BUBBLE_FULL, UiTheme::BUBBLE_PALETTE);
            else
                UiTheme::sprite(renderer, x+1.0f, y+14.0f, px,
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
    const auto size = renderer.measureText(name, 1.0f);
    const float x = (screenWidth - size.x) * .5f;
    UiTheme::textWithShadow(renderer, name, x, 67.0f, 1.0f,
                            glm::vec3(0.95f, 0.95f, 0.95f));
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
                         UiTheme::GOLD);
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

    const float hotbarWidth = InventoryModel::HOTBAR_SIZE * Config::HOTBAR_SLOT_SIZE +
        (InventoryModel::HOTBAR_SIZE - 1) * Config::HOTBAR_GAP +
        Config::HOTBAR_PAD_X * 2.0f;
    constexpr float width = 6.0f;
    const float height = Config::HOTBAR_SLOT_SIZE;
    const float x = (screenWidth - hotbarWidth) * 0.5f - 13.0f;
    const float y = 4.0f + Config::HOTBAR_PAD_Y;
    renderer.drawRect(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f,
                      UiTheme::INK);
    renderer.drawRect(x, y, width, height, background);
    const float filled = std::floor(height * strength);
    renderer.drawRect(x, y, width, filled, fill);
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
