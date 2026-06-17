#include "player/Player.h"
#include "world/World.h"
#include "world/Block.h"
#include "Config.h"
#include <GLFW/glfw3.h>

#include <cmath>
#include <algorithm>

Player::Player(World& world) : m_world(world) {}

// ── Input ─────────────────────────────────────────────────────────────

void Player::handleMouseDelta(float dx, float dy) {
    if (!m_mouseLocked) return;

    m_yaw   -= dx * Config::MOUSE_SENSITIVITY;
    m_pitch -= dy * Config::MOUSE_SENSITIVITY;
    m_pitch = std::max(-89.9f, std::min(89.9f, m_pitch));
}

void Player::handleMovement(const bool keys[256], float dt) {
    if (!m_mouseLocked) return;

    // ── Double-tap SPACE to toggle flight ──────────────────────────
    bool spaceDown = keys[GLFW_KEY_SPACE];

    if (spaceDown && !m_spaceWasDown) {
        // Space just pressed — check for double-tap
        if (m_lastSpaceReleaseTime > 0.0f && m_lastSpaceReleaseTime < 0.35f) {
            m_flying = !m_flying;
            m_velocity.y = 0.0f;
            m_lastSpaceReleaseTime = 1.0f;  // prevent chain double-taps
        }
    }

    // Track time since last release (not held duration)
    if (!spaceDown) {
        m_lastSpaceReleaseTime += dt;
    } else {
        m_lastSpaceReleaseTime = 0.0f;
    }

    m_spaceWasDown = spaceDown;

    m_isSprinting = keys[GLFW_KEY_LEFT_CONTROL] || keys[GLFW_KEY_RIGHT_CONTROL];
    float speed = m_isSprinting ? Config::SPRINT_SPEED : Config::PLAYER_SPEED;
    if (m_flying) speed = Config::SPRINT_SPEED * 1.5f;

    glm::vec3 moveDir(0.0f);
    if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP])    moveDir += m_forward;
    if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN])  moveDir -= m_forward;
    if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT])  moveDir += m_right;
    if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) moveDir -= m_right;

    if (m_flying) {
        // Flying: full 3D movement, no gravity, no collision
        if (keys[GLFW_KEY_SPACE])            moveDir.y += 1.0f;
        if (keys[GLFW_KEY_LEFT_SHIFT])       moveDir.y -= 1.0f;

        if (glm::length(moveDir) > 0.0f) {
            moveDir = glm::normalize(moveDir) * speed * dt;
            m_position += moveDir;
        }
        m_velocity.y = 0.0f;
    } else {
        // Normal movement
        glm::vec3 horizontal(moveDir.x, 0.0f, moveDir.z);
        float hLen = glm::length(horizontal);
        if (hLen > 0.0f) {
            horizontal = horizontal / hLen * speed * dt;
            moveAndCollide(horizontal);
        }

        // Jump
        if (keys[GLFW_KEY_SPACE] && m_onGround) {
            m_velocity.y = Config::JUMP_SPEED;
            m_onGround = false;
        }
    }
}

void Player::handleMouseButton(int button) {
    if (!m_mouseLocked) return;
    if (m_actionCooldown > 0.0f) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        breakBlock();
        m_actionCooldown = 0.15f;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        placeBlock();
        m_actionCooldown = 0.15f;
    }
}

// ── Update ────────────────────────────────────────────────────────────

void Player::update(float dt) {
    if (m_actionCooldown > 0.0f) {
        m_actionCooldown -= dt;
    }

    updateDirectionVectors();
    applyPhysics(dt);
    updateHighlight();
}

void Player::updateDirectionVectors() {
    float yawRad   = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    m_forward = glm::vec3(
        std::cos(pitchRad) * std::sin(yawRad),
        std::sin(pitchRad),
        std::cos(pitchRad) * std::cos(yawRad)
    );
    m_forward = glm::normalize(m_forward);

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    m_right = glm::normalize(glm::cross(worldUp, m_forward));
}

glm::vec3 Player::getEyePosition() const {
    return m_position + glm::vec3(0.0f, Config::EYE_HEIGHT, 0.0f);
}

// ── Movement & Collision ──────────────────────────────────────────────

void Player::moveAndCollide(const glm::vec3& delta) {
    bool blockedX = false, blockedZ = false;

    // X axis
    float newX = m_position.x + delta.x;
    if (!checkCollision(newX, m_position.y, m_position.z)) {
        m_position.x = newX;
    } else {
        blockedX = true;
    }

    // Z axis
    float newZ = m_position.z + delta.z;
    if (!checkCollision(m_position.x, m_position.y, newZ)) {
        m_position.z = newZ;
    } else {
        blockedZ = true;
    }

    // Auto step-up: if blocked on ground, try stepping up 1 block
    if ((blockedX || blockedZ) && m_onGround) {
        float stepHeight = 1.0f;
        float testY = m_position.y + stepHeight;

        if (!checkCollision(m_position.x, testY, m_position.z)) {
            if (blockedX && !checkCollision(newX, testY, m_position.z)) {
                m_position.x = newX;
                m_position.y = testY;
                float groundY = findGround();
                m_position.y = groundY + 0.001f;
            }
            if (blockedZ && !checkCollision(m_position.x, testY, newZ)) {
                m_position.z = newZ;
                m_position.y = testY;
                float groundY = findGround();
                m_position.y = groundY + 0.001f;
            }
        }
    }
}

bool Player::checkCollision(float px, float py, float pz) const {
    float halfW = Config::PLAYER_WIDTH / 2.0f;
    float margin = 0.001f;

    int minX = static_cast<int>(std::floor(px - halfW + margin));
    int maxX = static_cast<int>(std::floor(px + halfW - margin));
    int minY = static_cast<int>(std::floor(py - margin));
    int maxY = static_cast<int>(std::floor(py + Config::PLAYER_HEIGHT - margin));
    int minZ = static_cast<int>(std::floor(pz - halfW + margin));
    int maxZ = static_cast<int>(std::floor(pz + halfW - margin));

    for (int bx = minX; bx <= maxX; ++bx) {
        for (int by = minY; by <= maxY; ++by) {
            for (int bz = minZ; bz <= maxZ; ++bz) {
                BlockId id = m_world.getBlock(bx, by, bz);
                if (id == BlockId::AIR) continue;

                const BlockProperties& props = getBlockProps(id);
                if (!props.solid) continue;

                // AABB vs AABB
                if (px - halfW < bx + 1 && px + halfW > bx &&
                    py < by + 1 && py + Config::PLAYER_HEIGHT > by &&
                    pz - halfW < bz + 1 && pz + halfW > bz) {
                    return true;
                }
            }
        }
    }
    return false;
}

float Player::findGround() const {
    float px = m_position.x, py = m_position.y, pz = m_position.z;
    for (int by = static_cast<int>(std::floor(py)); by >= 0; --by) {
        BlockId id = m_world.getBlock(
            static_cast<int>(std::floor(px)), by,
            static_cast<int>(std::floor(pz))
        );
        const BlockProperties& props = getBlockProps(id);
        if (props.solid) {
            return static_cast<float>(by + 1);
        }
    }
    return 0.0f;
}

void Player::applyPhysics(float dt) {
    if (m_flying) return; // no physics in flight mode

    // Gravity
    m_velocity.y -= Config::GRAVITY * dt;

    // Vertical movement
    float dy = m_velocity.y * dt;
    float newY = m_position.y + dy;

    if (!checkCollision(m_position.x, newY, m_position.z)) {
        m_position.y = newY;
        m_onGround = false;
    } else {
        if (dy < 0) {
            // Landing — snap to ground
            float groundY = findGround();
            m_position.y = groundY + 0.001f;
            m_onGround = true;
        } else {
            // Hit ceiling — snap below it
            float ceiling = std::floor(m_position.y + Config::PLAYER_HEIGHT);
            m_position.y = ceiling - Config::PLAYER_HEIGHT - 0.001f;
        }
        m_velocity.y = 0.0f;
    }

    // Clamp Y
    if (m_position.y < 0.0f) {
        m_position.y = 0.01f;
        m_velocity.y = 0.0f;
        m_onGround = true;
    }
    if (m_position.y > Config::CHUNK_SIZE_Y - Config::PLAYER_HEIGHT) {
        m_position.y = Config::CHUNK_SIZE_Y - Config::PLAYER_HEIGHT;
    }
}

// ── Block interaction ─────────────────────────────────────────────────

void Player::updateHighlight() {
    auto hit = m_world.raycast(getEyePosition(), m_forward, Config::REACH_DISTANCE);
    if (hit) {
        m_highlightedBlock = hit->blockPos;
    } else {
        m_highlightedBlock = std::nullopt;
    }
}

void Player::breakBlock() {
    auto hit = m_world.raycast(getEyePosition(), m_forward, Config::REACH_DISTANCE);
    if (hit) {
        m_world.setBlock(hit->blockPos.x, hit->blockPos.y, hit->blockPos.z, BlockId::AIR);
    }
}

void Player::placeBlock() {
    auto hit = m_world.raycast(getEyePosition(), m_forward, Config::REACH_DISTANCE);
    if (!hit) return;

    glm::ivec3 placePos = hit->blockPos + hit->faceNormal;

    // Don't place on top of ourselves
    if (!collidesWithPlayer(placePos)) {
        m_world.setBlock(placePos.x, placePos.y, placePos.z, m_selectedBlock);
    }
}

bool Player::collidesWithPlayer(const glm::ivec3& blockPos) const {
    float halfW = Config::PLAYER_WIDTH / 2.0f;
    float px = m_position.x, py = m_position.y, pz = m_position.z;
    int bx = blockPos.x, by = blockPos.y, bz = blockPos.z;

    return (bx < px + halfW && bx + 1 > px - halfW &&
            by < py + Config::PLAYER_HEIGHT && by + 1 > py &&
            bz < pz + halfW && bz + 1 > pz - halfW);
}
