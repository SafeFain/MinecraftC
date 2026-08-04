#pragma once

#include <cstdint>
#include <functional>
#include <memory>

struct GLFWwindow;

enum class TouchPhase : uint8_t { Begin, Move, End, Cancel };

struct TouchEvent {
    int32_t id = -1;
    TouchPhase phase = TouchPhase::Cancel;
    double x = 0.0;
    // Native window client/surface coordinates with a top-left origin.
    double y = 0.0;
};

class TouchSource {
public:
    using Callback = std::function<void(const TouchEvent&)>;

    explicit TouchSource(GLFWwindow* window);
    ~TouchSource();
    TouchSource(const TouchSource&) = delete;
    TouchSource& operator=(const TouchSource&) = delete;

    void setCallback(Callback callback);
    bool available() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
