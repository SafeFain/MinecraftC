#pragma once

#include "core/RuntimeClock.h"
#include "ui/Menu.h"

#include <cstdint>
#include <string>

class AudioSystem;
struct ClientSettings;
class GameSession;
class GameScenePresenter;
class GameUiController;
class Window;
struct CommandError;
namespace platform { class Clipboard; }
enum class ItemId : std::uint16_t;

// Owns the application state machine (GameState) and every transition between
// game states plus the UI/session actions those transitions perform: starting
// and leaving worlds, pause/resume, respawn, inventory and command console,
// creative item grants and item dropping. All collaborators are injected as
// references (app-layer component pattern); the controller owns no other
// state, so it is directly testable without a renderer.

class GameFlowController {
public:
    GameFlowController(GameSession& session, GameUiController& ui,
                       GameScenePresenter& scene, AudioSystem& audio,
                       Window& window, RuntimeClock& clock,
                       ClientSettings& settings, platform::Clipboard& clipboard);

    GameState state() const { return m_state; }
    // Used only by the settings-menu back path, which restores the menu and
    // state that were active before settings were opened.
    void restoreState(GameState state) { m_state = state; }

    // State transitions
    void startGame(const std::string& worldId, bool newWorld);
    void completeLoading();
    void beginDimensionLoading();
    void pause();
    void resume();
    void backToMainMenu();
    void respawnPlayer();

    // UI transitions
    void openInventory();
    void closeInventory();
    void openCommandInput();
    void closeCommandInput();
    void executeCommand();
    void openPlayerInventoryView();
    void giveCreativeItem(ItemId id);
    void dropSelectedItem();
    bool playerInventoryViewOpen() const;

    // Command console feedback
    void showCommandMessage(const std::string& message);
    void showCommandError(const std::string& submitted,
                          const CommandError& error);

    // Persistence and menus
    void saveCurrentWorld();
    void showMainMenu();

private:
    GameState m_state = GameState::MainMenu;
    GameSession& m_session;
    GameUiController& m_ui;
    GameScenePresenter& m_scene;
    AudioSystem& m_audio;
    Window& m_window;
    RuntimeClock& m_clock;
    ClientSettings& m_settings;
    platform::Clipboard& m_clipboard;
};
