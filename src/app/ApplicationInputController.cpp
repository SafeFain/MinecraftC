#include "app/ApplicationInputController.h"

#include "core/Window.h"
#include "game/ClientSettings.h"

#include <algorithm>

void ApplicationInputController::bind(
    Window& window, const ClientSettings& settings, Callbacks callbacks) {
    m_settings = &settings;
    m_callbacks = std::move(callbacks);
    window.setKeyCallback(
        [this](int key, int scancode, ButtonAction action, int mods) {
            state.keyEvent(key, action);
            state.update(m_settings->bindings);
            if (action == ButtonAction::Press ||
                action == ButtonAction::Repeat) {
                if (key >= 0 && key < Key::Count) keys[key] = true;
            } else if (action == ButtonAction::Release &&
                       key >= 0 && key < Key::Count) {
                keys[key] = false;
            }
            if (m_callbacks.key)
                m_callbacks.key(key, scancode, action, mods);
        });
    window.setCharCallback([this](std::string_view text) {
        if (m_callbacks.text) m_callbacks.text(text);
    });
    window.setMouseButtonCallback(
        [this](int button, ButtonAction action, int mods) {
            // Touch controls drive the UI through synthetic mouse events; keep
            // physical mouse input from mutating InputState while touch is active.
            if (uiTouch.active) return;
            state.mouseEvent(button, action);
            state.update(m_settings->bindings);
            if (m_callbacks.mouseButton)
                m_callbacks.mouseButton(button, action, mods);
        });
    window.setScrollCallback([this](double x, double y) {
        state.scrollEvent(y);
        state.update(m_settings->bindings);
        if (m_callbacks.scroll) m_callbacks.scroll(x, y);
    });
    window.setTouchCallback([this](const TouchEvent& event) {
        if (m_callbacks.touch) m_callbacks.touch(event);
    });
    // Focus loss clears held physical input inside the controller; no
    // forwarding callback is needed by the application.
    window.setFocusCallback([this](bool focused) {
        if (!focused) clearPhysical(*m_settings);
    });
    window.setScreenKeyboardCallback([this](bool visible) {
        if (m_callbacks.screenKeyboard)
            m_callbacks.screenKeyboard(visible);
    });
}

void ApplicationInputController::beginFrame(
    Window& window, const ClientSettings& settings,
    const TouchControlConfig& config, int guiScale, bool textInput) {
    state.beginFrame();
    state.clearVirtual();
    window.setTextInputEnabled(textInput);
    const WindowSafeArea safe = window.safeArea();
    touchControls.configure(
        std::max(1, safe.width / std::max(1, guiScale)),
        std::max(1, safe.height / std::max(1, guiScale)), config);
    if (settings.controlMode != ControlMode::KeyboardMouse)
        touchControls.applyTo(state);
    window.gamepads().sample(gamepadButtons, gamepadAxes);
    state.updateGamepad(settings.gamepadBindings, gamepadButtons,
                        gamepadAxes, settings.gamepadDeadzone);
    state.update(settings.bindings);
}

void ApplicationInputController::clearPhysical(
    const ClientSettings& settings) {
    state.clearPhysical();
    state.update(settings.bindings);
    keys.fill(false);
}

bool ApplicationInputController::altPressed() const {
    return keys[Key::LeftAlt] || keys[Key::RightAlt];
}
