#pragma once

#include "core/Input.h"
#include "core/Touch.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

class UIRenderer;

enum class TouchCommand : uint8_t {
    AttackPress, AttackRelease, UsePress, UseRelease,
    OpenInventory, Pause, SelectHotbar
};

struct TouchCommandEvent {
    TouchCommand command;
    int value = 0;
};

struct TouchControlConfig {
    float sensitivity = 1.0f;
    float size = 1.0f;
    float opacity = 0.65f;
    bool leftHanded = false;
};

struct TouchRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

class TouchControls {
public:
    void configure(int width, int height, const TouchControlConfig& config);
    std::vector<TouchCommandEvent> onTouch(const TouchEvent& event);
    void applyTo(InputState& input) const;
    glm::vec2 consumeLookDelta();
    void cancelAll();
    void render(UIRenderer& ui) const;
    bool active() const { return !m_touches.empty(); }

private:
    enum class Target : uint8_t { Move, Look, Jump, Sneak, Attack, Use, Inventory, Pause, Hotbar };
    struct Capture { Target target; glm::vec2 last; int slot = -1; };

    int m_width = 1, m_height = 1;
    TouchControlConfig m_config;
    TouchRect m_moveArea, m_jump, m_sneak, m_attack, m_use, m_inventory, m_pause;
    std::array<TouchRect, 9> m_hotbar{};
    glm::vec2 m_moveCenter{0.0f};
    float m_moveRadius = 50.0f;
    glm::vec2 m_move{0.0f};
    glm::vec2 m_lookDelta{0.0f};
    std::unordered_map<int32_t, Capture> m_touches;
    bool m_jumpHeld = false, m_sneakHeld = false;

    Target targetAt(float x, float y, int& slot) const;
    void updateMove(float x, float y);
    std::vector<TouchCommandEvent> release(Capture capture);
};
