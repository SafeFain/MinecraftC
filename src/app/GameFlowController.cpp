#include "app/GameFlowController.h"
#include "app/GameScenePresenter.h"
#include "app/GameSession.h"
#include "app/GameUiController.h"
#include "audio/AudioSystem.h"
#include "core/Window.h"
#include "game/ClientSettings.h"
#include "game/Command.h"
#include "game/InventoryInteraction.h"
#include "game/Item.h"
#include "platform/Clipboard.h"
#include "player/Player.h"
#include "ui/Menu.h"
#include "debug/Log.h"
#include "Config.h"

#include <glm/glm.hpp>

GameFlowController::GameFlowController(
    GameSession& session, GameUiController& ui, GameScenePresenter& scene,
    AudioSystem& audio, Window& window, RuntimeClock& clock,
    ClientSettings& settings, platform::Clipboard& clipboard)
    : m_session(session), m_ui(ui), m_scene(scene), m_audio(audio),
      m_window(window), m_clock(clock), m_settings(settings),
      m_clipboard(clipboard) {}

void GameFlowController::startGame(const std::string& worldId, bool newWorld) {
    saveCurrentWorld();
    m_audio.setMusicMode(AudioMusicMode::Gameplay);
    const GameMode mode = m_session.startWorld(
        worldId, newWorld, m_clock.now());

    m_state = GameState::LoadingWorld;
    m_ui.hotbar.setInventory(mode == GameMode::Spectator
        ? nullptr : &m_session.player.inventory());
    m_ui.survivalInventory.setCreativeAccess(mode == GameMode::Creative);
    m_audio.stopRain();
    m_scene.resetForWorld(m_session.player.getPosition());
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
    m_state = GameState::LoadingWorld;
    m_window.setCursorLocked(false);
    m_ui.activeMenu.reset();
    m_ui.commandOpen = false;
    m_ui.commandInput.setText({});
    m_scene.resetForWorld(m_session.player.getPosition());
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
    m_scene.resetPlayerFeedback(m_session.player.getPosition());
    if (!wasHeaven) m_window.setCursorLocked(true);
}

void GameFlowController::openInventory() {
    if (m_session.player.isSpectator()) return;
    m_session.player.cancelBowCharge();
    if (m_session.player.isSurvival() && !m_ui.inventoryOpen)
        m_ui.survivalInventory.setCraftingTable(false);
    m_ui.openInventory(
        m_session.player.gameMode() == GameMode::Creative);
    m_window.setCursorLocked(false);
}

void GameFlowController::closeInventory() {
    if (m_ui.tradeOpen) {
        m_ui.tradeScreen.close();
    } else if (m_ui.containerOpen) {
        m_ui.containerScreen.close([this](ItemStack stack) {
            m_session.entities.spawnItem(m_session.player.getPosition() + glm::dvec3(0.0, 0.5, 0.0), stack);
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
        m_session.executeCommand(*result.command, m_ui.localization);
    if (execution.gameModeChanged) {
        const GameMode mode = *execution.gameModeChanged;
        m_ui.hotbar.setInventory(mode == GameMode::Spectator
            ? nullptr : &m_session.player.inventory());
        m_ui.survivalInventory.setCreativeAccess(
            mode == GameMode::Creative);
    }
    for (const std::string& message : execution.messages)
        showCommandMessage(message);
}

void GameFlowController::openPlayerInventoryView() {
    if (m_session.player.gameMode() != GameMode::Creative) return;
    m_ui.openPlayerInventoryTab();
}

void GameFlowController::giveCreativeItem(ItemId id, int hotbarSlot) {
    if (hotbarSlot < 0 || hotbarSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE))
        hotbarSlot = m_ui.hotbar.getSelectedSlot();
    auto& slot = m_session.player.inventory().slot(
        static_cast<size_t>(hotbarSlot));
    InventoryInteraction::setCreativeItem(slot, id);
    m_ui.itemNameSeconds = 2.0f;
}

void GameFlowController::dropSelectedItem(bool entireStack) {
    if (m_session.player.isSpectator()) return;
    auto& slot = m_session.player.inventory().slot(
        static_cast<size_t>(m_ui.hotbar.getSelectedSlot()));
    ItemStack dropped = slot;
    if (!entireStack) dropped = InventoryInteraction::takeOne(slot);
    else slot.clear();
    dropInventoryItem(dropped);
}

void GameFlowController::dropInventoryItem(ItemStack dropped) {
    if (dropped.empty()) return;
    const glm::vec3 forward = glm::normalize(m_session.player.getForward());
    m_session.entities.spawnItem(
        m_session.player.getEyePosition() + glm::dvec3(forward) * 0.65,
        dropped, forward * 4.5f + glm::vec3(0.0f, 1.5f, 0.0f), 0.8f);
}

void GameFlowController::pickBlock() {
    if (m_session.player.isSpectator()) return;
    const auto hit = m_session.world.raycast(
        m_session.player.getEyePosition(), m_session.player.getForward(),
        Config::REACH_DISTANCE);
    if (!hit) return;
    const ItemId item = itemForBlock(m_session.world.getBlock(
        hit->blockPos.x, hit->blockPos.y, hit->blockPos.z));
    if (item == ItemId::EMPTY) return;

    auto& inventory = m_session.player.inventory();
    int source = -1;
    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) {
        if (inventory.slot(i).id == item) { source = static_cast<int>(i); break; }
    }
    if (source >= 0 && source < static_cast<int>(InventoryModel::HOTBAR_SIZE)) {
        m_ui.hotbar.selectSlot(source);
        m_session.player.setSelectedSlot(source);
        return;
    }

    const int selected = m_ui.hotbar.getSelectedSlot();
    if (m_session.player.gameMode() == GameMode::Creative) {
        InventoryInteraction::setCreativeItem(
            inventory.slot(static_cast<size_t>(selected)), item);
    } else if (source >= 0) {
        std::swap(inventory.slot(static_cast<size_t>(selected)),
                  inventory.slot(static_cast<size_t>(source)));
    }
    m_session.player.setSelectedSlot(selected);
    m_ui.itemNameSeconds = 2.0f;
}

void GameFlowController::swapOffhand() {
    if (m_session.player.isSpectator()) return;
    auto& inventory = m_session.player.inventory();
    std::swap(inventory.slot(static_cast<size_t>(m_ui.hotbar.getSelectedSlot())),
              inventory.offhand());
    m_session.player.cancelBowCharge();
    m_ui.itemNameSeconds = 2.0f;
}

bool GameFlowController::playerInventoryViewOpen() const {
    return m_ui.playerInventoryViewOpen(m_session.player);
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
        m_ui.menuCallbacks, m_session.worldCatalog.list(), m_settings,
        m_ui.localization, &m_clipboard);
}
