#pragma once

#include <tuple>

#include <cstdint>
#include <filesystem>
#include <vector>
#include <set>
#include <limits>
#include <map>
#include <optional>
#include <memory>
#include <unordered_map>
#include "entity/EntityAi.h"

#include <glm/glm.hpp>

#include "game/Item.h"
#include "entity/EntityAttack.h"
#include "entity/EntityRecord.h"
#include "entity/EntityLogic.h"
#include "game/CombatRules.h"
#include "player/PlayerPhysics.h"

struct PotionDefinition;
struct EntitySnapshot;
struct EntityDeathSnapshot;
class Player;
class IEntityPresentation;
class World;
class Chunk;
class SaveStore;

struct EntityAiStats {
    size_t decisions = 0;
    size_t sightQueries = 0;
    size_t pathNodes = 0;
    size_t activeSearches = 0;
    size_t poiBlocks = 0;
    size_t villageActions = 0;
    size_t farmBlocks = 0;
};

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
    glm::vec3 locomotionVelocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, -1.0f};
    bool attackPending = false;
    // Simulation-owned attack clock. Presentation only supplies the timing
    // data and the visual action; it cannot supply authoritative hit events.
    EntityAttackTiming attackTiming;
    float attackElapsed = 0.0f;
    bool attackImpactResolved = false;
    PlayerPhysics::HurtImmunity hurtImmunity;
    VillagerData villager;
    bool sleeping = false;
    EntityAiState ai;
    uint64_t shooterId = 0;
    std::string pluginState="{}";
    std::string nativeOwner; // derived cache of the persisted shared envelope
    uint32_t pluginStateVersion=0;
    NetherMobState nether;
    StatusEffects effects;
    float nativeTickAccumulator=0;
    uint64_t riderId=UINT64_MAX;
    float portalCooldown=0,portalRetry=0;
    bool despawning=false;
    glm::vec3 pluginMovement{0};
    uint64_t pluginAttack=UINT64_MAX;
    double pluginIntentUntil=0;
};

struct DeadEntityRender {
    uint64_t id = 0;
    EntityType type = EntityType::Cow;
    glm::dvec3 position{0.0};
    glm::vec3 velocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, -1.0f};
    uint32_t behaviorSeed = 0;
    float elapsed = 0.0f;
};

struct EntityPlayerView {
    uint64_t id = 0;
    Player* player = nullptr;
    bool targetable = true;
    bool canPickup = true;
    bool damageable = true;
};

class EntityManager {
public:
    void setPvpPlayers(bool enabled, std::vector<EntityPlayerView> players) {
        m_pvpEnabled = enabled; m_pvpPlayers = enabled ? std::move(players) : std::vector<EntityPlayerView>{};
    }
    bool arrowTouchesButton(const glm::ivec3& position) const;
    explicit EntityManager(World& world) : m_world(world) {}

    const EntityAiStats& aiStats() const { return m_aiStats; }
    void clear();
    void spawnItem(const glm::dvec3& position, ItemStack stack,
                   const glm::vec3& velocity = glm::vec3(0.0f),
                   float pickupDelaySeconds = 0.0f);
    void spawnArrow(const glm::dvec3& position, const glm::vec3& velocity,
                    float damage, bool playerOwned, uint64_t shooterId = 0);
    bool spawnMob(EntityType type, const glm::dvec3& position);
    void spawnNativeProjectile(EntityType,const glm::dvec3&,const glm::vec3&,uint64_t,ItemStack={},const std::string& owner="local");
    bool useNativeMob(Player&,uint64_t playerId);
    void dismount(uint64_t playerId);
    bool tryDismount(Player&,uint64_t playerId);
    std::vector<uint64_t> portalCandidates(float dt);
    void deferPortal(uint64_t id);
    bool transferTo(uint64_t id,EntityManager& target,const glm::dvec3&,const std::filesystem::path& root);
    bool riding(uint64_t playerId) const;
    void angerPiglins(const glm::dvec3&,bool zombified=false);
    bool pluginSnapshot(uint64_t,MC_EntitySnapshot&,std::string&) const;
    bool pluginRemove(uint64_t);
    bool pluginIntent(uint64_t,const MC_EntityIntent&);
    void explodeAt(const glm::dvec3&,float,const std::vector<EntityPlayerView>&);
    void primeTnt(const glm::ivec3& position, float fuseSeconds = 4.0f,
                  bool removeBlock = true);
    std::vector<glm::dvec3> takeExplosionEvents();
    void update(Player& player, float dt, bool isDay, bool peaceful,
                bool playerTargetable, bool playerCanPickup,
                bool thunderstorm = false, bool raining = false,
                uint64_t worldTick = 0);
    // One simulation pass for a whole dimension; participants have stable room IDs.
    void update(const std::vector<EntityPlayerView>& players, float dt, bool isDay,
                bool peaceful, bool thunderstorm = false, bool raining = false,
                uint64_t worldTick = 0);
    void strikeLightning(const std::vector<EntityPlayerView>& players, const glm::ivec3& position);
    void strikeLightning(Player& player, const glm::ivec3& position);
    MeleeAttackResult attackRay(const glm::dvec3& origin,
                                const glm::vec3& direction,
                                const MeleeAttackRequest& attack, uint64_t playerId = UINT64_MAX,const std::string& owner="local");
    bool hasAttackTarget(const glm::dvec3& origin, const glm::vec3& direction,
                         float reach) const;
    void applyReplicaSnapshots(const std::vector<EntitySnapshot>& snapshots, const std::vector<EntityDeathSnapshot>& deaths);
    void advanceReplicaPresentation(float dt);
    // Presentation is optional: a null pointer supplies canonical animation
    // timing and no-op visuals. Attack clocks and impacts stay simulation-owned,
    // which keeps combat, persistence and LAN replication renderer-independent.
    void setPresentation(IEntityPresentation* presentation) {
        m_presentation = presentation;
    }
    static glm::vec3 entitySize(const Entity& entity) {
        return renderSize(entity.type)*(entity.type==EntityType::MagmaCube ? static_cast<float>(1u<<entity.nether.magmaSize) : 1.f)*((entity.type==EntityType::Villager ||
            entity.type==EntityType::ZombieVillager) && !entity.villager.adult() ? .5f : 1.0f);
    }
    const std::vector<Entity>& entities() const { return m_entities; }
    const std::vector<DeadEntityRender>& deadEntityRenders() const {
        return m_deadEntityRenders;
    }
    std::vector<EntityRecord> saveEntities() const;
    void loadEntities(const std::vector<EntityRecord>& entities);
    bool hasHostileNear(const glm::dvec3& position, float radius) const;
    std::optional<uint64_t> useRay(const glm::dvec3& origin,
                                   const glm::vec3& direction,
                                   float reach) const;
    TradeResult tradeWith(uint64_t entityId, uint8_t offerIndex,
                          InventoryModel& inventory);
    const Entity* entityById(uint64_t entityId) const;
    bool isLogicalVillageMember(uint64_t entityId) const;
    size_t logicalVillageCount() const { return m_logicalVillages.size(); }
    bool villagerUsable(uint64_t entityId, const glm::dvec3& origin,
                        const glm::vec3& direction, float reach) const;
    void setSaveStore(SaveStore* store) { m_saveStore = store; }
    void setNaturalSpawningEnabled(bool enabled) {
        m_naturalSpawningEnabled = enabled;
    }
    void syncChunks();
    bool flushChunkEntities(size_t maxFiles = std::numeric_limits<size_t>::max(),
                            bool includeAllLoaded = false);
    void beginChunkEntityAutosave();
    bool hasDirtyChunkEntities() const {
        return !m_dirtyEntityChunks.empty() || !m_pendingEntitySaves.empty();
    }
    bool hasPendingChunkEntitySaves() const {
        return !m_pendingEntitySaves.empty();
    }

private:
    EntityAiStats m_aiStats;
    double m_aiTime = 0;
    std::map<std::tuple<int,int,int>,double> m_openedVillageDoors;
    void openNavigationDoors(Entity& entity, const glm::dvec3& destination);
    std::map<std::pair<int,int>,const Chunk*> m_aiChunks;
    std::unordered_map<uint64_t,size_t> m_aiEntityIndices;
    std::map<std::tuple<int,int,int>,std::vector<uint64_t>> m_aiBuckets;
    struct NavigationRequest {
        glm::dvec3 origin{0};
        GroundNavigation::Goal goal;
        NavigationPurpose purpose = NavigationPurpose::Move;
        double queuedAt = 0;
        std::unique_ptr<GroundNavigation::Search> search;
        std::map<std::pair<int,int>,uint64_t> revisions;
    };
    std::map<uint64_t,NavigationRequest> m_navigation;
    uint64_t m_searchCursor = 0;
    struct AiArrow { glm::dvec3 position; glm::vec3 velocity; float damage; uint64_t shooterId; };
    struct AiExplosion { glm::dvec3 position; uint32_t seed; };
    std::vector<AiArrow> m_aiArrows;
    std::vector<AiExplosion> m_aiExplosions;
    GroundNavigation::Terrain navigationTerrain(
        std::map<std::pair<int,int>,uint64_t>* revisions = nullptr,
        bool canOpenWoodDoors = false) const;
    void prepareAiFrame(float dt);
    void scheduleNavigation(const glm::dvec3& playerPosition);
    void requestNavigation(Entity& entity, const GroundNavigation::Goal& goal,
                           NavigationPurpose purpose = NavigationPurpose::Move);
    void cancelNavigation(Entity& entity);
    bool revisionsCurrent(const std::map<std::pair<int,int>,uint64_t>& revisions) const;
    Entity* aiEntity(uint64_t id);
    std::vector<uint64_t> nearbyEntities(const glm::dvec3& position, double radius) const;
    bool aiClearSight(const glm::dvec3& from, const glm::dvec3& to);
    void decideBehavior(Entity& entity, Player& player, bool isDay,
                        bool playerTargetable, uint64_t worldTick, bool thunderstorm);
    void updateMobAi(Entity& entity, Player& player, float dt, bool isDay,
                     bool playerTargetable, uint64_t worldTick, bool thunderstorm);
    void beginAttack(Entity& entity);
    static void cancelAttack(Entity& entity) {
        entity.attackPending = false;
        entity.attackElapsed = 0.0f;
        entity.attackImpactResolved = false;
    }
    void followNavigation(Entity& entity, float dt);
    void chooseEscape(Entity& entity, const glm::dvec3& danger);
    void refreshPoiIndex();
    void requestPoiClaim(Entity& entity);
    bool interactablePoi(const Entity& entity, const glm::ivec3& poi);
    std::optional<glm::dvec3> poiStand(const Entity& entity, const glm::ivec3& poi) const;
    std::optional<GroundNavigation::Goal> poiGoal(const Entity& entity, const glm::ivec3& poi) const;
    bool poiAvailable(const Entity& entity, const glm::ivec3& poi, bool bed) const;

    std::vector<EntityPlayerView> m_framePlayers;
    size_t m_spawnPlayerCursor = 0;
    bool m_pvpEnabled = false;
    std::vector<EntityPlayerView> m_pvpPlayers;
    World& m_world;
    std::vector<Entity> m_entities;
    std::map<uint64_t, std::pair<glm::dvec3, glm::vec3>> m_replicaTargets;
    std::vector<DeadEntityRender> m_deadEntityRenders;
    IEntityPresentation* m_presentation = nullptr;
    IEntityPresentation& presentation();
    uint64_t m_nextId = 1;
    float m_spawnTimer = 0.0f;
    float m_spawnerTimer=0;
    void tickSpawners(bool peaceful);
    uint32_t m_spawnSequence = 0;
    bool m_naturalSpawningEnabled = true;
    bool m_lastPopulationEnabled = false;
    SaveStore* m_saveStore = nullptr;
    std::set<std::pair<int,int>> m_loadedChunks;
    std::set<std::pair<int,int>> m_dirtyEntityChunks;
    std::set<std::pair<int,int>> m_pendingEntitySaves;
    uint64_t m_lastStreamingRevision = std::numeric_limits<uint64_t>::max();
    std::vector<glm::dvec3> m_explosionEvents;
    struct PoiChunk {
        uint64_t revision = 0;
        std::vector<glm::ivec3> beds;
        std::vector<glm::ivec3> workstations;
        size_t cursor = 0;
        bool complete = false;
    };
    std::map<std::pair<int,int>, PoiChunk> m_poiChunks;
    struct LogicalVillage {
        glm::ivec3 minimumSubchunk{0};
        glm::ivec3 maximumSubchunk{0};
        std::vector<uint64_t> members;
    };
    std::vector<LogicalVillage> m_logicalVillages;
    float m_villageRefreshSeconds = 0.0f;

    std::vector<Entity> m_nativeSpawns;
    struct NativeExplosion {glm::dvec3 position;uint32_t seed;float power;};
    std::vector<NativeExplosion> m_nativeExplosions;
    void tickNativeState(Entity&,float);
    bool nativeTargetsPlayer(Entity&,const Player&) const;
    bool updateNativeMob(Entity&,Player&,float,bool);
    void updateNativeProjectile(Entity&,float);
    void applyEntityPotion(Entity&,const PotionDefinition&,float);
    bool teleportEnderman(Entity&);
    void pluginEvent(Entity&,uint32_t);
    bool pluginAi(Entity&,float);
    void spawnAroundPlayer(const glm::dvec3& playerPosition, bool hostile,
                           bool underground);
    void moveWithTerrain(Entity& entity, const glm::vec3& horizontal, float dt);
    void integrateVelocity(Entity& entity, float dt);
    bool collides(const Entity& entity, const glm::dvec3& position) const;
    bool riderCollides(const glm::dvec3& position) const;
    bool exposedToSky(const Entity& entity) const;
    bool touchesWater(const Entity& entity) const;
    float damageEntity(Entity& entity, float damage,
                       const glm::vec3& knockback, bool playerAttack,
                       std::optional<glm::dvec3> source = std::nullopt,
                       uint64_t sourceId = 0);
    void updateArrow(Entity& entity, float dt);
    void explode(const glm::dvec3& center, float power,
                 uint32_t eventSeed, bool mobExplosion = false);
    void dropMobLoot(const Entity& entity);
    static bool hostile(EntityType type);
    static glm::vec3 renderSize(EntityType type);
    void tickVillageLife(float dt, uint64_t worldTick);
    void changeVillageReputation(Entity& victim, int amount);
    size_t m_villageLifeCursor = 0;
    float m_villageLifeTimer = 0;

    void refreshVillageClaims();

    void rebuildLogicalVillages();
};
