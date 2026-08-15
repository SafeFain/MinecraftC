#pragma once

#include "core/Input.h"
#include "core/RuntimeClock.h"
#include "core/Touch.h"
#include "ui/TouchControls.h"

#include <string_view>
#include <vector>

class ApplicationInputController;
class AudioSystem;
class ClientSettings;
class GameFlowController;
class GameSession;
class GameScenePresenter;
class GameUiController;
class Window;
struct TouchEvent;

// Routes every input source (keyboard, text, mouse, scroll, touch, gamepad)
// into the application's UI, session, scene, and flow components. Holds no
// state of its own — input state lives in ApplicationInputController, UI
// state in GameUiController, game state in GameFlowController — so it is a
// pure coordinator and is directly testable with the same offscreen-SDL
// collaborators as the flow tests.

class ApplicationInputRouter {
public:
    ApplicationInputRouter(Window& window, GameUiController& ui,
                           GameSession& session,
                           ApplicationInputController& inputs,
                           GameScenePresenter& scene, ClientSettings& settings,
                           GameFlowController& flow, RuntimeClock& clock);

    void bind();
    void beginFrame(RuntimeClock::Tick now, bool textInputWanted);
    void handleFrameInput(float dt);
    void releaseGameplayActions();

    TouchControlConfig touchConfig() const;
    bool touchUiVisible() const;

    // Event handlers (public so tests can drive them directly).
    void handleKeyEvent(int key, int scancode, ButtonAction action, int mods);
    void handleTextEvent(std::string_view text);
    void handleMouseButtonEvent(int button, ButtonAction action, int mods);
    void handleScrollEvent(double xoffset, double yoffset);
    void handleTouch(const TouchEvent& event);

private:
    void handleGameplayAction(bool use, ButtonAction action);
    void handleUiTouch(const TouchEvent& event, const glm::vec2& position);
    void dispatchTouchCommands(const std::vector<TouchCommandEvent>& commands);
    void dispatchUiTouchButton(int button, ButtonAction action,
                               const glm::vec2& position);
    void dispatchUiTouchMove(const glm::vec2& position);
    void updateLongPress();
    void updateGamepadUi(RuntimeClock::Tick now);
    void cyclePerspective();

    Window& m_window;
    GameUiController& m_ui;
    GameSession& m_session;
    ApplicationInputController& m_inputs;
    GameScenePresenter& m_scene;
    ClientSettings& m_settings;
    GameFlowController& m_flow;
    RuntimeClock& m_clock;
};
