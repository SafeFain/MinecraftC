#pragma once

#include <string>
#include <functional>
#include <GLFW/glfw3.h>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();
    void setTitle(const std::string& title);

    int width() const  { return m_width; }
    int height() const { return m_height; }
    float aspectRatio() const { return static_cast<float>(m_width) / m_height; }

    // Input queries
    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    void getCursorDelta(double& dx, double& dy);
    void getCursorPos(double& x, double& y) const;

    // Mouse lock (cursor grab)
    void setCursorLocked(bool locked);
    bool isCursorLocked() const { return m_cursorLocked; }

    // Callbacks
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    void setKeyCallback(KeyCallback cb) { m_keyCallback = std::move(cb); }

    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    void setMouseButtonCallback(MouseButtonCallback cb) { m_mouseButtonCallback = std::move(cb); }

    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
    void setScrollCallback(ScrollCallback cb) { m_scrollCallback = std::move(cb); }

    GLFWwindow* native() const { return m_window; }

    bool isMinimized() const { return m_minimized; }

private:
    GLFWwindow* m_window = nullptr;
    int m_width, m_height;
    bool m_cursorLocked = false;
    bool m_minimized = false;

    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    double m_cursorDeltaX = 0.0;
    double m_cursorDeltaY = 0.0;

    KeyCallback m_keyCallback;
    MouseButtonCallback m_mouseButtonCallback;
    ScrollCallback m_scrollCallback;

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void iconifyCallback(GLFWwindow* window, int iconified);

    void handleFramebufferResize(int width, int height);
};
