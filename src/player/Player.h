#pragma once

#include <glm/glm.hpp>
#include <optional>

#include "world/Block.h"

class World;

class Player {
public:
    Player(World& world);

    // ── Input ───────────────────────────────────────────────────────
    void handleMouseDelta(float dx, float dy);
    void handleMovement(const bool* keys, float dt);  // GLFW key states (at least 512 entries)
    void handleMouseButton(int button);  // GLFW_MOUSE_BUTTON_LEFT etc.

    // ── Update ──────────────────────────────────────────────────────
    void update(float dt);

    // ── State ───────────────────────────────────────────────────────
    glm::vec3 getEyePosition() const;
    glm::vec3 getPosition() const { return m_position; }
    void setPosition(const glm::vec3& pos) { m_position = pos; }
    glm::vec3 getForward() const   { return m_forward; }
    float getYaw() const   { return m_yaw; }
    float getPitch() const { return m_pitch; }

    std::optional<glm::ivec3> getHighlightedBlock() const { return m_highlightedBlock; }

    bool isMouseLocked() const { return m_mouseLocked; }
    void setMouseLocked(bool locked) { m_mouseLocked = locked; }
    void toggleMouseLock() { m_mouseLocked = !m_mouseLocked; }

    bool isFlying() const { return m_flying; }

    // ── Selected block for placement ───────────────────────────────────
    void setSelectedBlock(BlockId id) { m_selectedBlock = id; }
    BlockId getSelectedBlock() const { return m_selectedBlock; }

private:
    World& m_world;

    // Position & velocity
    glm::vec3 m_position{0.0f, 50.0f, 0.0f};
    glm::vec3 m_velocity{0.0f, 0.0f, 0.0f};

    // View angles
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    // Direction vectors
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};

    // State
    bool m_onGround = false;
    bool m_isSprinting = false;
    bool m_mouseLocked = true;
    bool m_flying = false;
    bool m_spaceWasDown = false;
    float m_lastSpaceReleaseTime = 1.0f;  // time since last SPACE release (for double-tap detection)

    // Interaction
    std::optional<glm::ivec3> m_highlightedBlock;
    float m_actionCooldown = 0.0f;
    BlockId m_selectedBlock = BlockId::GRASS;

    // ── Internal methods ─────────────────────────────────────────────
    void updateDirectionVectors();
    void moveAndCollide(const glm::vec3& delta);
    bool checkCollision(float px, float py, float pz) const;
    float findGround() const;
    void applyPhysics(float dt);
    void updateHighlight();

    void breakBlock();
    void placeBlock();
    bool collidesWithPlayer(const glm::ivec3& blockPos) const;
};
