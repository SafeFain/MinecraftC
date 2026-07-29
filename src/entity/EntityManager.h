#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include <set>

#include <glm/glm.hpp>

#include "game/Item.h"
#include "game/SaveStore.h"
#include "entity/EntityLogic.h"
#include "entity/EntityModelRegistry.h"

class Player;
class Renderer;
class World;

struct Entity {
    uint64_t id = 0;
    EntityType type = EntityType::Item;
    glm::dvec3 position{0.0};
    glm::vec3 velocity{0.0f};
    float health = 1.0f;
    float ageSeconds = 0.0f;
    float actionCooldown = 0.0f;
    ItemStack item;
    uint32_t behaviorSeed = 0;
    bool inGround = false;
    bool playerOwned = false;
    float projectileDamage = 0.0f;
    float stuckSeconds = 0.0f;
    float hurtFlashSeconds = 0.0f;
    float burningSeconds = 0.0f;
    float burnDamageSeconds = 0.0f;
    bool spiderProvoked = false;
};

struct DeadEntityRender {
    uint64_t id = 0;
    EntityType type = EntityType::Cow;
    glm::dvec3 position{0.0};
    glm::vec3 velocity{0.0f};
    uint32_t behaviorSeed = 0;
    float elapsed = 0.0f;
};

class EntityManager {
public:
    explicit EntityManager(World& world) : m_world(world) {}

    void clear();
    void spawnItem(const glm::dvec3& position, ItemStack stack,
                   const glm::vec3& velocity = glm::vec3(0.0f));
    void spawnArrow(const glm::dvec3& position, const glm::vec3& velocity,
                    float damage, bool playerOwned);
    void primeTnt(const glm::ivec3& position, float fuseSeconds = 4.0f,
                  bool removeBlock = true);
    std::vector<glm::dvec3> takeExplosionEvents();
    void update(Player& player, float dt, bool isDay, bool peaceful,
                bool playerTargetable, bool playerCanPickup,
                bool thunderstorm = false, bool raining = false);
    void strikeLightning(Player& player, const glm::ivec3& position);
    bool attackRay(const glm::dvec3& origin, const glm::vec3& direction,
                   float reach, float damage);
    void render(Renderer& renderer, const glm::mat4& viewProjection,
                const glm::dvec3& renderOrigin) const;
    void initializeModels(const std::filesystem::path& assetRoot,
                          Renderer& renderer);
    const std::vector<Entity>& entities() const { return m_entities; }
    const std::vector<DeadEntityRender>& deadEntityRenders() const {
        return m_deadEntityRenders;
    }
    std::vector<WorldMetadata::PersistedEntity> saveEntities() const;
    void loadEntities(const std::vector<WorldMetadata::PersistedEntity>& entities);
    bool hasHostileNear(const glm::dvec3& position, float radius) const;
    void setSaveStore(SaveStore* store) { m_saveStore = store; }
    void syncChunks();
    void flushChunkEntities();

private:
    World& m_world;
    std::vector<Entity> m_entities;
    std::vector<DeadEntityRender> m_deadEntityRenders;
    mutable EntityModelRegistry m_modelRegistry;
    uint64_t m_nextId = 1;
    float m_spawnTimer = 0.0f;
    uint32_t m_spawnSequence = 0;
    SaveStore* m_saveStore = nullptr;
    std::set<std::pair<int,int>> m_loadedChunks;
    std::vector<glm::dvec3> m_explosionEvents;

    void spawnMob(EntityType type, const glm::dvec3& position);
    void spawnAroundPlayer(const glm::dvec3& playerPosition, bool hostile);
    void moveWithTerrain(Entity& entity, const glm::vec3& horizontal, float dt);
    void integrateVelocity(Entity& entity, float dt);
    bool collides(const Entity& entity, const glm::dvec3& position) const;
    bool exposedToSky(const Entity& entity) const;
    bool touchesWater(const Entity& entity) const;
    void damageEntity(Entity& entity, float damage, const glm::vec3& knockback,
                      bool playerAttack);
    void updateArrow(Entity& entity, Player& player, float dt);
    void explode(Player& player, const glm::dvec3& center, float power,
                 uint32_t eventSeed);
    void dropMobLoot(const Entity& entity);
    static bool hostile(EntityType type);
    static glm::vec3 renderColor(EntityType type);
    static glm::vec3 renderSize(EntityType type);
};
