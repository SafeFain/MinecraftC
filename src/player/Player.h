#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <optional>
#include <functional>
#include <utility>

#include "world/Block.h"
#include "game/GameRules.h"
#include "game/InventoryModel.h"
#include "game/SurvivalStats.h"
#include "core/Input.h"
#include "player/PlayerPhysics.h"

class World;
class EntityManager;

class Player {
public:
    Player(World& world);

    // ── Input ───────────────────────────────────────────────────────
    void handleMouseDelta(float dx, float dy, float sensitivity, bool invertY);
    void handleMovement(const InputState& input, float dt);
    void handleMouseButton(int button, ButtonAction action);

    // ── Update ──────────────────────────────────────────────────────
    void update(float dt);

    // ── State ───────────────────────────────────────────────────────
    glm::dvec3 getEyePosition() const;
    glm::dvec3 getPosition() const { return m_position; }
    void setPosition(const glm::dvec3& pos) { m_position = pos; }
    void teleport(const glm::dvec3& pos);
    glm::vec3 getForward() const   { return m_forward; }
    float getYaw() const   { return m_yaw; }
    float getPitch() const { return m_pitch; }

    std::optional<glm::ivec3> getHighlightedBlock() const { return m_highlightedBlock; }
    float getMiningProgress() const {
        if (!m_mining || !m_miningTarget || m_miningRequired <= 0.0f) return 0.0f;
        return std::min(m_miningProgress / m_miningRequired, 1.0f);
    }

    bool isMouseLocked() const { return m_mouseLocked; }
    void setMouseLocked(bool locked) { m_mouseLocked = locked; }
    void toggleMouseLock() { m_mouseLocked = !m_mouseLocked; }

    bool isFlying() const { return m_flying; }
    bool isSurvival() const { return m_gameMode == GameMode::Survival; }
    bool isSpectator() const { return m_gameMode == GameMode::Spectator; }
    void configureRules(GameMode mode, Difficulty difficulty);
    void setEntityManager(EntityManager* entities) { m_entities = entities; }
    void setBedCallback(std::function<void(const glm::ivec3&)> callback) {
        m_bedCallback = std::move(callback);
    }
    void setBlockBreakCallback(
        std::function<void(const glm::ivec3&, BlockId)> callback) {
        m_blockBreakCallback = std::move(callback);
    }
    void setDamageCallback(std::function<void(float)> callback) {
        m_damageCallback = std::move(callback);
    }
    GameMode gameMode() const { return m_gameMode; }
    Difficulty difficulty() const { return m_difficulty; }
    InventoryModel& inventory() { return m_inventory; }
    const InventoryModel& inventory() const { return m_inventory; }
    SurvivalStats& survivalStats() { return m_survivalStats; }
    const SurvivalStats& survivalStats() const { return m_survivalStats; }
    void takeDamage(float amount, bool bypassArmor = false);
    void ignite(float seconds) { m_burningSeconds = std::max(m_burningSeconds, seconds); }
    void extinguish() { m_burningSeconds = 0.0f; m_burnDamageTicks = 0; }
    void setRainExposure(bool exposed) { m_rainExposed = exposed; }
    void resetDamageImmunity() { m_hurtImmunity = {}; }
    void setSelectedSlot(int slot) { if (slot >= 0 && slot < 9) m_selectedSlot = slot; }
    int selectedSlot() const { return m_selectedSlot; }
    float airFraction() const { return std::clamp(m_airTicks / 300.0f, 0.0f, 1.0f); }
    bool underwater() const { return m_airTicks < 300; }
    bool onGround() const { return m_onGround; }
    void applyImpulse(const glm::vec3& impulse) { m_velocity += impulse; }

    // ── Selected block for placement ───────────────────────────────────
    void setSelectedBlock(BlockId id) { m_selectedBlock = id; }
    void setSelectedCreativeItem(ItemId id) { m_selectedCreativeItem = id; }
    BlockId getSelectedBlock() const { return m_selectedBlock; }

private:
    World& m_world;
    std::function<void(const glm::ivec3&, BlockId)> m_blockBreakCallback;
    std::function<void(float)> m_damageCallback;

    // Position & velocity
    glm::dvec3 m_position{0.0, 50.0, 0.0};
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
    ItemId m_selectedCreativeItem = ItemId::GRASS_BLOCK;
    GameMode m_gameMode = GameMode::Creative;
    Difficulty m_difficulty = Difficulty::Normal;
    InventoryModel m_inventory;
    SurvivalStats m_survivalStats;
    EntityManager* m_entities = nullptr;
    std::function<void(const glm::ivec3&)> m_bedCallback;
    int m_selectedSlot = 0;
    bool m_mining = false;
    std::optional<glm::ivec3> m_miningTarget;
    float m_miningProgress = 0.0f;
    float m_miningRequired = 0.0f;
    float m_survivalTickRemainder = 0.0f;
    float m_fallDistance = 0.0f;
    int m_airTicks = 300;
    uint32_t m_environmentDamageTicks = 0;
    float m_burningSeconds = 0.0f;
    uint32_t m_burnDamageTicks = 0;
    bool m_rainExposed = false;
    bool m_blocking = false;
    PlayerPhysics::HurtImmunity m_hurtImmunity;

    // ── Internal methods ─────────────────────────────────────────────
    void updateDirectionVectors();
    void moveAndCollide(const glm::vec3& delta);
    void moveFlyingAndCollide(const glm::vec3& delta);
    bool checkCollision(double px, double py, double pz) const;
    float findGround() const;
    bool isInWater() const;
    void applyPhysics(float dt);
    void updateHighlight();
    void updateMining(float dt);
    void updateEnvironment(uint32_t ticks);

    void breakBlock();
    void placeBlock();
    bool collidesWithPlayer(const glm::ivec3& blockPos) const;
};
