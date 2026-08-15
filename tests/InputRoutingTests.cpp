#include "app/ApplicationInputController.h"
#include "core/Input.h"
#include "core/InputCodes.h"
#include "core/Window.h"
#include "game/ClientSettings.h"
#include "game/Localization.h"
#include "ui/TouchControls.h"
#include "ui/UIRenderer.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

SDL_Event keyEvent(SDL_EventType type, SDL_Scancode scancode,
                   bool repeat = false) {
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    event.key.repeat = repeat;
    event.key.mod = 0;
    return event;
}

SDL_Event mouseButtonEvent(SDL_EventType type, Uint8 button) {
    SDL_Event event{};
    event.type = type;
    event.button.button = button;
    event.button.which = 0;
    event.button.x = 10;
    event.button.y = 20;
    return event;
}
}

// TouchControls::render is linked into this logic test, but no OpenGL-backed
// renderer is constructed. These inert definitions keep the test focused on
// input capture and routing (same pattern as ClientInputTests).
void UIRenderer::drawRect(float, float, float, float, const glm::vec4&) {}
void UIRenderer::renderText(const std::string&, float, float, float,
                            const glm::vec3&) {}
glm::vec2 UIRenderer::measureText(const std::string&, float) { return {0, 0}; }
std::string Localization::text(std::string_view key) const {
    return std::string(key);
}

int main() {
    // Prefer the platform's default video driver (desktop sessions on
    // Windows/macOS CI and X11/Wayland hosts). Headless Linux CI falls back
    // to the offscreen driver, which provides a real SDL window and OpenGL
    // context where EGL is available. If no video backend can create a
    // window, the test is skipped instead of failing the suite.
    std::unique_ptr<Window> window;
    try {
        window = std::make_unique<Window>(
            640, 480, "input routing test", 0, GraphicsApi::OpenGL33, true,
            false);
    } catch (const std::exception&) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        try {
            window = std::make_unique<Window>(
                640, 480, "input routing test", 0, GraphicsApi::OpenGL33,
                true, false);
        } catch (const std::exception&) {
            std::cout << "SKIP: no SDL video driver can create a window\n";
            return 0;
        }
    }
    Window& windowRef = *window;

    ClientSettings settings;
    int keyCalls = 0;
    int textCalls = 0;
    int mouseCalls = 0;
    int scrollCalls = 0;
    int touchCalls = 0;
    int screenKeyboardCalls = 0;
    int lastKey = -1;
    ButtonAction lastKeyAction = ButtonAction::Release;
    std::string lastText;
    int lastButton = -1;
    ButtonAction lastButtonAction = ButtonAction::Release;
    double lastScroll = 0.0;
    TouchPhase lastTouchPhase = TouchPhase::End;
    bool lastScreenKeyboard = false;

    {
        ApplicationInputController controller;
        controller.bind(windowRef, settings, {
            [&](int key, int, ButtonAction action, int) {
                ++keyCalls;
                lastKey = key;
                lastKeyAction = action;
            },
            [&](std::string_view text) {
                ++textCalls;
                lastText = std::string(text);
            },
            [&](int button, ButtonAction action, int) {
                ++mouseCalls;
                lastButton = button;
                lastButtonAction = action;
            },
            [&](double, double y) {
                ++scrollCalls;
                lastScroll = y;
            },
            [&](const TouchEvent& event) {
                ++touchCalls;
                lastTouchPhase = event.phase;
            },
            [&](bool visible) {
                ++screenKeyboardCalls;
                lastScreenKeyboard = visible;
            },
        });

        // Key press routes into InputState and the narrow callback.
        SDL_Event down = keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W);
        windowRef.handleEvent(&down);
        require(controller.state.held(InputAction::MoveForward) &&
                    controller.state.pressed(InputAction::MoveForward),
                "bound key press exposes held and pressed state");
        require(controller.keys[Key::W],
                "key tracking records the pressed key");
        require(keyCalls == 1 && lastKey == Key::W &&
                    lastKeyAction == ButtonAction::Press,
                "key callback receives the physical key and press edge");

        // Key repeat is forwarded as a repeat edge.
        SDL_Event repeat = keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, true);
        windowRef.handleEvent(&repeat);
        require(keyCalls == 2 && lastKeyAction == ButtonAction::Repeat,
                "key callback forwards repeat edges");

        // beginFrame clears edges but keeps held state.
        TouchControlConfig touchConfig;
        controller.beginFrame(windowRef, settings, touchConfig, 1, false);
        require(controller.state.held(InputAction::MoveForward) &&
                    !controller.state.pressed(InputAction::MoveForward),
                "beginFrame clears press edges without losing held state");

        // Key release exposes the release edge and clears key tracking.
        SDL_Event up = keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_W);
        windowRef.handleEvent(&up);
        require(controller.state.released(InputAction::MoveForward) &&
                    !controller.state.held(InputAction::MoveForward),
                "bound key release exposes the release edge");
        require(!controller.keys[Key::W],
                "key tracking clears the released key");
        require(keyCalls == 3 && lastKeyAction == ButtonAction::Release,
                "key callback receives the release edge");

        // Alt key tracking drives altPressed().
        SDL_Event altDown = keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LALT);
        windowRef.handleEvent(&altDown);
        require(controller.altPressed(), "alt press is tracked");
        SDL_Event altUp = keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_LALT);
        windowRef.handleEvent(&altUp);
        require(!controller.altPressed(), "alt release clears tracking");

        // Text input forwards to the narrow text callback.
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.text = "hi";
        windowRef.handleEvent(&text);
        require(textCalls == 1 && lastText == "hi",
                "text input forwards to the text callback");

        // Mouse button presses route into InputState and the callback.
        SDL_Event mouseDown = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN,
                                               SDL_BUTTON_LEFT);
        windowRef.handleEvent(&mouseDown);
        require(mouseCalls == 1 && lastButton == MouseButton::Left &&
                    lastButtonAction == ButtonAction::Press,
                "mouse callback receives the button and press edge");
        SDL_Event mouseUp = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP,
                                             SDL_BUTTON_LEFT);
        windowRef.handleEvent(&mouseUp);
        require(lastButtonAction == ButtonAction::Release,
                "mouse callback receives the release edge");

        // Physical mouse input is suppressed while a UI touch is active.
        controller.uiTouch = {};
        controller.uiTouch.id = {1, 2};
        controller.uiTouch.position = {0.0f, 0.0f};
        controller.uiTouch.origin = {0.0f, 0.0f};
        controller.uiTouch.active = true;
        windowRef.handleEvent(&mouseDown);
        require(mouseCalls == 2,
                "physical mouse is suppressed while UI touch is active");
        controller.uiTouch = {};

        // Wheel events forward to the narrow scroll callback.
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.which = 0;
        wheel.wheel.y = 1;
        wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        windowRef.handleEvent(&wheel);
        require(scrollCalls == 1 && lastScroll == 1.0,
                "scroll callback receives the wheel delta");

        // Touch events forward with their phase preserved.
        SDL_Event touchDown{};
        touchDown.type = SDL_EVENT_FINGER_DOWN;
        touchDown.tfinger.touchID = 1;
        touchDown.tfinger.fingerID = 2;
        touchDown.tfinger.x = 0.5f;
        touchDown.tfinger.y = 0.25f;
        windowRef.handleEvent(&touchDown);
        require(touchCalls == 1 && lastTouchPhase == TouchPhase::Begin,
                "touch callback receives the begin phase");
        SDL_Event touchUp{};
        touchUp.type = SDL_EVENT_FINGER_UP;
        touchUp.tfinger.touchID = 1;
        touchUp.tfinger.fingerID = 2;
        windowRef.handleEvent(&touchUp);
        require(lastTouchPhase == TouchPhase::End,
                "touch callback receives the end phase");

        // Screen-keyboard visibility routes to the narrow callback.
        SDL_Event keyboardShown{};
        keyboardShown.type = SDL_EVENT_SCREEN_KEYBOARD_SHOWN;
        windowRef.handleEvent(&keyboardShown);
        require(screenKeyboardCalls == 1 && lastScreenKeyboard,
                "screen-keyboard shown routes to the callback");
        SDL_Event keyboardHidden{};
        keyboardHidden.type = SDL_EVENT_SCREEN_KEYBOARD_HIDDEN;
        windowRef.handleEvent(&keyboardHidden);
        require(screenKeyboardCalls == 2 && !lastScreenKeyboard,
                "screen-keyboard hidden routes to the callback");

        // Focus loss clears held physical input inside the controller and
        // cancels in-flight touches; the application needs no forwarding.
        SDL_Event held = keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W);
        windowRef.handleEvent(&held);
        require(controller.state.held(InputAction::MoveForward),
                "held action is active before focus loss");
        const int touchesBeforeFocusLoss = touchCalls;
        SDL_Event focusLost{};
        focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        windowRef.handleEvent(&focusLost);
        require(!controller.state.held(InputAction::MoveForward) &&
                    controller.state.released(InputAction::MoveForward),
                "focus loss releases held actions");
        require(!controller.keys[Key::W],
                "focus loss clears key tracking");
        require(touchCalls == touchesBeforeFocusLoss + 1 &&
                    lastTouchPhase == TouchPhase::Cancel,
                "focus loss cancels in-flight touches");
        SDL_Event focusGained{};
        focusGained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        windowRef.handleEvent(&focusGained);
        require(keyCalls == 6,
                "focus gain forwards no synthetic key events");
    }

    std::cout << "Application input routing tests passed\n";
}
