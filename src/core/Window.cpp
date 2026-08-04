#include "core/Window.h"

#include "Config.h"
#include "debug/Log.h"

#include <SDL3/SDL.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void configureOpenGLAttributes(bool srgb, int samples) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, srgb ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
}

int projectMouseButton(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return MouseButton::Left;
        case SDL_BUTTON_RIGHT: return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1: return 3;
        case SDL_BUTTON_X2: return 4;
        default: return -1;
    }
}

int projectModifiers(SDL_Keymod modifiers) {
    int result = 0;
    if ((modifiers & SDL_KMOD_SHIFT) != 0) result |= KeyModifier::Shift;
    if ((modifiers & SDL_KMOD_CTRL) != 0) result |= KeyModifier::Control;
    if ((modifiers & SDL_KMOD_ALT) != 0) result |= KeyModifier::Alt;
    if ((modifiers & SDL_KMOD_GUI) != 0) result |= KeyModifier::Super;
    return result;
}
}

Window::Window(int width, int height, const std::string& title) {
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    SDL_SetAppMetadata(
        "MinecraftC", MINECRAFTC_VERSION_STRING, "io.github.SafeFain.MinecraftC");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_FATAL("Failed to initialize SDL: " << SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
    }
    m_gamepads = std::make_unique<GamepadManager>();

    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_HIGH_PIXEL_DENSITY;
    struct VisualRequest {
        bool srgb;
        int samples;
    };
    const VisualRequest requests[] = {
        {true, Config::MSAA_SAMPLES},
        {false, Config::MSAA_SAMPLES},
        {true, 0},
        {false, 0},
    };
    std::string firstError;
    VisualRequest selected = requests[0];
    for (const VisualRequest& request : requests) {
        configureOpenGLAttributes(request.srgb, request.samples);
        m_window = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (m_window) {
            selected = request;
            break;
        }
        if (firstError.empty()) firstError = SDL_GetError();
    }
    if (!m_window) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        LOG_FATAL("Failed to create SDL window: " << error);
        throw std::runtime_error("Failed to create SDL window");
    }
    if (!selected.srgb || selected.samples != Config::MSAA_SAMPLES) {
        LOG_WARN("Preferred OpenGL visual unavailable (" << firstError
                 << "); using " << selected.samples << "x MSAA with "
                 << (selected.srgb ? "sRGB required" : "sRGB optional"));
    }
    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context || !SDL_GL_MakeCurrent(
            m_window, static_cast<SDL_GLContext>(m_context))) {
        const std::string error = SDL_GetError();
        if (m_context) SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_context));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_Quit();
        LOG_FATAL("Failed to create SDL OpenGL context: " << error);
        throw std::runtime_error("Failed to create OpenGL context");
    }
    if (!SDL_GL_SetSwapInterval(1))
        LOG_WARN("Could not enable VSync: " << SDL_GetError());
    refreshSizes();
    int srgb = 0;
    m_srgbCapable = SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &srgb) && srgb != 0;
    float x = 0.0f, y = 0.0f;
    SDL_GetMouseState(&x, &y);
    m_cursorX = x;
    m_cursorY = y;
    int touchCount = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&touchCount);
    m_touchAvailable = touchCount > 0;
    SDL_free(devices);
}

Window::~Window() {
    if (m_textInputEnabled && m_window) SDL_StopTextInput(m_window);
    if (m_context) SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_context));
    if (m_window) SDL_DestroyWindow(m_window);
    m_gamepads.reset();
    SDL_Quit();
}

void Window::refreshSizes() {
    SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
    SDL_GetWindowSizeInPixels(m_window, &m_pixelWidth, &m_pixelHeight);
    m_windowWidth = std::max(1, m_windowWidth);
    m_windowHeight = std::max(1, m_windowHeight);
}

void Window::resetEventFrame() {
    m_cursorDeltaX = 0.0;
    m_cursorDeltaY = 0.0;
}

void Window::processEvent(const void* opaqueEvent) {
    const auto& event = *static_cast<const SDL_Event*>(opaqueEvent);
    if (m_gamepads) m_gamepads->processEvent(event);
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            m_shouldClose = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            refreshSizes();
            glViewport(0, 0, m_pixelWidth, m_pixelHeight);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            m_minimized = true;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
            m_minimized = false;
            refreshSizes();
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            m_keys.fill(false);
            m_mouse.fill(false);
            if (m_focusCallback) m_focusCallback(false);
            if (m_touchCallback) m_touchCallback({{}, TouchPhase::Cancel, 0.0, 0.0});
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (m_focusCallback) m_focusCallback(true);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const int key = static_cast<int>(event.key.scancode);
            const ButtonAction action = event.type == SDL_EVENT_KEY_UP
                ? ButtonAction::Release
                : (event.key.repeat ? ButtonAction::Repeat : ButtonAction::Press);
            if (key >= 0 && key < static_cast<int>(m_keys.size()))
                m_keys[static_cast<size_t>(key)] = action != ButtonAction::Release;
            if (m_keyCallback)
                m_keyCallback(key, key, action, projectModifiers(event.key.mod));
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (m_charCallback) m_charCallback(event.text.text);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (event.motion.which == SDL_TOUCH_MOUSEID ||
                event.motion.which == SDL_PEN_MOUSEID) break;
            m_cursorX = event.motion.x;
            m_cursorY = event.motion.y;
            m_cursorDeltaX += event.motion.xrel;
            m_cursorDeltaY += event.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event.button.which == SDL_TOUCH_MOUSEID ||
                event.button.which == SDL_PEN_MOUSEID) break;
            m_cursorX = event.button.x;
            m_cursorY = event.button.y;
            const int button = projectMouseButton(event.button.button);
            if (button < 0) break;
            const ButtonAction action = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? ButtonAction::Press : ButtonAction::Release;
            m_mouse[static_cast<size_t>(button)] = action == ButtonAction::Press;
            if (m_mouseButtonCallback)
                m_mouseButtonCallback(button, action, projectModifiers(SDL_GetModState()));
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (event.wheel.which != SDL_TOUCH_MOUSEID &&
                event.wheel.which != SDL_PEN_MOUSEID && m_scrollCallback) {
                const double direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                    ? -1.0 : 1.0;
                m_scrollCallback(event.wheel.x * direction, event.wheel.y * direction);
            }
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            m_touchAvailable = true;
            if (!m_touchCallback) break;
            TouchPhase phase = TouchPhase::Move;
            if (event.type == SDL_EVENT_FINGER_DOWN) phase = TouchPhase::Begin;
            else if (event.type == SDL_EVENT_FINGER_UP) phase = TouchPhase::End;
            else if (event.type == SDL_EVENT_FINGER_CANCELED) phase = TouchPhase::Cancel;
            m_touchCallback({
                {static_cast<uint64_t>(event.tfinger.touchID),
                 static_cast<uint64_t>(event.tfinger.fingerID)},
                phase,
                static_cast<double>(event.tfinger.x * m_windowWidth),
                static_cast<double>(event.tfinger.y * m_windowHeight)});
            break;
        }
        default:
            break;
    }
}

void Window::swapBuffers() { SDL_GL_SwapWindow(m_window); }
void Window::setTitle(const std::string& title) { SDL_SetWindowTitle(m_window, title.c_str()); }

bool Window::isKeyPressed(int key) const {
    return key >= 0 && key < static_cast<int>(m_keys.size()) &&
        m_keys[static_cast<size_t>(key)];
}

bool Window::isMouseButtonPressed(int button) const {
    return button >= 0 && button < static_cast<int>(m_mouse.size()) &&
        m_mouse[static_cast<size_t>(button)];
}

void Window::getCursorDelta(double& dx, double& dy) {
    dx = m_cursorDeltaX;
    dy = m_cursorDeltaY;
}

void Window::getCursorPos(double& x, double& y) const {
    x = m_cursorX;
    y = m_cursorY;
}

void Window::setCursorLocked(bool locked) {
    m_cursorLocked = locked;
    m_cursorDeltaX = 0.0;
    m_cursorDeltaY = 0.0;
    if (!SDL_SetWindowRelativeMouseMode(m_window, locked))
        LOG_WARN("Could not change relative mouse mode: " << SDL_GetError());
}

void Window::setTextInputEnabled(bool enabled) {
    if (enabled == m_textInputEnabled) return;
    const bool changed = enabled ? SDL_StartTextInput(m_window) : SDL_StopTextInput(m_window);
    if (!changed) {
        LOG_WARN("Could not change SDL text input state: " << SDL_GetError());
        return;
    }
    m_textInputEnabled = enabled;
}

void* Window::glProcAddress(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}
