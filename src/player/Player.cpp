#include "player/Player.h"
#include "world/World.h"
#include "world/Block.h"
#include "player/PlayerPhysics.h"
#include "Config.h"
#include "game/SurvivalRules.h"
#include "entity/EntityManager.h"

#include <cmath>
#include <algorithm>
#include <utility>

Player::Player(World& world) : m_world(world) {}

void Player::teleport(const glm::dvec3& pos) {
    m_position = pos;
    m_velocity = glm::vec3(0.0f);
    m_onGround = false;
    m_fallDistance = 0.0f;
}

void Player::configureRules(GameMode mode, Difficulty difficulty) {
    m_gameMode = mode;
    m_difficulty = difficulty;
    m_blocking = false;
    m_mining = false;
    m_miningTarget.reset();
    m_miningProgress = 0.0f;
    resetDamageImmunity();
    if (mode == GameMode::Spectator) {
        m_flying = true;
        m_onGround = false;
        m_velocity = glm::vec3(0.0f);
    } else {
        m_flying = false;
        m_velocity.y = 0.0f;
    }
}

void Player::takeDamage(float amount, bool bypassArmor) {
    if (m_gameMode != GameMode::Survival || amount <= 0.0f) return;
    amount = PlayerPhysics::damageAfterImmunity(
        m_hurtImmunity, amount, Config::PLAYER_HURT_IMMUNITY_SECONDS);
    if (amount <= 0.0f) return;
    if (!bypassArmor && m_blocking && m_inventory.offhand().id == ItemId::SHIELD) {
        amount *= 0.33f;
        auto& shield = m_inventory.offhand();
        if (++shield.damage >= getItemProps(shield.id).maxDurability) shield.clear();
    }
    if (!bypassArmor) {
        int points = 0;
        for (size_t slot = 0; slot < InventoryModel::ARMOR_SIZE; ++slot) {
            auto& armor = m_inventory.armor()[slot];
            if (armor.empty()) continue;
            const uint16_t relative = static_cast<uint16_t>(armor.id) -
                                      static_cast<uint16_t>(ItemId::LEATHER_HELMET);
            if (relative < 16 && relative % 4 == slot) {
                points += armorPointsForItem(armor.id);
                if (++armor.damage >= getItemProps(armor.id).maxDurability)
                    armor.clear();
            }
        }
        amount *= 1.0f - std::min(points, 20) * 0.04f;
    }
    m_survivalStats.damage(amount);
    if (m_damageCallback) m_damageCallback(amount);
}

// ── Input ─────────────────────────────────────────────────────────────

void Player::handleMouseDelta(float dx, float dy, float sensitivity, bool invertY) {
    if (!m_mouseLocked) return;

    m_yaw   -= dx * sensitivity;
    m_pitch += dy * sensitivity * (invertY ? 1.0f : -1.0f);
    m_pitch = std::max(-89.9f, std::min(89.9f, m_pitch));
}

void Player::handleMovement(const InputState& input, float dt) {
    if (!m_mouseLocked) return;

    // ── Double-tap SPACE to toggle flight ──────────────────────────
    bool spaceDown = input.held(InputAction::Jump);

    if (spaceDown && !m_spaceWasDown) {
        // Space just pressed — check for double-tap
        if (m_gameMode == GameMode::Creative &&
            m_lastSpaceReleaseTime > 0.0f && m_lastSpaceReleaseTime < 0.35f) {
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

    m_isSprinting = input.held(InputAction::Sprint);
    if (m_gameMode == GameMode::Survival && !m_survivalStats.canSprint())
        m_isSprinting = false;
    float speed = m_isSprinting ? Config::SPRINT_SPEED : Config::PLAYER_SPEED;
    const bool inWater = m_gameMode == GameMode::Survival && isInWater();
    if (inWater) speed *= Config::WATER_HORIZONTAL_FACTOR;

    glm::vec3 moveDir(0.0f);
    glm::vec3 planarForward(m_forward.x, 0.0f, m_forward.z);
    if (glm::length(planarForward) > 0.0f)
        planarForward = glm::normalize(planarForward);
    moveDir += planarForward * input.value(InputAction::MoveForward);
    moveDir -= planarForward * input.value(InputAction::MoveBackward);
    moveDir += m_right * input.value(InputAction::MoveLeft);
    moveDir -= m_right * input.value(InputAction::MoveRight);

    if (m_flying) {
        // Creative flight uses camera yaw for horizontal travel. Vertical
        // controls are independent, so Space+Shift holds altitude.
        glm::vec3 horizontal(moveDir.x, 0.0f, moveDir.z);
        const float horizontalLength = glm::length(horizontal);
        if (horizontalLength > 1.0f) horizontal /= horizontalLength;
        const float flySpeed = m_isSprinting
            ? Config::CREATIVE_FLY_SPRINT_SPEED
            : Config::CREATIVE_FLY_SPEED;
        float vertical = 0.0f;
        if (input.held(InputAction::Jump)) vertical += 1.0f;
        if (input.held(InputAction::Sneak))
            vertical -= 1.0f;
        const glm::vec3 flightDelta =
            horizontal * flySpeed * dt +
            glm::vec3(0.0f, vertical * Config::CREATIVE_FLY_VERTICAL_SPEED * dt,
                      0.0f);
        if (m_gameMode == GameMode::Spectator)
            m_position += flightDelta;
        else
            moveFlyingAndCollide(flightDelta);
        m_velocity.y = 0.0f;
    } else {
        // Normal movement
        glm::vec3 horizontal(moveDir.x, 0.0f, moveDir.z);
        float hLen = glm::length(horizontal);
        if (hLen > 0.0f) {
            if (hLen > 1.0f) { horizontal /= hLen; hLen = 1.0f; }
            horizontal *= speed * dt;
            moveAndCollide(horizontal);
            if (m_gameMode == GameMode::Survival) {
                m_survivalStats.addExhaustion(
                    hLen * speed * dt * (m_isSprinting ? 0.1f : 0.01f));
            }
        }

        // Jump
        if (inWater) {
            m_velocity.y = PlayerPhysics::waterVerticalVelocity(
                m_velocity.y, input.held(InputAction::Jump),
                input.held(InputAction::Sneak), dt);
            m_onGround = false;
            m_fallDistance = 0.0f;
        } else if (input.held(InputAction::Jump) && m_onGround) {
            m_velocity.y = Config::JUMP_SPEED;
            m_onGround = false;
            if (m_gameMode == GameMode::Survival)
                m_survivalStats.addExhaustion(m_isSprinting ? 0.2f : 0.05f);
        }
    }
}

void Player::handleMouseButton(int button, ButtonAction action) {
    if (!m_mouseLocked) return;
    if (m_gameMode == GameMode::Spectator) return;
    if (button == MouseButton::Right && m_gameMode == GameMode::Survival &&
        m_inventory.offhand().id == ItemId::SHIELD) {
        m_blocking = action != ButtonAction::Release;
        if (m_blocking) return;
    }

    if (button == MouseButton::Left) {
        m_mining = action != ButtonAction::Release;
        if (!m_mining) {
            m_miningTarget.reset();
            m_miningProgress = 0.0f;
            m_miningRequired = 0.0f;
        } else if (m_actionCooldown <= 0.0f) {
            float damage = 1.0f;
            if (m_gameMode == GameMode::Survival) {
                const auto& stack = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
                if (!stack.empty()) damage = std::max(1.0f, getItemProps(stack.id).attackDamage);
            } else if (m_gameMode == GameMode::Creative)
                damage = std::max(1.0f, getItemProps(m_selectedCreativeItem).attackDamage);
            if (m_entities && m_entities->attackRay(
                    getEyePosition(), m_forward,
                    m_gameMode == GameMode::Survival ? 3.0f : Config::REACH_DISTANCE,
                    damage)) {
                m_mining = false;
                m_actionCooldown = 0.6f;
            } else if (m_gameMode == GameMode::Creative) {
                breakBlock();
                m_actionCooldown = 0.15f;
            }
        }
    } else if (button == MouseButton::Right && action == ButtonAction::Press &&
               m_actionCooldown <= 0.0f) {
        placeBlock();
        m_actionCooldown = 0.15f;
    }
}

// ── Update ────────────────────────────────────────────────────────────

void Player::update(float dt) {
    PlayerPhysics::tickHurtImmunity(m_hurtImmunity, dt);
    if (m_actionCooldown > 0.0f) {
        m_actionCooldown -= dt;
    }

    updateDirectionVectors();
    applyPhysics(dt);
    updateHighlight();
    updateMining(dt);
    if (m_gameMode == GameMode::Survival) {
        m_survivalTickRemainder += dt * 20.0f;
        const uint32_t ticks = static_cast<uint32_t>(m_survivalTickRemainder);
        if (ticks > 0) {
            m_survivalStats.tick(m_difficulty, ticks);
            updateEnvironment(ticks);
            m_survivalTickRemainder -= static_cast<float>(ticks);
        }
    }
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

glm::dvec3 Player::getEyePosition() const {
    return m_position + glm::dvec3(0.0, Config::EYE_HEIGHT, 0.0);
}

// ── Movement & Collision ──────────────────────────────────────────────

void Player::moveAndCollide(const glm::vec3& delta) {
    bool blockedX = false, blockedZ = false;
    double blockedTargetX = m_position.x;
    double blockedTargetZ = m_position.z;
    const int steps = PlayerPhysics::movementSubsteps(glm::length(delta));
    const glm::vec3 step = delta / static_cast<float>(steps);

    // Bound each collision query so a slow frame cannot carry the player
    // across a one-block wall or into a corner.
    for (int i = 0; i < steps; ++i) {
        const double newX = m_position.x + step.x;
        if (!checkCollision(newX, m_position.y, m_position.z)) {
            m_position.x = newX;
        } else {
            blockedX = true;
            blockedTargetX = newX;
        }

        const double newZ = m_position.z + step.z;
        if (!checkCollision(m_position.x, m_position.y, newZ)) {
            m_position.z = newZ;
        } else {
            blockedZ = true;
            blockedTargetZ = newZ;
        }
    }

    // Auto jump uses the normal vertical impulse. It never teleports the
    // player onto the obstacle, so the arc follows the same physics as Space.
    const bool movementBlocked = blockedX || blockedZ;
    const double raisedY = m_position.y + 1.0;
    const bool currentHeadroomClear =
        !checkCollision(m_position.x, raisedY, m_position.z);
    const bool targetHeadroomClear =
        (!blockedX || !checkCollision(blockedTargetX, raisedY, m_position.z)) &&
        (!blockedZ || !checkCollision(m_position.x, raisedY, blockedTargetZ));
    if (PlayerPhysics::shouldAutoJump(
            Config::AUTO_JUMP, m_onGround, movementBlocked,
            currentHeadroomClear, targetHeadroomClear)) {
        m_velocity.y = Config::JUMP_SPEED;
        m_onGround = false;
    }
}

void Player::moveFlyingAndCollide(const glm::vec3& delta) {
    // Substeps prevent high-speed sprint flight from tunneling through a
    // one-block wall during a slow frame.
    constexpr float maxStep = 0.20f;
    const int steps = std::max(
        1, static_cast<int>(std::ceil(glm::length(delta) / maxStep)));
    const glm::vec3 step = delta / static_cast<float>(steps);
    for (int i = 0; i < steps && m_flying; ++i) {
        const double nextX = m_position.x + step.x;
        if (!checkCollision(nextX, m_position.y, m_position.z))
            m_position.x = nextX;

        const double nextZ = m_position.z + step.z;
        if (!checkCollision(m_position.x, m_position.y, nextZ))
            m_position.z = nextZ;

        const double nextY = m_position.y + step.y;
        if (!checkCollision(m_position.x, nextY, m_position.z)) {
            m_position.y = nextY;
            m_onGround = false;
        } else if (step.y < 0.0f) {
            m_position.y = findGround() + 0.001f;
            m_onGround = true;
            m_flying = false;
            m_fallDistance = 0.0f;
        }
    }
}

bool Player::checkCollision(double px, double py, double pz) const {
    const double halfW = Config::PLAYER_WIDTH / 2.0;
    const double margin = 0.001;

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
    return PlayerPhysics::findSupportHeight(
        m_position.x, m_position.y, m_position.z,
        [this](int x, int y, int z) { return m_world.getBlock(x, y, z); });
}

bool Player::isInWater() const {
    const glm::dvec3 eye = getEyePosition();
    const double half = Config::PLAYER_WIDTH * 0.5 - 0.001;
    const int minX = static_cast<int>(std::floor(m_position.x - half));
    const int maxX = static_cast<int>(std::floor(m_position.x + half));
    const int minZ = static_cast<int>(std::floor(m_position.z - half));
    const int maxZ = static_cast<int>(std::floor(m_position.z + half));
    const int feetY = static_cast<int>(std::floor(m_position.y + 0.1));
    const int eyeY = static_cast<int>(std::floor(eye.y));
    for (int x = minX; x <= maxX; ++x)
        for (int z = minZ; z <= maxZ; ++z)
            if (isWater(m_world.getBlock(x, feetY, z)) ||
                isWater(m_world.getBlock(x, eyeY, z))) return true;
    return false;
}

void Player::applyPhysics(float dt) {
    if (m_flying) return; // no physics in flight mode

    const bool inWater = m_gameMode == GameMode::Survival && isInWater();
    if (inWater) {
        m_fallDistance = 0.0f;
        m_velocity.y = std::max(m_velocity.y, -Config::WATER_ENTRY_MAX_FALL_SPEED);
    } else {
        m_velocity.y -= Config::GRAVITY * dt;
    }

    // Vertical movement is swept in bounded steps. In particular, a ceiling
    // hit retains the last non-colliding position; deriving a ceiling from the
    // old player height could push the AABB down into the floor.
    const float dy = m_velocity.y * dt;
    const int steps = PlayerPhysics::movementSubsteps(dy);
    const float stepY = dy / static_cast<float>(steps);
    bool verticalBlocked = false;
    for (int i = 0; i < steps; ++i) {
        const double newY = m_position.y + stepY;
        if (!checkCollision(m_position.x, newY, m_position.z)) {
            m_position.y = newY;
            m_onGround = false;
            if (stepY < 0.0f) m_fallDistance += -stepY;
            continue;
        }

        verticalBlocked = true;
        if (stepY < 0.0f) {
            m_position.y = findGround() + 0.001f;
            m_onGround = true;
            if (m_gameMode == GameMode::Survival && m_fallDistance > 3.0f)
                takeDamage(std::floor(m_fallDistance - 3.0f));
            m_fallDistance = 0.0f;
        }
        break;
    }
    if (verticalBlocked) m_velocity.y = 0.0f;

    // Clamp Y
    if (m_position.y < static_cast<double>(Config::WORLD_MIN_Y)) {
        m_position.y = static_cast<double>(Config::WORLD_MIN_Y) + 0.01;
        m_velocity.y = 0.0f;
        m_onGround = true;
    }
    if (m_position.y > Config::WORLD_MAX_Y - Config::PLAYER_HEIGHT) {
        m_position.y = Config::WORLD_MAX_Y - Config::PLAYER_HEIGHT;
    }
}

void Player::updateEnvironment(uint32_t ticks) {
    const glm::ivec3 eye(
        static_cast<int>(std::floor(getEyePosition().x)),
        static_cast<int>(std::floor(getEyePosition().y)),
        static_cast<int>(std::floor(getEyePosition().z)));
    const glm::ivec3 feet(
        static_cast<int>(std::floor(m_position.x)),
        static_cast<int>(std::floor(m_position.y)),
        static_cast<int>(std::floor(m_position.z)));
    const BlockId eyeBlock = m_world.getBlock(eye.x, eye.y, eye.z);
    const BlockId feetBlock = m_world.getBlock(feet.x, feet.y, feet.z);
    if (feetBlock == BlockId::FIRE || eyeBlock == BlockId::FIRE)
        ignite(4.0f);
    if (isWater(feetBlock) || isWater(eyeBlock) || m_rainExposed) {
        m_burningSeconds = 0.0f;
        m_burnDamageTicks = 0;
    } else if (m_burningSeconds > 0.0f) {
        m_burningSeconds = std::max(0.0f, m_burningSeconds - ticks / 20.0f);
        m_burnDamageTicks += ticks;
        while (m_burnDamageTicks >= 20) {
            takeDamage(4.0f);
            m_burnDamageTicks -= 20;
        }
    }
    if (isWater(eyeBlock)) {
        m_airTicks -= static_cast<int>(ticks);
        if (m_airTicks <= 0) {
            m_environmentDamageTicks += ticks;
            if (m_environmentDamageTicks >= 20) {
                takeDamage(2.0f, true);
                m_environmentDamageTicks = 0;
            }
        }
    } else {
        m_airTicks = std::min(300, m_airTicks + static_cast<int>(ticks) * 4);
    }
    if (isLava(feetBlock) || isLava(eyeBlock)) {
        m_environmentDamageTicks += ticks;
        if (m_environmentDamageTicks >= 10) {
            takeDamage(4.0f);
            m_environmentDamageTicks = 0;
        }
    } else if (isSolid(eyeBlock)) {
        m_environmentDamageTicks += ticks;
        if (m_environmentDamageTicks >= 10) {
            takeDamage(1.0f, true);
            m_environmentDamageTicks = 0;
        }
    }
}

// ── Block interaction ─────────────────────────────────────────────────

void Player::updateHighlight() {
    if (m_gameMode == GameMode::Spectator) {
        m_highlightedBlock.reset();
        return;
    }
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
        glm::ivec3 breakPos = hit->blockPos;
        BlockId block = m_world.getBlock(
            hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
        if (block == BlockId::SUNFLOWER_TOP &&
            m_world.getBlock(breakPos.x, breakPos.y - 1, breakPos.z) ==
                BlockId::SUNFLOWER_BOTTOM) {
            --breakPos.y;
            block = BlockId::SUNFLOWER_BOTTOM;
        }
        if (m_gameMode == GameMode::Survival) {
            if (m_entities) {
                for (const auto& content : m_world.takeBlockEntityContents(hit->blockPos))
                    m_entities->spawnItem(glm::dvec3(hit->blockPos) + glm::dvec3(0.5), content);
            }
            const ItemStack& tool = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
            const auto drops = getBlockDrops(block, tool,
                static_cast<uint32_t>(hit->blockPos.x * 73428767 ^
                                      hit->blockPos.y * 912931 ^
                                      hit->blockPos.z * 438289));
            for (const auto& drop : drops) {
                ItemStack remaining = drop;
                remaining.count = static_cast<uint8_t>(m_inventory.add(drop));
                if (!remaining.empty() && m_entities)
                    m_entities->spawnItem(glm::vec3(hit->blockPos) + glm::vec3(0.5f),
                                          remaining);
            }
            if (!tool.empty() && getItemProps(tool.id).maxDurability > 0) {
                auto& mutableTool = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
                if (++mutableTool.damage >= getItemProps(mutableTool.id).maxDurability)
                    mutableTool.clear();
            }
            m_survivalStats.addExhaustion(0.005f);
        }
        m_world.setBlock(breakPos.x, breakPos.y, breakPos.z, BlockId::AIR);
        if (m_blockBreakCallback) m_blockBreakCallback(breakPos, block);
    }
}

void Player::updateMining(float dt) {
    if (!m_mining || m_gameMode != GameMode::Survival) return;
    auto hit = m_world.raycast(getEyePosition(), m_forward, Config::REACH_DISTANCE);
    if (!hit) {
        m_miningTarget.reset();
        m_miningProgress = 0.0f;
        m_miningRequired = 0.0f;
        return;
    }
    if (!m_miningTarget || *m_miningTarget != hit->blockPos) {
        m_miningTarget = hit->blockPos;
        m_miningProgress = 0.0f;
        m_miningRequired = 0.0f;
    }
    const BlockId block = m_world.getBlock(
        hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
    const float required = miningSeconds(
        block, m_inventory.slot(static_cast<size_t>(m_selectedSlot)),
        isInWater(), !m_onGround);
    m_miningRequired = required;
    if (required < 0.0f) {
        m_miningProgress = 0.0f;
        return;
    }
    m_miningProgress += dt;
    if (required == 0.0f || m_miningProgress >= required) {
        breakBlock();
        m_miningTarget.reset();
        m_miningProgress = 0.0f;
        m_miningRequired = 0.0f;
    }
}

void Player::placeBlock() {
    if (m_gameMode == GameMode::Creative && m_selectedCreativeItem == ItemId::BOW) {
        if (m_entities)
            m_entities->spawnArrow(getEyePosition() + glm::dvec3(m_forward) * 0.45,
                                   m_forward * 24.0f, 6.0f, true);
        return;
    }
    if (m_gameMode == GameMode::Survival) {
        auto& selected = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
        if (!selected.empty() && getItemProps(selected.id).kind == ItemKind::Food) {
            if (m_survivalStats.eat(selected.id) && --selected.count == 0)
                selected.clear();
            return;
        }
        if (!selected.empty() && selected.id == ItemId::BOW) {
            if (m_inventory.remove(ItemId::ARROW, 1) && m_entities) {
                m_entities->spawnArrow(getEyePosition() + glm::dvec3(m_forward) * 0.45,
                                       m_forward * 24.0f, 6.0f, true);
                if (++selected.damage >= getItemProps(selected.id).maxDurability)
                    selected.clear();
            }
            return;
        }
    }

    auto hit = m_world.raycast(getEyePosition(), m_forward, Config::REACH_DISTANCE);
    if (!hit) return;

    glm::ivec3 placePos = hit->blockPos + hit->faceNormal;
    const BlockId targetedBlock = m_world.getBlock(
        hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
    if (m_gameMode == GameMode::Survival && targetedBlock == BlockId::WHITE_BED) {
        if (m_bedCallback) m_bedCallback(hit->blockPos);
        return;
    }

    const ItemId activeItem = m_gameMode == GameMode::Survival
        ? m_inventory.slot(static_cast<size_t>(m_selectedSlot)).id
        : m_selectedCreativeItem;
    const auto& activeProperties = getItemProps(activeItem);
    if (m_gameMode == GameMode::Creative && activeProperties.spawnEggMob) {
        if (m_entities) {
            const glm::dvec3 spawnPosition(
                static_cast<double>(placePos.x) + 0.5,
                static_cast<double>(placePos.y),
                static_cast<double>(placePos.z) + 0.5);
            m_entities->spawnMob(
                entityTypeForSpawnEgg(*activeProperties.spawnEggMob),
                spawnPosition);
        }
        return;
    }
    if (activeItem == ItemId::FLINT_AND_STEEL) {
        bool used = false;
        if (targetedBlock == BlockId::TNT && m_entities) {
            m_entities->primeTnt(hit->blockPos);
            used = true;
        } else if (m_world.getBlock(placePos.x, placePos.y, placePos.z) == BlockId::AIR &&
                   (isSolid(targetedBlock) || isFlammable(targetedBlock))) {
            m_world.setBlock(placePos.x, placePos.y, placePos.z, BlockId::FIRE);
            used = true;
        }
        if (used && m_gameMode == GameMode::Survival) {
            auto& stack = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
            if (++stack.damage >= getItemProps(stack.id).maxDurability) stack.clear();
        }
        return;
    }

    BlockId placed = m_selectedBlock;
    if (m_gameMode == GameMode::Creative && placed == BlockId::AIR) return;
    if (m_gameMode == GameMode::Survival) {
        auto& stack = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
        if (stack.empty()) return;
        const auto& props = getItemProps(stack.id);
        const BlockId hitBlock = targetedBlock;
        if (props.tool == ToolKind::Hoe &&
            (hitBlock == BlockId::DIRT || hitBlock == BlockId::GRASS) &&
            hit->faceNormal.y > 0) {
            m_world.setBlock(hit->blockPos.x, hit->blockPos.y, hit->blockPos.z,
                             BlockId::FARMLAND);
            if (++stack.damage >= props.maxDurability) stack.clear();
            return;
        }
        if (stack.id == ItemId::WHEAT_SEEDS && isFarmland(hitBlock) &&
            hit->faceNormal.y > 0 && m_world.getBlock(
                placePos.x, placePos.y, placePos.z) == BlockId::AIR) {
            m_world.setBlock(placePos.x, placePos.y, placePos.z, BlockId::WHEAT_0);
            if (--stack.count == 0) stack.clear();
            return;
        }
        if (!props.placedBlock) return;
        placed = *props.placedBlock;
        if (isSapling(placed)) {
            if (hit->faceNormal.y <= 0 ||
                (hitBlock != BlockId::GRASS && hitBlock != BlockId::DIRT &&
                 hitBlock != BlockId::PODZOL) ||
                m_world.getBlock(placePos.x, placePos.y, placePos.z) != BlockId::AIR)
                return;
        }
    }

    if (!collidesWithPlayer(placePos)) {
        if (placed == BlockId::SUNFLOWER_BOTTOM) {
            const BlockId soil = m_world.getBlock(
                placePos.x, placePos.y - 1, placePos.z);
            if (hit->faceNormal.y <= 0 ||
                (soil != BlockId::GRASS && soil != BlockId::DIRT &&
                 soil != BlockId::PODZOL) ||
                placePos.y + 1 >= Config::WORLD_MAX_Y ||
                m_world.getBlock(placePos.x, placePos.y, placePos.z) != BlockId::AIR ||
                m_world.getBlock(placePos.x, placePos.y + 1, placePos.z) != BlockId::AIR ||
                collidesWithPlayer(placePos + glm::ivec3(0, 1, 0)))
                return;
            m_world.setBlock(placePos.x, placePos.y + 1, placePos.z,
                             BlockId::SUNFLOWER_TOP);
        }
        m_world.setBlock(placePos.x, placePos.y, placePos.z, placed);
        if (m_gameMode == GameMode::Survival) {
            auto& stack = m_inventory.slot(static_cast<size_t>(m_selectedSlot));
            if (--stack.count == 0) stack.clear();
        }
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
