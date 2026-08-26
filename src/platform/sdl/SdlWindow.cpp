#include "core/Window.h"

#include "debug/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace {
SDL_Window* sdlWindow(void* window) {
    return static_cast<SDL_Window*>(window);
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

Window::Window(
    int width, int height, const std::string& title, SurfaceMode surfaceMode,
    bool synchronizePresentation,
    bool highPixelDensity)
    : m_synchronizePresentation(synchronizePresentation),
      m_surfaceMode(surfaceMode) {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
#else
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
#endif
    SDL_SetAppMetadata(
        "MinecraftC", MINECRAFTC_VERSION_STRING, "io.github.SafeFain.MinecraftC");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_FATAL("Failed to initialize SDL: " << SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
    }
    m_gamepads = std::make_unique<GamepadManager>();

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (highPixelDensity) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (m_surfaceMode == SurfaceMode::Vulkan) flags |= SDL_WINDOW_VULKAN;
    m_window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!m_window) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        LOG_FATAL("Failed to create SDL window: " << error);
        throw std::runtime_error("Failed to create window: " + error);
    }
    refreshSizes();
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
    if (m_textInputEnabled && m_window) SDL_StopTextInput(sdlWindow(m_window));
    if (m_window) SDL_DestroyWindow(sdlWindow(m_window));
    m_gamepads.reset();
    SDL_Quit();
}

void Window::refreshSizes() {
    SDL_GetWindowSize(sdlWindow(m_window), &m_windowWidth, &m_windowHeight);
    SDL_GetWindowSizeInPixels(sdlWindow(m_window), &m_pixelWidth, &m_pixelHeight);
    m_windowWidth = std::max(1, m_windowWidth);
    m_windowHeight = std::max(1, m_windowHeight);
}

void Window::refreshSizesAndNotify() {
    const int previousPixelWidth = m_pixelWidth;
    const int previousPixelHeight = m_pixelHeight;
    refreshSizes();
    if ((m_pixelWidth != previousPixelWidth ||
         m_pixelHeight != previousPixelHeight) && m_resizeCallback)
        m_resizeCallback(m_pixelWidth, m_pixelHeight);
}

void Window::finishEventFrame() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    // Mobile surfaces may resize or rotate without a matching SDL resize event.
    refreshSizesAndNotify();
#endif
    resetEventFrame();
}

void Window::resetEventFrame() {
    m_cursorDeltaX = 0.0;
    m_cursorDeltaY = 0.0;
}

void Window::processEvent(const void* opaqueEvent) {
    const auto& event = *static_cast<const SDL_Event*>(opaqueEvent);
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            m_shouldClose = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_DISPLAY_ORIENTATION:
            refreshSizesAndNotify();
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            m_minimized = true;
            break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
            m_minimized = false;
            refreshSizesAndNotify();
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
        case SDL_EVENT_SCREEN_KEYBOARD_SHOWN:
            if (m_screenKeyboardCallback) m_screenKeyboardCallback(true);
            break;
        case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
            if (m_screenKeyboardCallback) m_screenKeyboardCallback(false);
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
        case SDL_EVENT_GAMEPAD_ADDED:
            m_gamepads->deviceAdded(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            m_gamepads->deviceRemoved(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            m_gamepads->deviceActive(event.gbutton.which);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (std::abs(event.gaxis.value) > 8000)
                m_gamepads->deviceActive(event.gaxis.which);
            break;
        default:
            break;
    }
}

void Window::setTitle(const std::string& title) {
    SDL_SetWindowTitle(sdlWindow(m_window), title.c_str());
}

bool Window::isKeyPressed(int key) const {
    return key >= 0 && key < static_cast<int>(m_keys.size()) &&
        m_keys[static_cast<size_t>(key)];
}

std::vector<std::string> Window::requiredVulkanInstanceExtensions() const {
    if (m_surfaceMode != SurfaceMode::Vulkan)
        throw std::runtime_error("Vulkan extensions requested for an input-only window");
    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions)
        throw std::runtime_error(std::string("Failed to query Vulkan extensions: ") +
                                 SDL_GetError());
    std::vector<std::string> result;
    result.reserve(count);
    for (Uint32 i = 0; i < count; ++i) result.emplace_back(extensions[i]);
    return result;
}

std::uintptr_t Window::createVulkanSurface(void* instance) const {
    if (m_surfaceMode != SurfaceMode::Vulkan || !m_window)
        throw std::runtime_error("Cannot create a surface for an input-only window");
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(
            sdlWindow(m_window), reinterpret_cast<VkInstance>(instance), nullptr,
            &surface))
        throw std::runtime_error(std::string("Failed to create Vulkan surface: ") +
                                 SDL_GetError());
    return reinterpret_cast<std::uintptr_t>(surface);
}

WindowSafeArea Window::safeArea() const {
    SDL_Rect area{};
    if (!SDL_GetWindowSafeArea(sdlWindow(m_window), &area))
        return {0, 0, m_pixelWidth, m_pixelHeight};
    return projectWindowSafeArea(area.x, area.y, area.w, area.h,
                                 m_windowWidth, m_windowHeight,
                                 m_pixelWidth, m_pixelHeight);
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
    if (!SDL_SetWindowRelativeMouseMode(sdlWindow(m_window), locked))
        LOG_WARN("Could not change relative mouse mode: " << SDL_GetError());
}

bool Window::isFullscreen() const {
    return (SDL_GetWindowFlags(sdlWindow(m_window)) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::toggleFullscreen() {
    if (!SDL_SetWindowFullscreen(sdlWindow(m_window), !isFullscreen()))
        LOG_WARN("Could not toggle fullscreen: " << SDL_GetError());
}

void Window::setTextInputEnabled(bool enabled) {
    if (enabled == m_textInputEnabled) return;
    const bool changed = enabled ? SDL_StartTextInput(sdlWindow(m_window))
                                 : SDL_StopTextInput(sdlWindow(m_window));
    if (!changed) {
        LOG_WARN("Could not change SDL text input state: " << SDL_GetError());
        return;
    }
    m_textInputEnabled = enabled;
}

bool Window::openUrl(const std::string& url) const {
    if (SDL_OpenURL(url.c_str())) return true;
    LOG_WARN("Could not open URL '" << url << "': " << SDL_GetError());
    return false;
}
