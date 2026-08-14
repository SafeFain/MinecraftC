#include "app/GameUiController.h"

#include "Config.h"
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
            renderer.drawRect(12.0f, 18.0f + inputHeight,
                static_cast<float>(uiWidth - 24),
                lineHeight * static_cast<float>(visibleHistory.size()) + 8.0f,
                glm::vec4(0.02f, 0.02f, 0.03f, 0.72f));
            for (size_t i = 0; i < visibleHistory.size(); ++i)
                renderer.renderText(visibleHistory[i], 20.0f,
                    23.0f + inputHeight + lineHeight * static_cast<float>(i),
                    1.0f, glm::vec3(1.0f, 0.88f, 0.58f));
        }
        if (commandOpen) {
            renderer.drawRect(12.0f, 18.0f,
                static_cast<float>(uiWidth - 24), inputHeight,
                glm::vec4(0.02f, 0.02f, 0.03f, 0.86f));
            for (size_t i = 0; i < inputLines.size(); ++i)
                renderer.renderText(inputLines[inputLines.size() - 1 - i],
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
        renderer.drawRect(0, 0, static_cast<float>(uiWidth),
                              static_cast<float>(uiHeight),
                              glm::vec4(.055f, .065f, .08f, 1.0f));
        const std::string title = localization.text("loading.title");
        const auto titleSize = renderer.measureText(title, 3.0f);
        renderer.renderText(title, (uiWidth - titleSize.x) * 0.5f,
                                uiHeight * 0.58f, 3.0f,
                                glm::vec3(1.0f, .85f, .3f));
        const float barWidth = std::min(420.0f, uiWidth - 48.0f);
        const float barX = (uiWidth - barWidth) * 0.5f;
        const float barY = uiHeight * 0.46f;
        renderer.drawRect(barX - 2, barY - 2, barWidth + 4, 18,
                              glm::vec4(.02f, .02f, .025f, 1.0f));
        renderer.drawRect(barX, barY, barWidth, 14,
                              glm::vec4(.18f, .18f, .2f, 1.0f));
        renderer.drawRect(barX, barY, barWidth * fraction, 14,
                              glm::vec4(.36f, .72f, .3f, 1.0f));
        const std::string status = localization.format(
            session.loadingGenerationComplete ? "loading.preparing" :
            (session.loadingNewWorld ? "loading.generating"
                               : "loading.cached"), {
            std::to_string(progress.completed), std::to_string(progress.total)});
        const auto statusSize = renderer.measureText(status, 1.25f);
        renderer.renderText(status, (uiWidth - statusSize.x) * 0.5f,
                                barY - 28.0f, 1.25f, glm::vec3(.82f));
        renderer.endUIFrame();
    }

    if (session.playerDead) {
        renderer.beginUIFrame(uiWidth, uiHeight);
        renderer.drawRect(0, 0, static_cast<float>(uiWidth),
                              static_cast<float>(uiHeight),
                              glm::vec4(0.28f, 0.0f, 0.0f, 0.62f));
        const std::string title = localization.text("death.title");
        auto titleSize = renderer.measureText(title, 4.0f);
        renderer.renderText(title, (uiWidth - titleSize.x) * 0.5f,
                                uiHeight * 0.58f, 4.0f,
                                glm::vec3(1.0f, 0.82f, 0.82f));
        const std::string prompt = localization.text("death.respawn");
        auto promptSize = renderer.measureText(prompt, 1.5f);
        renderer.renderText(prompt, (uiWidth - promptSize.x) * 0.5f,
                                uiHeight * 0.46f, 1.5f, glm::vec3(1.0f));
        renderer.endUIFrame();
    }
}


GameUiController::GameUiController(
    Player& player, platform::Clipboard& clipboard)
    : survivalInventory(player.inventory()),
      containerScreen(player.inventory()),
      commandInput({}, 80, &clipboard) {}

void GameUiController::tick(float dt) {
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
    for (int i = 0; i < 10; ++i) {
        const float healthFill = std::clamp(stats.health() - i * 2.0f, 0.0f, 2.0f) * 0.5f;
        const float hungerFill = std::clamp(
            static_cast<float>(stats.hunger()) - i * 2.0f, 0.0f, 2.0f) * 0.5f;
        const float hx = leftX + i * (unitW + gap);
        const float fx = rightX + (9 - i) * (unitW + gap);
        auto heart = [&](float x, float fill) {
            renderer.drawRect(x+2,y+2,8,7,{.20f,.03f,.04f,.95f});
            renderer.drawRect(x+1,y+5,10,4,{.20f,.03f,.04f,.95f});
            if (fill > 0) {
                const float width = fill < 1 ? 5.0f : 10.0f;
                renderer.drawRect(x+1,y+5,width,4,{.90f,.07f,.10f,1});
                renderer.drawRect(x+2,y+2,std::max(0.0f,width-2),3,{.90f,.07f,.10f,1});
            }
        };
        auto food = [&](float x, float fill) {
            renderer.drawRect(x+3,y+1,7,8,{.18f,.08f,.02f,.95f});
            renderer.drawRect(x+1,y+3,4,5,{.18f,.08f,.02f,.95f});
            if (fill > 0) renderer.drawRect(x+(fill<1?6:2),y+3,
                fill<1?4:8,5,{.90f,.46f,.06f,1});
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
            renderer.drawRect(x, armorY, unitW, unitH,
                                  glm::vec4(0.08f, 0.10f, 0.13f, 0.9f));
            renderer.drawRect(x, armorY, unitW * fill, unitH,
                                  glm::vec4(0.62f, 0.72f, 0.82f, 1.0f));
        }
    }
    if (player.underwater()) {
        const int bubbles = static_cast<int>(std::ceil(player.airFraction() * 10.0f));
        for (int i=0;i<10;++i) {
            const float x = rightX + (9-i)*(unitW+gap);
            renderer.drawRect(x+2,y+15,8,8,{.06f,.18f,.25f,.9f});
            renderer.drawRect(x+4,y+17,4,4,
                i < bubbles ? glm::vec4(.45f,.82f,1,1) : glm::vec4(.08f,.12f,.16f,1));
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
    renderer.renderText(name, x+1, 66, 1.0f, {.05f,.05f,.05f});
    renderer.renderText(name, x, 67, 1.0f, {.95f,.95f,.95f});
}

void GameUiController::renderCrosshairAndMiningProgress(const Player& player, int screenWidth, int screenHeight) {
    const float centerX = static_cast<float>(screenWidth) * 0.5f;
    const float centerY = static_cast<float>(screenHeight) * 0.5f;
    constexpr float armLength = 8.0f;
    constexpr float thickness = 2.0f;
    constexpr float centerGap = 3.0f;

    const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 0.85f);
    const glm::vec4 foreground(1.0f, 1.0f, 1.0f, 0.95f);
    auto drawCrossPart = [&](float x, float y, float width, float height) {
        renderer.drawRect(x - 1.0f, y - 1.0f,
                              width + 2.0f, height + 2.0f, shadow);
        renderer.drawRect(x, y, width, height, foreground);
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
    renderer.drawRect(barX - 2.0f, barY - 2.0f,
                          barWidth + 4.0f, barHeight + 4.0f,
                          glm::vec4(0.0f, 0.0f, 0.0f, 0.82f));
    renderer.drawRect(barX, barY, barWidth, barHeight,
                          glm::vec4(0.18f, 0.18f, 0.20f, 0.92f));
    renderer.drawRect(barX, barY, barWidth * progress, barHeight,
                          glm::vec4(0.92f, 0.74f, 0.25f, 1.0f));
}
