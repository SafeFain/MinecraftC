#pragma once

#include "core/InputCodes.h"
#include "core/Touch.h"
#include "core/GamepadManager.h"
#include "core/GraphicsApi.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct WindowSafeArea {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
};

inline WindowSafeArea projectWindowSafeArea(
    int areaX, int areaY, int areaWidth, int areaHeight,
    int windowWidth, int windowHeight, int pixelWidth, int pixelHeight) {
    const double scaleX = windowWidth > 0
        ? static_cast<double>(pixelWidth) / windowWidth : 1.0;
    const double scaleY = windowHeight > 0
        ? static_cast<double>(pixelHeight) / windowHeight : 1.0;
    const int x = static_cast<int>(areaX * scaleX + 0.5);
    const int top = static_cast<int>(areaY * scaleY + 0.5);
    const int width = std::max(1, static_cast<int>(areaWidth * scaleX + 0.5));
    const int height = std::max(1, static_cast<int>(areaHeight * scaleY + 0.5));
    return {x, std::max(0, pixelHeight - top - height), width, height};
}

class Window {
public:
    Window(int width, int height, const std::string& title, int preferredSamples,
           GraphicsApi graphicsApi = GraphicsApi::OpenGL33);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const { return m_shouldClose; }
    void swapBuffers();
    void handleEvent(const void* event) { processEvent(event); }
    void finishEventFrame() { resetEventFrame(); }
    void setTitle(const std::string& title);

    int width() const { return m_pixelWidth; }
    int height() const { return m_pixelHeight; }
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }
    float aspectRatio() const {
        return static_cast<float>(m_pixelWidth) /
            static_cast<float>(m_pixelHeight > 0 ? m_pixelHeight : 1);
    }

    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    void getCursorDelta(double& dx, double& dy);
    void getCursorPos(double& x, double& y) const;
    void setCursorLocked(bool locked);
    bool isCursorLocked() const { return m_cursorLocked; }
    void setTextInputEnabled(bool enabled);

    bool isTouchAvailable() const { return m_touchAvailable; }
    bool isMinimized() const {
        return m_minimized || m_pixelWidth <= 0 || m_pixelHeight <= 0;
    }
    bool isSrgbCapable() const { return m_srgbCapable; }
    GraphicsApi graphicsApi() const { return m_graphicsApi; }
    GraphicsCapabilities graphicsCapabilities() const;
    std::vector<std::string> requiredVulkanInstanceExtensions() const;
    std::uintptr_t createVulkanSurface(void* instance) const;
    WindowSafeArea safeArea() const;
    GamepadManager& gamepads() { return *m_gamepads; }

    using KeyCallback =
        std::function<void(int key, int scancode, ButtonAction action, int mods)>;
    void setKeyCallback(KeyCallback callback) { m_keyCallback = std::move(callback); }
    using CharCallback = std::function<void(std::string_view text)>;
    void setCharCallback(CharCallback callback) { m_charCallback = std::move(callback); }
    using MouseButtonCallback =
        std::function<void(int button, ButtonAction action, int mods)>;
    void setMouseButtonCallback(MouseButtonCallback callback) {
        m_mouseButtonCallback = std::move(callback);
    }
    using ScrollCallback = std::function<void(double xOffset, double yOffset)>;
    void setScrollCallback(ScrollCallback callback) { m_scrollCallback = std::move(callback); }
    using TouchCallback = std::function<void(const TouchEvent&)>;
    void setTouchCallback(TouchCallback callback) { m_touchCallback = std::move(callback); }
    using FocusCallback = std::function<void(bool focused)>;
    void setFocusCallback(FocusCallback callback) { m_focusCallback = std::move(callback); }
    using ResizeCallback = std::function<void(int pixelWidth, int pixelHeight)>;
    void setResizeCallback(ResizeCallback callback) {
        m_resizeCallback = std::move(callback);
    }

    static void* graphicsProcAddress(const char* name);

private:
    void* m_window = nullptr;
    void* m_context = nullptr;
    int m_pixelWidth = 1;
    int m_pixelHeight = 1;
    int m_windowWidth = 1;
    int m_windowHeight = 1;
    bool m_shouldClose = false;
    bool m_cursorLocked = false;
    bool m_minimized = false;
    bool m_srgbCapable = false;
    GraphicsApi m_graphicsApi = GraphicsApi::OpenGL33;
    bool m_touchAvailable = false;
    bool m_textInputEnabled = false;
    std::unique_ptr<GamepadManager> m_gamepads;
    std::array<bool, Key::Count> m_keys{};
    std::array<bool, MouseButton::Count> m_mouse{};
    double m_cursorX = 0.0;
    double m_cursorY = 0.0;
    double m_cursorDeltaX = 0.0;
    double m_cursorDeltaY = 0.0;

    KeyCallback m_keyCallback;
    CharCallback m_charCallback;
    MouseButtonCallback m_mouseButtonCallback;
    ScrollCallback m_scrollCallback;
    TouchCallback m_touchCallback;
    FocusCallback m_focusCallback;
    ResizeCallback m_resizeCallback;

    void resetEventFrame();
    void processEvent(const void* event);
    void refreshSizes();
};
