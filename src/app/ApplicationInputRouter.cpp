#include "app/ApplicationInputRouter.h"
#include "app/ApplicationInputController.h"
#include "app/GameFlowController.h"
#include "app/GameScenePresenter.h"
#include "app/GameSession.h"
#include "app/GameUiController.h"
#include "core/Window.h"
#include "game/ClientSettings.h"
#include "game/Utf8.h"
#include "player/Player.h"
#include "ui/Menu.h"
#include "ui/SettingsMenu.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

ApplicationInputRouter::ApplicationInputRouter(
    Window& window, GameUiController& ui, GameSession& session,
    ApplicationInputController& inputs, GameScenePresenter& scene,
    ClientSettings& settings, GameFlowController& flow, RuntimeClock& clock)
    : m_window(window), m_ui(ui), m_session(session), m_inputs(inputs),
      m_scene(scene), m_settings(settings), m_flow(flow), m_clock(clock) {}

void ApplicationInputRouter::bind() {
    m_inputs.bind(m_window, m_settings, {
        [this](int key, int scancode, ButtonAction action, int mods) {
            handleKeyEvent(key, scancode, action, mods);
        },
        [this](std::string_view text) { handleTextEvent(text); },
        [this](int button, ButtonAction action, int mods) {
            handleMouseButtonEvent(button, action, mods);
        },
        [this](double xoffset, double yoffset) {
            handleScrollEvent(xoffset, yoffset);
        },
        [this](const TouchEvent& event) { handleTouch(event); },
        [this](bool visible) {
            if (!visible && m_ui.commandOpen) m_flow.closeCommandInput();
        }
    });
}

void ApplicationInputRouter::beginFrame(RuntimeClock::Tick now,
                                        bool textInputWanted) {
    m_inputs.beginFrame(m_window, m_settings, touchConfig(), m_ui.guiScale,
                        textInputWanted);
    updateLongPress();
    if (m_flow.state() == GameState::Playing && !m_ui.inventoryOpen &&
        !m_ui.commandOpen && !m_ui.activeMenu &&
        m_inputs.state.pressed(InputAction::Perspective))
        cyclePerspective();
    if (m_flow.state() == GameState::Playing && !m_ui.inventoryOpen &&
        !m_ui.commandOpen && !m_ui.activeMenu && !m_session.playerDead &&
        m_inputs.state.pressed(InputAction::DropItem))
        m_flow.dropSelectedItem();
    updateGamepadUi(now);
}

void ApplicationInputRouter::handleFrameInput(float dt) {
    // ── Handle input ──────────────────────────────────────────
    if (m_flow.state() == GameState::Playing && !m_ui.inventoryOpen &&
        !m_ui.commandOpen && !m_ui.activeMenu) {
        double dx, dy;
        m_window.getCursorDelta(dx, dy);
        m_session.player.handleMouseDelta(static_cast<float>(dx), static_cast<float>(dy),
            m_settings.mouseSensitivity, m_settings.invertMouseY);
        const float padLookX = normalizeGamepadAxis(m_inputs.gamepadAxes[2], m_settings.gamepadDeadzone);
        float padLookY = normalizeGamepadAxis(m_inputs.gamepadAxes[3], m_settings.gamepadDeadzone);
        if (m_settings.invertGamepadY) padLookY = -padLookY;
        m_session.player.handleMouseDelta(padLookX, padLookY,
            4.0f * m_settings.gamepadLookSensitivity * dt * 60.0f, false);
        const glm::vec2 touchLook = m_inputs.touchControls.consumeLookDelta();
        m_session.player.handleMouseDelta(touchLook.x, -touchLook.y, .15f, false);
        if (!m_session.playerDead) m_session.player.handleMovement(m_inputs.state, dt);
    }

    // Track mouse position (always, for inventory/menu hover)
    {
        double pointerDx = 0, pointerDy = 0;
        m_window.getCursorDelta(pointerDx, pointerDy);
        const bool pointerMoved = m_inputs.uiTouch.active || pointerDx != 0.0 || pointerDy != 0.0;
        if (!m_inputs.uiTouch.active) m_ui.updateMouseScreenPosition(m_window);
        else {
            m_ui.mouseScreenX = m_inputs.uiTouch.position.x;
            m_ui.mouseScreenY = m_inputs.uiTouch.position.y;
        }

        // Route to inventory hover if open
        if (m_ui.inventoryOpen && pointerMoved) {
            m_ui.inventory.onMouseMove(
                static_cast<int>(m_ui.mouseScreenX),
                static_cast<int>(m_ui.mouseScreenY));
            if (m_ui.containerOpen) m_ui.containerScreen.onMouseMove(
                static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY));
            else if (m_ui.playerInventoryViewOpen(m_session.player)) m_ui.survivalInventory.onMouseMove(
                static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY));
        }

        // Route to menu hover
        if (m_ui.activeMenu && pointerMoved) {
            m_ui.activeMenu->onMouseMove(m_ui.mouseScreenX, m_ui.mouseScreenY);
        }
    }
}

void ApplicationInputRouter::releaseGameplayActions() {
    handleGameplayAction(false, ButtonAction::Release);
    handleGameplayAction(true, ButtonAction::Release);
    m_inputs.touchControls.cancelAll();
    m_inputs.touchHudVisible = false;
}

TouchControlConfig ApplicationInputRouter::touchConfig() const {
    return {m_settings.touchSensitivity, m_settings.touchControlSize,
            m_settings.touchControlOpacity, m_settings.touchLeftHanded};
}

bool ApplicationInputRouter::touchUiVisible() const {
    return m_settings.controlMode == ControlMode::Touch ||
        (m_settings.controlMode == ControlMode::Auto && m_inputs.touchHudVisible);
}

void ApplicationInputRouter::handleKeyEvent(
    int key, int, ButtonAction action, int mods) {
    if (action == ButtonAction::Press && m_settings.controlMode == ControlMode::Auto)
        m_inputs.touchHudVisible = false;
    auto keyBound = [this, key](InputAction inputAction) {
        const auto& binding = m_settings.bindings[static_cast<size_t>(inputAction)];
        return binding.device == InputDevice::Keyboard && binding.code == key;
    };
    if (m_ui.commandOpen) {
        if (action == ButtonAction::Press) {
            if (key == Key::Escape) {
                m_flow.closeCommandInput();
            } else if (key == Key::Enter) {
                m_flow.executeCommand();
            } else if (key == Key::Backspace) m_ui.commandInput.backspace();
            else if (key == Key::Delete) m_ui.commandInput.eraseForward();
            else if (key == Key::Left) m_ui.commandInput.moveLeft((mods & KeyModifier::Shift) != 0);
            else if (key == Key::Right) m_ui.commandInput.moveRight((mods & KeyModifier::Shift) != 0);
            else if (key == Key::Home) m_ui.commandInput.moveHome((mods & KeyModifier::Shift) != 0);
            else if (key == Key::End) m_ui.commandInput.moveEnd((mods & KeyModifier::Shift) != 0);
            else if ((mods & KeyModifier::Control) != 0 && key == Key::A) m_ui.commandInput.selectAll();
            else if ((mods & KeyModifier::Control) != 0 && key == Key::C) m_ui.commandInput.copySelection();
            else if ((mods & KeyModifier::Control) != 0 && key == Key::X) m_ui.commandInput.cutSelection();
            else if ((mods & KeyModifier::Control) != 0 && key == Key::V) m_ui.commandInput.pasteClipboard();
        }
        return;
    }

    // Menus, including the non-pausing sleep overlay, own all keyboard
    // actions while visible.  This keeps Escape as “get up” during sleep
    // instead of opening the pause screen.
    if (action == ButtonAction::Press && m_ui.activeMenu) {
        m_ui.activeMenu->onKeyPress(key, mods);
        return;
    }

    if (action == ButtonAction::Press && keyBound(InputAction::Command) &&
        m_flow.state() == GameState::Playing && !m_ui.activeMenu &&
        !m_ui.inventoryOpen && !m_session.playerDead) {
        m_flow.openCommandInput();
        return;
    }

    // E key — toggle creative inventory (Playing only, no menu active)
    if (action == ButtonAction::Press && keyBound(InputAction::Inventory)) {
        if (m_flow.state() == GameState::Playing && !m_ui.activeMenu &&
            !m_session.player.isSpectator()) {
            if (m_ui.inventoryOpen) {
                m_flow.closeInventory();
            } else {
                m_flow.openInventory();
            }
            return;
        }
    }

    // Discrete keyboard actions are handled in the event callback.
    // The callback updates InputState immediately, so waiting until
    // runFrame() would lose this press when beginFrame() clears edges.
    if (action == ButtonAction::Press && keyBound(InputAction::Perspective) &&
        m_flow.state() == GameState::Playing && !m_ui.activeMenu &&
        !m_ui.inventoryOpen && !m_ui.commandOpen) {
        cyclePerspective();
        return;
    }

    if (action == ButtonAction::Press && keyBound(InputAction::DropItem) &&
        m_flow.state() == GameState::Playing && !m_ui.activeMenu &&
        !m_ui.inventoryOpen && !m_ui.commandOpen && !m_session.playerDead) {
        m_flow.dropSelectedItem();
        return;
    }

    // Number keys 1-9 — hotbar selection (Playing only)
    for (int slot = 0; slot < 9 && action == ButtonAction::Press; ++slot) {
        if (!keyBound(static_cast<InputAction>(
                static_cast<int>(InputAction::Hotbar1) + slot))) continue;
        if (m_flow.state() == GameState::Playing && !m_ui.activeMenu) {
            m_ui.hotbar.selectSlot(slot);
            m_session.player.setSelectedSlot(m_ui.hotbar.getSelectedSlot());
        }
        break;
    }

    if ((keyBound(InputAction::Attack) || keyBound(InputAction::Use)) &&
        m_flow.state() == GameState::Playing && !m_ui.inventoryOpen && !m_ui.commandOpen) {
        if (keyBound(InputAction::Attack)) handleGameplayAction(false, action);
        if (keyBound(InputAction::Use) && !m_ui.inventoryOpen) handleGameplayAction(true, action);
        return;
    }

    // ESC handling
    if (key == Key::Escape && action == ButtonAction::Press) {
        // Close inventory first if open
        if (m_ui.inventoryOpen) {
            m_flow.closeInventory();
            return;
        }

        if (m_flow.state() == GameState::Playing) {
            // Pause the game
            m_flow.pause();
        } else if (m_flow.state() == GameState::Paused) {
            // Resume (ESC in pause menu handled by menu itself)
            // But just in case the menu hasn't handled it:
            m_ui.menuCallbacks.onResume();
        }
        // In MainMenu, ESC does nothing
        return;
    }

    if (m_session.playerDead && action == ButtonAction::Press &&
        (key == Key::Enter || key == Key::Space)) {
        m_flow.respawnPlayer();
        return;
    }

}

void ApplicationInputRouter::handleTextEvent(std::string_view text) {
    for (const uint32_t codepoint : decodeUtf8(text)) {
        if (m_ui.commandOpen) {
            std::string encoded; appendUtf8(encoded, codepoint);
            m_ui.commandInput.insert(encoded);
        } else if (m_ui.activeMenu) {
            m_ui.activeMenu->onChar(codepoint);
        }
    }
}

void ApplicationInputRouter::handleMouseButtonEvent(
    int button, ButtonAction action, int mods) {
    if (m_inputs.uiTouch.active) return;
    if (action == ButtonAction::Press && m_settings.controlMode == ControlMode::Auto)
        m_inputs.touchHudVisible = false;
    auto mouseBound = [this, button](InputAction inputAction) {
        const auto& binding = m_settings.bindings[static_cast<size_t>(inputAction)];
        return binding.device == InputDevice::Mouse && binding.code == button;
    };
    m_ui.updateMouseScreenPosition(m_window);
    if (action == ButtonAction::Press && m_flow.state() == GameState::Playing && !m_ui.activeMenu) {
        if (mouseBound(InputAction::Command) && !m_ui.inventoryOpen && !m_session.playerDead) {
            m_flow.openCommandInput(); return;
        }
        if (mouseBound(InputAction::Inventory) && !m_ui.commandOpen && !m_session.player.isSpectator()) {
            if (m_ui.inventoryOpen) m_flow.closeInventory(); else m_flow.openInventory(); return;
        }
        if (mouseBound(InputAction::Perspective) && !m_ui.inventoryOpen && !m_ui.commandOpen) {
            cyclePerspective(); return;
        }
        if (mouseBound(InputAction::DropItem) && !m_ui.inventoryOpen && !m_ui.commandOpen &&
            !m_session.playerDead) { m_flow.dropSelectedItem(); return; }
        for (int slot = 0; slot < 9; ++slot) if (mouseBound(static_cast<InputAction>(
            static_cast<int>(InputAction::Hotbar1) + slot))) {
            m_ui.hotbar.selectSlot(slot);
            m_session.player.setSelectedSlot(slot);
        }
    }
    if (m_ui.inventoryOpen && (m_ui.playerInventoryViewOpen(m_session.player) || m_ui.containerOpen) &&
        (action == ButtonAction::Press || action == ButtonAction::Release)) {
        if (!m_ui.containerOpen && m_session.player.gameMode() == GameMode::Creative &&
            action == ButtonAction::Press &&
            m_ui.survivalInventory.creativeCatalogButtonContains(
                static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY))) {
            m_ui.openCreativeCatalog();
            return;
        }
        if (m_ui.containerOpen) m_ui.containerScreen.onMouseButton(
            button, action, static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY), mods);
        else m_ui.survivalInventory.onMouseButton(button, action,
            static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY), mods);
        return;
    }
    if (!m_ui.inventoryOpen && !m_ui.commandOpen && !m_ui.activeMenu &&
        m_flow.state() == GameState::Playing &&
        (mouseBound(InputAction::Attack) || mouseBound(InputAction::Use))) {
        if (mouseBound(InputAction::Attack)) handleGameplayAction(false, action);
        if (mouseBound(InputAction::Use) && !m_ui.inventoryOpen) handleGameplayAction(true, action);
        return;
    }
    if (action == ButtonAction::Press || action == ButtonAction::Release) {
        if (m_ui.inventoryOpen) {
            if (m_session.player.gameMode() == GameMode::Creative &&
                m_ui.creativeCatalogOpen && action == ButtonAction::Press) {
                m_ui.inventory.onMouseClick(button,
                    static_cast<int>(m_ui.mouseScreenX),
                    static_cast<int>(m_ui.mouseScreenY),
                    [this](ItemId id) {
                        m_flow.giveCreativeItem(id);
                    }, [this]() { m_flow.openPlayerInventoryView(); });
            }
        } else if (m_ui.activeMenu) {
            m_ui.activeMenu->onMouseButton(button, action,
                                        m_ui.mouseScreenX, m_ui.mouseScreenY);
        }
    }
}

void ApplicationInputRouter::handleScrollEvent(double, double yoffset) {
    if (m_ui.activeMenu) { m_ui.activeMenu->onScroll(yoffset); return; }
    if (m_ui.inventoryOpen && m_session.player.gameMode() == GameMode::Creative &&
        m_ui.creativeCatalogOpen && !m_ui.containerOpen) {
        m_ui.inventory.onScroll(yoffset);
        return;
    }
    auto wheelBound = [this, yoffset](InputAction action) {
        const auto& binding = m_settings.bindings[static_cast<size_t>(action)];
        return binding.device == InputDevice::Wheel && binding.code == (yoffset > 0 ? 1 : -1);
    };
    if (m_flow.state() == GameState::Playing && !m_ui.commandOpen) {
        if (wheelBound(InputAction::Inventory) && !m_session.player.isSpectator()) {
            if (m_ui.inventoryOpen) m_flow.closeInventory(); else m_flow.openInventory(); return;
        }
        if (wheelBound(InputAction::Command) && !m_ui.inventoryOpen && !m_session.playerDead) {
            m_flow.openCommandInput(); return;
        }
        if (wheelBound(InputAction::Perspective) && !m_ui.inventoryOpen) { cyclePerspective(); return; }
        if (wheelBound(InputAction::DropItem) && !m_ui.inventoryOpen && !m_session.playerDead) {
            m_flow.dropSelectedItem(); return;
        }
        for (int slot = 0; slot < 9; ++slot)
            if (wheelBound(static_cast<InputAction>(static_cast<int>(InputAction::Hotbar1) + slot)))
                m_ui.hotbar.selectSlot(slot);
        if (!m_ui.inventoryOpen) {
            if (wheelBound(InputAction::Attack)) {
                handleGameplayAction(false, ButtonAction::Press);
                handleGameplayAction(false, ButtonAction::Release);
            }
            if (wheelBound(InputAction::Use)) {
                handleGameplayAction(true, ButtonAction::Press);
                if (!m_ui.inventoryOpen) handleGameplayAction(true, ButtonAction::Release);
            }
        }
    }
    if (m_flow.state() == GameState::Playing && !m_ui.activeMenu &&
        !m_ui.inventoryOpen && !m_ui.commandOpen) {
        if (m_inputs.state.pressed(InputAction::PreviousSlot)) m_ui.hotbar.onScroll(1.0);
        if (m_inputs.state.pressed(InputAction::NextSlot)) m_ui.hotbar.onScroll(-1.0);
        m_session.player.setSelectedSlot(m_ui.hotbar.getSelectedSlot());
    }
}

void ApplicationInputRouter::dispatchTouchCommands(
    const std::vector<TouchCommandEvent>& commands) {
    if (m_ui.activeMenu) return;
    for (const auto& command : commands) {
        switch (command.command) {
            case TouchCommand::AttackPress: handleGameplayAction(false, ButtonAction::Press); break;
            case TouchCommand::AttackRelease: handleGameplayAction(false, ButtonAction::Release); break;
            case TouchCommand::UsePress: handleGameplayAction(true, ButtonAction::Press); break;
            case TouchCommand::UseRelease: handleGameplayAction(true, ButtonAction::Release); break;
            case TouchCommand::OpenInventory:
                if (!m_session.player.isSpectator()) {
                    handleGameplayAction(false, ButtonAction::Release);
                    handleGameplayAction(true, ButtonAction::Release);
                    m_inputs.touchControls.cancelAll();
                    m_flow.openInventory();
                }
                break;
            case TouchCommand::OpenCommand:
                if (!m_session.playerDead) {
                    handleGameplayAction(false, ButtonAction::Release);
                    handleGameplayAction(true, ButtonAction::Release);
                    m_inputs.touchControls.cancelAll();
                    m_flow.openCommandInput();
                }
                break;
            case TouchCommand::Pause:
                handleGameplayAction(false, ButtonAction::Release);
                handleGameplayAction(true, ButtonAction::Release);
                m_inputs.touchControls.cancelAll();
                m_flow.pause();
                break;
            case TouchCommand::ChangePerspective: cyclePerspective(); break;
            case TouchCommand::SelectHotbar:
                m_ui.hotbar.selectSlot(command.value);
                m_session.player.setSelectedSlot(command.value);
                break;
        }
    }
}

void ApplicationInputRouter::dispatchUiTouchButton(
    int button, ButtonAction action, const glm::vec2& position) {
    const int x = static_cast<int>(position.x), y = static_cast<int>(position.y);
    if (m_ui.inventoryOpen && (m_ui.playerInventoryViewOpen(m_session.player) || m_ui.containerOpen)) {
        if (!m_ui.containerOpen && m_session.player.gameMode() == GameMode::Creative &&
            action == ButtonAction::Press &&
            m_ui.survivalInventory.creativeCatalogButtonContains(x, y)) {
            m_ui.openCreativeCatalog(); return;
        }
        if (m_ui.containerOpen) m_ui.containerScreen.onMouseButton(button, action, x, y);
        else m_ui.survivalInventory.onMouseButton(button, action, x, y);
    } else if (m_ui.inventoryOpen) {
        if (action == ButtonAction::Press) m_ui.inventory.onMouseClick(button, x, y,
            [this](ItemId id) { m_flow.giveCreativeItem(id); },
            [this]() { m_flow.openPlayerInventoryView(); });
    } else if (m_ui.activeMenu) m_ui.activeMenu->onMouseButton(button, action, position.x, position.y);
    else if (m_session.playerDead && action == ButtonAction::Release) m_flow.respawnPlayer();
}

void ApplicationInputRouter::dispatchUiTouchMove(const glm::vec2& position) {
    const int x = static_cast<int>(position.x), y = static_cast<int>(position.y);
    if (m_ui.inventoryOpen) {
        m_ui.inventory.onMouseMove(x, y);
        if (m_ui.containerOpen) m_ui.containerScreen.onMouseMove(x, y);
        else if (m_ui.playerInventoryViewOpen(m_session.player)) m_ui.survivalInventory.onMouseMove(x, y);
    }
    if (m_ui.activeMenu) m_ui.activeMenu->onMouseMove(position.x, position.y);
}

void ApplicationInputRouter::handleUiTouch(const TouchEvent& event,
                                           const glm::vec2& position) {
    if (event.phase == TouchPhase::Begin) {
        if (m_inputs.uiTouch.active) return;
        m_inputs.uiTouch = {event.id, position, position, m_clock.now(), true, false, false, false};
        dispatchUiTouchMove(position);
        if (m_ui.activeMenu && m_ui.activeMenu->capturesPointerDrag(
                position.x, position.y)) {
            dispatchUiTouchButton(
                MouseButton::Left, ButtonAction::Press, position);
            m_inputs.uiTouch.buttonDown = true;
        }
        return;
    }
    if (!m_inputs.uiTouch.active || event.id != m_inputs.uiTouch.id) return;
    if (event.phase == TouchPhase::Move) {
        m_inputs.uiTouch.position = position;
        const glm::vec2 delta = position - m_inputs.uiTouch.origin;
        const bool scrollSurface = m_ui.activeMenu || (m_ui.inventoryOpen &&
            m_session.player.gameMode() == GameMode::Creative &&
            m_ui.creativeCatalogOpen && !m_ui.containerOpen);
        if (scrollSurface && !m_inputs.uiTouch.buttonDown && std::abs(delta.y) > 24.0f) {
            const double scroll = delta.y > 0.0f ? -1.0 : 1.0;
            if (m_ui.activeMenu) m_ui.activeMenu->onScroll(scroll); else m_ui.inventory.onScroll(scroll);
            m_inputs.uiTouch.origin = position; m_inputs.uiTouch.scrolling = true;
        } else if (!scrollSurface && !m_inputs.uiTouch.buttonDown && glm::length(delta) > 8.0f) {
            dispatchUiTouchButton(MouseButton::Left, ButtonAction::Press, m_inputs.uiTouch.origin);
            m_inputs.uiTouch.buttonDown = true;
        }
        dispatchUiTouchMove(position); return;
    }
    if (event.phase == TouchPhase::End) {
        if (m_inputs.uiTouch.buttonDown) dispatchUiTouchButton(
            m_inputs.uiTouch.rightButton ? MouseButton::Right : MouseButton::Left,
            ButtonAction::Release, m_inputs.uiTouch.position);
        else if (!m_inputs.uiTouch.scrolling) {
            dispatchUiTouchButton(MouseButton::Left, ButtonAction::Press, m_inputs.uiTouch.position);
            dispatchUiTouchButton(MouseButton::Left, ButtonAction::Release, m_inputs.uiTouch.position);
        }
        m_inputs.uiTouch = {};
    }
}

void ApplicationInputRouter::updateLongPress() {
    if (!m_inputs.uiTouch.active || m_inputs.uiTouch.buttonDown || m_inputs.uiTouch.scrolling ||
        !m_ui.inventoryOpen || (!m_ui.playerInventoryViewOpen(m_session.player) && !m_ui.containerOpen))
        return;
    if (RuntimeClock::seconds(RuntimeClock::elapsed(m_inputs.uiTouch.started, m_clock.now())) < .45)
        return;
    dispatchUiTouchButton(MouseButton::Right, ButtonAction::Press, m_inputs.uiTouch.position);
    m_inputs.uiTouch.buttonDown = true; m_inputs.uiTouch.rightButton = true;
}

void ApplicationInputRouter::handleTouch(const TouchEvent& event) {
    if (event.phase == TouchPhase::Cancel) {
        handleGameplayAction(false, ButtonAction::Release);
        handleGameplayAction(true, ButtonAction::Release);
        m_inputs.touchControls.cancelAll(); m_inputs.touchGameplay.clear();
        if (m_inputs.uiTouch.active && m_inputs.uiTouch.buttonDown) dispatchUiTouchButton(
            m_inputs.uiTouch.rightButton ? MouseButton::Right : MouseButton::Left,
            ButtonAction::Release, m_inputs.uiTouch.position);
        m_inputs.uiTouch = {}; return;
    }
    const WindowSafeArea safe = m_window.safeArea();
    m_inputs.touchControls.configure(
        std::max(1, safe.width / std::max(1, m_ui.guiScale)),
        std::max(1, safe.height / std::max(1, m_ui.guiScale)), touchConfig());
    glm::vec2 position = event.phase == TouchPhase::End && m_inputs.uiTouch.active &&
                         event.id == m_inputs.uiTouch.id
        ? m_inputs.uiTouch.position : m_ui.touchToUi(m_window, event.x, event.y);
    if (event.phase == TouchPhase::Begin && m_ui.inventoryOpen &&
        touchUiVisible() &&
        touchInventoryCloseRect(
            std::max(1, safe.width / std::max(1, m_ui.guiScale)),
            std::max(1, safe.height / std::max(1, m_ui.guiScale))).contains(position.x, position.y)) {
        m_flow.closeInventory(); return;
    }
    bool gameplay = false;
    if (event.phase == TouchPhase::Begin) {
        gameplay = m_flow.state() == GameState::Playing && !m_ui.inventoryOpen &&
            !m_ui.activeMenu && !m_ui.commandOpen && !m_session.playerDead &&
            m_settings.controlMode != ControlMode::KeyboardMouse;
        m_inputs.touchGameplay[event.id] = gameplay;
    } else {
        const auto it = m_inputs.touchGameplay.find(event.id);
        gameplay = it != m_inputs.touchGameplay.end() && it->second;
    }
    if (gameplay) {
        m_inputs.touchHudVisible = true;
        TouchEvent converted = event; converted.x = position.x; converted.y = position.y;
        dispatchTouchCommands(m_inputs.touchControls.onTouch(converted));
        // Preserve press/release edges even when a quick tap begins and
        // ends within one event-poll call.
        m_inputs.state.clearVirtual();
        m_inputs.touchControls.applyTo(m_inputs.state);
        m_inputs.state.update(m_settings.bindings);
    } else handleUiTouch(event, position);
    if (event.phase == TouchPhase::End) m_inputs.touchGameplay.erase(event.id);
}

void ApplicationInputRouter::handleGameplayAction(bool use, ButtonAction action) {
    if (m_ui.activeMenu) return;
    const int logicalButton = use ? MouseButton::Right : MouseButton::Left;
    if (action == ButtonAction::Press && use && !m_session.player.isSpectator()) {
        auto hit = m_session.world.raycast(m_session.player.getEyePosition(), m_session.player.getForward(),
                                   Config::REACH_DISTANCE);
        if (hit) {
            const BlockId target = m_session.world.getBlock(
                hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
            if (target == BlockId::CRAFTING_TABLE && m_session.player.isSurvival()) {
                m_flow.openInventory();
                m_ui.survivalInventory.setCraftingTable(true);
                return;
            }
            if (target == BlockId::CHEST || target == BlockId::FURNACE) {
                if (m_ui.containerScreen.open(m_session.world, hit->blockPos)) {
                    m_ui.containerOpen = true;
                    m_ui.inventoryOpen = true;
                    m_window.setCursorLocked(false);
                    return;
                }
            }
        }
    }
    m_session.player.handleMouseButton(logicalButton, action);
}

void ApplicationInputRouter::updateGamepadUi(RuntimeClock::Tick now) {
    auto* settings = dynamic_cast<SettingsMenu*>(m_ui.activeMenu.get());
    if (settings && settings->capturingGamepad()) {
        bool centered = true;
        for (float axis : m_inputs.gamepadAxes) if (std::abs(axis) > .25f) centered = false;
        if (centered) m_inputs.gamepadCaptureArmed = true;
        for (size_t i = 0; i < m_inputs.gamepadButtons.size(); ++i)
            if (m_inputs.gamepadButtons[i] && !m_inputs.previousGamepadButtons[i]) {
                if (i == 4) settings->onKeyPress(Key::Escape);
                else settings->onGamepadBinding({GamepadBindingType::Button, static_cast<int>(i)});
                m_inputs.gamepadCaptureArmed = false; break;
            }
        if (m_inputs.gamepadCaptureArmed && settings->capturingGamepad())
            for (size_t i = 0; i < m_inputs.gamepadAxes.size(); ++i) {
                if (std::abs(m_inputs.gamepadAxes[i]) > .65f) {
                    settings->onGamepadBinding({m_inputs.gamepadAxes[i] > 0
                        ? GamepadBindingType::AxisPositive : GamepadBindingType::AxisNegative,
                        static_cast<int>(i)});
                    m_inputs.gamepadCaptureArmed = false; break;
                }
            }
        m_inputs.previousGamepadButtons = m_inputs.gamepadButtons; return;
    }
    m_inputs.gamepadCaptureArmed = false;
    const bool pressA = m_inputs.gamepadButtons[0] && !m_inputs.previousGamepadButtons[0];
    const bool pressB = m_inputs.gamepadButtons[1] && !m_inputs.previousGamepadButtons[1];
    const bool pressX = m_inputs.gamepadButtons[2] && !m_inputs.previousGamepadButtons[2];
    const bool pressY = m_inputs.gamepadButtons[3] && !m_inputs.previousGamepadButtons[3];
    int navX = (m_inputs.gamepadButtons[14] || m_inputs.gamepadAxes[0] > .65f) ? 1 :
               (m_inputs.gamepadButtons[13] || m_inputs.gamepadAxes[0] < -.65f) ? -1 : 0;
    int navY = (m_inputs.gamepadButtons[12] || m_inputs.gamepadAxes[1] > .65f) ? 1 :
               (m_inputs.gamepadButtons[11] || m_inputs.gamepadAxes[1] < -.65f) ? -1 : 0;
    bool navigate = navX != m_inputs.gamepadNavX || navY != m_inputs.gamepadNavY;
    if ((navX || navY) && now >= m_inputs.gamepadRepeatTick) {
        navigate = true;
        m_inputs.gamepadRepeatTick = now + RuntimeClock::fromSeconds(.12);
    }
    if ((navX != m_inputs.gamepadNavX || navY != m_inputs.gamepadNavY) && (navX || navY))
        m_inputs.gamepadRepeatTick = now + RuntimeClock::fromSeconds(.35);
    m_inputs.gamepadNavX = navX; m_inputs.gamepadNavY = navY;
    if (m_ui.inventoryOpen) {
        if (navigate) {
            if (m_ui.containerOpen) m_ui.containerScreen.onGamepadNavigate(navX, -navY);
            else if (m_ui.playerInventoryViewOpen(m_session.player)) m_ui.survivalInventory.onGamepadNavigate(navX, -navY);
            else m_ui.inventory.onGamepadNavigate(navX, navY);
        }
        if (m_ui.containerOpen) {
            if (pressA) m_ui.containerScreen.onGamepadAction(0);
            if (pressX) m_ui.containerScreen.onGamepadAction(1);
            if (pressY) m_ui.containerScreen.onGamepadAction(2);
        } else if (m_ui.playerInventoryViewOpen(m_session.player)) {
            if (pressA) m_ui.survivalInventory.onGamepadAction(0);
            if (pressY) m_ui.survivalInventory.onGamepadAction(2);
            if (pressX && m_session.player.gameMode() == GameMode::Creative) { m_ui.openCreativeCatalog(); }
            else if (pressX) m_ui.survivalInventory.onGamepadAction(1);
        } else {
            if (pressA) m_ui.inventory.onGamepadAction(true, [this](ItemId id) { m_flow.giveCreativeItem(id); });
            if (pressX) m_flow.openPlayerInventoryView();
        }
        if (pressB) m_flow.closeInventory();
    } else if (m_ui.activeMenu) {
        if (navigate) {
            if (navY < 0) m_ui.activeMenu->onKeyPress(Key::Up);
            else if (navY > 0) m_ui.activeMenu->onKeyPress(Key::Down);
            else if (navX < 0) m_ui.activeMenu->onKeyPress(Key::Left);
            else if (navX > 0) m_ui.activeMenu->onKeyPress(Key::Right);
        }
        if (pressA) m_ui.activeMenu->onKeyPress(Key::Enter);
        if (pressB) m_ui.activeMenu->onKeyPress(Key::Escape);
    }
    m_inputs.previousGamepadButtons = m_inputs.gamepadButtons;
}

void ApplicationInputRouter::cyclePerspective() {
    m_scene.cyclePerspective();
}
