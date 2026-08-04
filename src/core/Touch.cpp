#include "core/Touch.h"

#include "debug/Log.h"

#include <GLFW/glfw3.h>
#include <cstring>
#include <utility>

#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include <wayland-client.h>
#include <algorithm>
#endif

struct TouchSource::Impl {
    Callback callback;
    bool isAvailable = false;

#if defined(__linux__)
    wl_display* display = nullptr;
    wl_surface* surface = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    wl_touch* touch = nullptr;
    uint32_t seatVersion = 0;
    uint32_t seatGlobal = 0;

    static void registryGlobal(void* data, wl_registry* registry, uint32_t name,
                               const char* interface, uint32_t version) {
        auto* self = static_cast<Impl*>(data);
        if (std::strcmp(interface, wl_seat_interface.name) != 0 || self->seat) return;
        self->seatGlobal = name;
        self->seatVersion = std::min(version, 7u);
        self->seat = static_cast<wl_seat*>(wl_registry_bind(
            registry, name, &wl_seat_interface, self->seatVersion));
        wl_seat_add_listener(self->seat, &seatListener, self);
    }
    static void registryRemove(void* data, wl_registry*, uint32_t name) {
        auto* self = static_cast<Impl*>(data);
        if (name == self->seatGlobal) self->destroySeat();
    }
    static void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
        auto* self = static_cast<Impl*>(data);
        const bool hasTouch = (capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;
        if (hasTouch && !self->touch) {
            self->touch = wl_seat_get_touch(seat);
            wl_touch_add_listener(self->touch, &touchListener, self);
            self->isAvailable = true;
            LOG_INFO("Wayland touchscreen input available");
        } else if (!hasTouch && self->touch) {
            self->cancelAll();
            self->destroyTouch();
            self->isAvailable = false;
        }
    }
    static void seatName(void*, wl_seat*, const char*) {}
    static void touchDown(void* data, wl_touch*, uint32_t, uint32_t, wl_surface* surface,
                          int32_t id, wl_fixed_t x, wl_fixed_t y) {
        auto* self = static_cast<Impl*>(data);
        if (surface != self->surface) return;
        self->emit({id, TouchPhase::Begin, wl_fixed_to_double(x), wl_fixed_to_double(y)});
    }
    static void touchUp(void* data, wl_touch*, uint32_t, uint32_t, int32_t id) {
        auto* self = static_cast<Impl*>(data);
        self->emit({id, TouchPhase::End, 0.0, 0.0});
    }
    static void touchMotion(void* data, wl_touch*, uint32_t, int32_t id,
                            wl_fixed_t x, wl_fixed_t y) {
        auto* self = static_cast<Impl*>(data);
        self->emit({id, TouchPhase::Move, wl_fixed_to_double(x), wl_fixed_to_double(y)});
    }
    static void touchFrame(void*, wl_touch*) {}
    static void touchCancel(void* data, wl_touch*) {
        static_cast<Impl*>(data)->cancelAll();
    }
    static void touchShape(void*, wl_touch*, int32_t, wl_fixed_t, wl_fixed_t) {}
    static void touchOrientation(void*, wl_touch*, int32_t, wl_fixed_t) {}

    inline static const wl_registry_listener registryListener = {
        registryGlobal, registryRemove};
    inline static const wl_seat_listener seatListener = {seatCapabilities, seatName};
    inline static const wl_touch_listener touchListener = {
        touchDown, touchUp, touchMotion, touchFrame, touchCancel,
        touchShape, touchOrientation};

    void emit(const TouchEvent& event) { if (callback) callback(event); }
    void cancelAll() { emit({-1, TouchPhase::Cancel, 0.0, 0.0}); }
    void destroyTouch() {
        if (!touch) return;
        if (wl_proxy_get_version(reinterpret_cast<wl_proxy*>(touch)) >= 3)
            wl_touch_release(touch);
        else wl_touch_destroy(touch);
        touch = nullptr;
        isAvailable = false;
    }
    void destroySeat() {
        cancelAll();
        destroyTouch();
        if (seat) {
            if (seatVersion >= 5) wl_seat_release(seat);
            else wl_seat_destroy(seat);
        }
        seat = nullptr;
        seatGlobal = 0;
        seatVersion = 0;
    }

    explicit Impl(GLFWwindow* window) {
        if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) return;
        display = glfwGetWaylandDisplay();
        surface = glfwGetWaylandWindow(window);
        if (!display || !surface) return;
        registry = wl_display_get_registry(display);
        if (!registry) return;
        wl_registry_add_listener(registry, &registryListener, this);
        // The application has not installed its input callbacks yet, so this
        // initial registry roundtrip cannot re-enter application input routing.
        if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0)
            LOG_WARN("Could not initialize Wayland touch registry");
    }

    ~Impl() {
        destroySeat();
        if (registry) wl_registry_destroy(registry);
    }
#else
    explicit Impl(GLFWwindow*) {}
#endif
};

TouchSource::TouchSource(GLFWwindow* window) : m_impl(std::make_unique<Impl>(window)) {}
TouchSource::~TouchSource() = default;
void TouchSource::setCallback(Callback callback) { m_impl->callback = std::move(callback); }
bool TouchSource::available() const { return m_impl->isAvailable; }
