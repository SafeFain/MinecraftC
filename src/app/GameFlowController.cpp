#include "app/GameFlowController.h"
#include "app/GameScenePresenter.h"
#include "app/GameSession.h"
#include "app/GameUiController.h"
#include "audio/AudioSystem.h"
#include "core/Window.h"
#include "game/ClientSettings.h"
#include "game/Command.h"
#include "platform/Clipboard.h"
#include "player/Player.h"
#include "ui/Menu.h"
#include "debug/Log.h"
#include "Config.h"

#include <glm/glm.hpp>

namespace {
AudioMusicMode musicModeFor(DimensionId dimension) {
    return dimension == DimensionId::Heaven
        ? AudioMusicMode::Heaven : AudioMusicMode::Overworld;
}
}

GameFlowController::GameFlowController(
    GameSession& session, GameUiController& ui, GameScenePresenter& scene,
    AudioSystem& audio, Window& window, RuntimeClock& clock,
    ClientSettings& settings, platform::Clipboard& clipboard)
    : m_session(session), m_ui(ui), m_scene(scene), m_audio(audio),
      m_window(window), m_clock(clock), m_settings(settings),
      m_clipboard(clipboard) {}

void GameFlowController::startGame(const std::string& worldId, bool newWorld) {
    saveCurrentWorld();
    const GameMode mode = m_session.startWorld(
        worldId, newWorld, m_clock.now());
    m_audio.setMusicMode(musicModeFor(m_session.activeDimension()));

    m_state = GameState::LoadingWorld;
    m_ui.hotbar.setInventory(mode == GameMode::Spectator
        ? nullptr : &m_session.inventory());
    m_ui.survivalInventory.setCreativeAccess(mode == GameMode::Creative);
    m_audio.stopRain();
    m_scene.resetForWorld(m_session.playerState().getPosition());
    m_window.setCursorLocked(false);
    m_ui.activeMenu.reset();
    m_ui.commandOpen = false;
    m_ui.commandInput.setText({});
    m_ui.chatHistory.clear();
    m_ui.chatVisibleSeconds = 0.0f;
    LOG_INFO((newWorld ? "Pregenerating new world around spawn"
                       : "Loading existing world around saved position")
             << " at render distance " << Config::RENDER_DISTANCE);
}

void GameFlowController::completeLoading() {
    m_state = GameState::Playing;
    m_window.setCursorLocked(true);
}

void GameFlowController::beginDimensionLoading() {
    m_audio.setMusicMode(musicModeFor(m_session.activeDimension()));
    m_state = GameState::LoadingWorld;
    m_window.setCursorLocked(false);
    m_ui.activeMenu.reset();
    m_ui.commandOpen = false;
    m_ui.commandInput.setText({});
    m_scene.resetForWorld(m_session.playerState().getPosition());
    m_audio.stopRain();
}

void GameFlowController::pause() {
    m_audio.setPaused(true);
    m_state = GameState::Paused;
    m_window.setCursorLocked(false);
    m_ui.activeMenu = std::make_unique<PauseMenu>(
        m_ui.menuCallbacks, m_ui.localization);
}

void GameFlowController::resume() {
    m_audio.setPaused(false);
    m_state = GameState::Playing;
    m_window.setCursorLocked(true);
    m_ui.activeMenu.reset();
}

void GameFlowController::backToMainMenu() {
    saveCurrentWorld();
    m_session.leaveWorld();
    m_audio.stopRain();
    m_audio.setPaused(false);
    m_state = GameState::MainMenu;
    m_window.setCursorLocked(false);
    showMainMenu();
}

void GameFlowController::respawnPlayer() {
    const bool wasHeaven = m_session.activeDimension() == DimensionId::Heaven;
    m_session.respawn(m_clock.now());
    if (wasHeaven) beginDimensionLoading();
    m_scene.resetPlayerFeedback(m_session.playerState().getPosition());
    if (!wasHeaven) m_window.setCursorLocked(true);
}

void GameFlowController::openInventory() {
    if (m_session.playerState().isSpectator()) return;
    m_session.cancelBowCharge();
    if (m_session.playerState().isSurvival() && !m_ui.inventoryOpen)
        m_ui.survivalInventory.setCraftingTable(false);
    m_ui.openInventory(
        m_session.playerState().gameMode() == GameMode::Creative);
    m_window.setCursorLocked(false);
}

void GameFlowController::closeInventory() {
    if (m_ui.tradeOpen) {
        m_ui.tradeScreen.close();
    } else if (m_ui.containerOpen) {
        m_ui.containerScreen.close([this](ItemStack stack) {
            m_session.dropContainerRemainder(stack);
        });
    } else if (playerInventoryViewOpen()) m_ui.survivalInventory.onClose();
    m_ui.containerOpen = false;
    m_ui.tradeOpen = false;
    m_ui.inventoryOpen = false;
    m_window.setCursorLocked(true);
}

void GameFlowController::openCommandInput(const std::string& initialText) {
    m_ui.openCommand();
    m_ui.commandInput.setText(initialText);
    m_window.setCursorLocked(false);
}

void GameFlowController::closeCommandInput() {
    m_ui.closeCommand();
    if (m_state == GameState::Playing) m_window.setCursorLocked(true);
}

void GameFlowController::executeCommand() {
    const std::string submitted = m_ui.commandInput.text();
    closeCommandInput();
    if (submitted.empty()) return;
    if (submitted.front() != '/') {
        showCommandMessage(m_ui.localization.format("message.chat_self", {submitted}));
        return;
    }

    const CommandParseResult result = parseCommand(submitted);
    if (result.error) {
        showCommandError(submitted, *result.error);
        return;
    }
    const GameSession::CommandResult execution =
        m_session.executeCommand(*result.command, m_ui.localization,
                                 m_clock.now());
    if (execution.gameModeChanged) {
        const GameMode mode = *execution.gameModeChanged;
        m_ui.hotbar.setInventory(mode == GameMode::Spectator
            ? nullptr : &m_session.inventory());
        m_ui.survivalInventory.setCreativeAccess(
            mode == GameMode::Creative);
    }
    for (const std::string& message : execution.messages)
        showCommandMessage(message);
    if (execution.teleported) {
        m_state = GameState::LoadingWorld;
        m_window.setCursorLocked(false);
        m_scene.resetForWorld(m_session.playerState().getPosition());
    }
}

void GameFlowController::openPlayerInventoryView() {
    if (m_session.playerState().gameMode() != GameMode::Creative) return;
    m_ui.openPlayerInventoryTab();
}

void GameFlowController::giveCreativeItem(ItemId id, int hotbarSlot) {
    if (hotbarSlot < 0 || hotbarSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE))
        hotbarSlot = m_ui.hotbar.getSelectedSlot();
    m_session.giveCreativeItem(id, hotbarSlot);
    m_ui.itemNameSeconds = 2.0f;
}

void GameFlowController::dropSelectedItem(bool entireStack) {
    m_session.dropSelectedItem(m_ui.hotbar.getSelectedSlot(), entireStack);
}

void GameFlowController::dropInventoryItem(ItemStack dropped) {
    m_session.dropInventoryItem(dropped);
}

void GameFlowController::pickBlock() {
    const auto result = m_session.pickBlock(m_ui.hotbar.getSelectedSlot());
    if (!result) return;
    m_ui.hotbar.selectSlot(result->slot);
    if (result->itemChanged) m_ui.itemNameSeconds = 2.0f;
}

void GameFlowController::swapOffhand() {
    if (m_session.playerState().isSpectator()) return;
    m_session.swapOffhand(m_ui.hotbar.getSelectedSlot());
    m_ui.itemNameSeconds = 2.0f;
}

bool GameFlowController::playerInventoryViewOpen() const {
    return m_ui.playerInventoryViewOpen(m_session.playerState());
}

void GameFlowController::showCommandMessage(const std::string& message) {
    m_ui.showMessage(message);
}

void GameFlowController::showCommandError(const std::string& submitted,
                                          const CommandError& error) {
    const std::string column = std::to_string(error.position + 1);
    showCommandMessage(error.kind == CommandErrorKind::UnknownCommand
        ? m_ui.localization.format("message.command_unknown", {column})
        : m_ui.localization.format("message.command_error", {column, error.expected}));
    showCommandMessage(submitted);
    showCommandMessage(std::string(error.position, ' ') + "^");
}

void GameFlowController::saveCurrentWorld() {
    m_session.saveNow([this] {
        showCommandMessage(m_ui.localization.text("message.save_log"));
    });
}

void GameFlowController::showMainMenu() {
    m_audio.setMusicMode(AudioMusicMode::Menu);
    m_ui.activeMenu = std::make_unique<MainMenu>(
        m_ui.menuCallbacks, m_session.listWorlds(), m_settings,
        m_ui.localization, &m_clipboard);
}
