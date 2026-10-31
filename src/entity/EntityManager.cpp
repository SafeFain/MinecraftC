#include "entity/EntityManager.h"
#include "entity/EntityLogic.h"
#include "entity/EntityPresentation.h"
#include "entity/ProjectileCollision.h"

#include "player/Player.h"
#include "world/World.h"
#include "game/SurvivalSession.h"
#include "game/SurvivalRules.h"
#include "game/SaveStore.h"
#include "game/NetherProgression.h"
#include "world/NetherGenerator.h"
#include "world/NetherPortal.h"
#include "game/EntityTransferJournal.h"
#include "game/DimensionRules.h"
#include "game/NativeProjectileState.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <tuple>

namespace {
uint32_t hash32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

std::pair<int,int> entityChunk(const glm::dvec3& position) {
    return {World::worldToChunkX(position.x), World::worldToChunkZ(position.z)};
}

NullEntityPresentation& nullPresentation() {
    static NullEntityPresentation instance;
    return instance;
}
}

IEntityPresentation& EntityManager::presentation() {
    return m_presentation ? *m_presentation : nullPresentation();
}

void EntityManager::clear() {
    m_entities.clear(); m_replicaTargets.clear(); m_pvpPlayers.clear(); m_pvpEnabled = false;
    m_navigation.clear();
    m_openedVillageDoors.clear();
    m_aiChunks.clear();
    m_aiBuckets.clear();
    m_aiEntityIndices.clear();
    m_aiTime = 0;
    m_aiArrows.clear();
    m_aiExplosions.clear();
    m_nativeSpawns.clear();m_nativeExplosions.clear();
    m_searchCursor = 0;
    m_aiStats = {};
    m_deadEntityRenders.clear();
    m_spawnTimer = 0.0f;m_spawnerTimer=0;
    m_villageLifeTimer=0; m_villageLifeCursor=0;
    m_spawnSequence = 0;
    m_lastPopulationEnabled = false;
    m_loadedChunks.clear();
    m_dirtyEntityChunks.clear();
    m_pendingEntitySaves.clear();
    m_lastStreamingRevision = std::numeric_limits<uint64_t>::max();
    m_explosionEvents.clear();
    m_poiChunks.clear();
    m_logicalVillages.clear();
    m_villageRefreshSeconds = 0.0f;
    presentation().clearInstances();
}

std::vector<EntityRecord> EntityManager::saveEntities() const {
    std::vector<EntityRecord> saved;
    saved.reserve(m_entities.size());
    for (const auto& entity : m_entities) {
        saved.push_back({
            static_cast<uint16_t>(entity.type), entity.position, entity.velocity,
            entity.health, entity.ageSeconds, entity.item, entity.behaviorSeed,
            static_cast<uint8_t>((entity.inGround ? 1 : 0) |
                                 (entity.playerOwned ? 2 : 0)), entity.projectileDamage
            , entity.villager, entity.pluginState,entity.pluginStateVersion,entity.nether,entity.effects.active()
        });
    }
    return saved;
}

void EntityManager::loadEntities(
    const std::vector<EntityRecord>& saved) {
    for (const auto& source : saved) {
        Entity entity;
        entity.id = m_nextId++;
        entity.type = static_cast<EntityType>(source.type);
        entity.position = source.position;
        entity.velocity = source.velocity;
        entity.health = source.health;
        entity.ageSeconds = source.ageSeconds;
        entity.item = source.item;
        entity.behaviorSeed = source.behaviorSeed;
        entity.inGround = (source.flags & 1) != 0;
        entity.playerOwned = (source.flags & 2) != 0;
        entity.projectileDamage = source.projectileDamage;
        entity.villager = source.villager;
        entity.nether=source.nether;entity.effects.restore(source.effects);
        entity.pluginState=source.pluginState;entity.pluginStateVersion=source.pluginStateVersion;
        m_entities.push_back(entity);
    }
}

void EntityManager::syncChunks() {
    if (!m_saveStore) return;
    const uint64_t revision = m_world.streamingRevision();
    const bool populationEnabled = m_naturalSpawningEnabled && m_world.gameRules().boolean(GameRuleId::SpawnMobs);
    const bool recheckPopulation = populationEnabled && !m_lastPopulationEnabled;
    if (revision == m_lastStreamingRevision && !recheckPopulation) {
        m_lastPopulationEnabled = populationEnabled;
        return;
    }
    std::set<std::pair<int,int>> active;
    for (const Chunk* chunk : m_world.getSimulationChunks())
        if (chunk->generated.load()) active.insert({chunk->cx,chunk->cz});
    for (const auto& key : m_loadedChunks) if (!active.count(key)) {
        // Unchanged or empty chunks already match their on-disk snapshot.
        // Avoid creating and atomically replacing an empty entity file merely
        // because movement carried the chunk outside the active radius.
        if (m_dirtyEntityChunks.count(key) != 0 ||
            m_pendingEntitySaves.count(key) != 0) {
            std::vector<EntityRecord> saved;
            for (const auto& entity : m_entities) if (entityChunk(entity.position)==key) {
                saved.push_back({static_cast<uint16_t>(entity.type),entity.position,entity.velocity,
                    entity.health,entity.ageSeconds,entity.item,entity.behaviorSeed,
                    static_cast<uint8_t>((entity.inGround?1:0)|(entity.playerOwned?2:0)),
                    entity.projectileDamage, entity.villager, entity.pluginState, entity.pluginStateVersion,entity.nether,entity.effects.active()});
            }
            m_saveStore->saveChunkEntities(key.first,key.second,saved);
        }
        m_dirtyEntityChunks.erase(key);
        m_pendingEntitySaves.erase(key);
    }
    m_entities.erase(std::remove_if(m_entities.begin(),m_entities.end(),[&](const Entity& entity){
        const auto key=entityChunk(entity.position);
        return m_loadedChunks.count(key) && !active.count(key);
    }),m_entities.end());
    for (const auto& key : active) if (!m_loadedChunks.count(key) || recheckPopulation) {
        uint32_t populationVersion = 0;
        if (m_loadedChunks.count(key)) {
            populationVersion = m_saveStore->loadChunkEntityPopulationVersion(key.first, key.second);
        } else if (auto prefetched = m_world.takePrefetchedChunkEntities(
                key.first, key.second)) {
            loadEntities(prefetched->entities);
            populationVersion = prefetched->populationVersion;
        } else {
            loadEntities(m_saveStore->loadChunkEntities(key.first,key.second));
            populationVersion = m_saveStore->loadChunkEntityPopulationVersion(
                key.first, key.second);
        }
        if (populationVersion < 1u && populationEnabled) {
            const auto requests = m_world.villageSpawnsForChunk(
                key.first, key.second);
            for (const auto& request : requests) {
                const bool alreadyMerged = std::any_of(
                    m_entities.begin(), m_entities.end(),
                    [&](const Entity& entity) {
                        return entity.type == EntityType::Villager &&
                            entity.behaviorSeed == request.seed &&
                            entity.position == request.position;
                    });
                if (alreadyMerged) continue;
                if (!spawnMob(EntityType::Villager, request.position)) continue;
                Entity& villager = m_entities.back();
                villager.behaviorSeed = request.seed;
                villager.villager.offerSeed = request.seed;
            }
            bool nativePopulation=m_world.dimension()==DimensionId::Nether;
            if(nativePopulation) {
                for(const auto& structure:m_world.generator().nether().structuresAt(key.first*16+8,key.second*16+8)) {
                    if(structure.kind>=NetherStructureKind::Fossil)continue;
                    for(int i=0;i<3;++i) {
                        const auto position=glm::dvec3(structure.anchor)+(structure.kind==NetherStructureKind::Fortress?glm::dvec3(i*8-8+.5,1,.5):glm::dvec3(i==1?10.5:-9.5,1,i==2?10.5:-9.5));
                        if(entityChunk(position)!=key)continue;
                        const uint32_t seed=hash32(uint32_t(structure.seed)^(uint32_t(i)+1)*0x9e3779b9u);
                        if(std::any_of(m_entities.begin(),m_entities.end(),[&](const auto& e){return e.behaviorSeed==seed;}))continue;
                        EntityType type=structure.kind==NetherStructureKind::Fortress?(i==0?EntityType::Blaze:EntityType::WitherSkeleton):
                            (i==0?EntityType::PiglinBrute:structure.kind==NetherStructureKind::BastionStable?EntityType::Hoglin:EntityType::Piglin);
                        if(spawnMob(type,position))m_entities.back().behaviorSeed=seed;
                    }
                }
            }
            if (!requests.empty() || nativePopulation) {
                std::vector<EntityRecord> populated;
                for (const Entity& entity : m_entities) {
                    if (entityChunk(entity.position) != key) continue;
                    populated.push_back({static_cast<uint16_t>(entity.type),
                        entity.position,entity.velocity,entity.health,
                        entity.ageSeconds,entity.item,entity.behaviorSeed,
                        static_cast<uint8_t>((entity.inGround?1:0) |
                                             (entity.playerOwned?2:0)),
                        entity.projectileDamage,entity.villager,entity.pluginState,entity.pluginStateVersion,entity.nether,entity.effects.active()});
                }
                // Persist the merged entities first. If a crash interrupts this
                // sequence, the absent revision marker safely retries and merges
                // the same deterministic requests on the next load.
                m_saveStore->saveChunkEntities(
                    key.first, key.second, populated);
                m_saveStore->saveChunkEntityPopulationVersion(
                    key.first, key.second, 1u);
            }
            // A request-free chunk needs no marker: retrying the empty pure
            // query is harmless, and a later generation version may add a
            // deterministic village request at this coordinate.
        }
    }
    m_loadedChunks=std::move(active);
    m_lastStreamingRevision = revision;
    m_lastPopulationEnabled = populationEnabled;
}

void EntityManager::beginChunkEntityAutosave() {
    m_pendingEntitySaves.insert(m_dirtyEntityChunks.begin(),
                                m_dirtyEntityChunks.end());
    m_dirtyEntityChunks.clear();
}

bool EntityManager::flushChunkEntities(size_t maxFiles, bool includeAllLoaded) {
    if (!m_saveStore) return true;
    if (includeAllLoaded) {
        m_pendingEntitySaves.insert(m_loadedChunks.begin(), m_loadedChunks.end());
        // Manual entities can be created before the next streaming sync (for
        // example, immediately after a dimension load). Include their owning
        // chunks so a dimension switch cannot strand them in memory.
        for (const auto& entity : m_entities)
            m_pendingEntitySaves.insert(entityChunk(entity.position));
        m_dirtyEntityChunks.clear();
    }
    size_t savedFiles = 0;
    while (!m_pendingEntitySaves.empty() && savedFiles < maxFiles) {
        const auto key = *m_pendingEntitySaves.begin();
        std::vector<EntityRecord> saved;
        for(const auto& entity:m_entities) if(entityChunk(entity.position)==key)
            saved.push_back({static_cast<uint16_t>(entity.type),entity.position,entity.velocity,
                entity.health,entity.ageSeconds,entity.item,entity.behaviorSeed,
                static_cast<uint8_t>((entity.inGround?1:0)|(entity.playerOwned?2:0)),
                entity.projectileDamage, entity.villager, entity.pluginState, entity.pluginStateVersion,entity.nether,entity.effects.active()});
        m_saveStore->saveChunkEntities(key.first,key.second,saved);
        m_pendingEntitySaves.erase(key);
        ++savedFiles;
    }
    return m_pendingEntitySaves.empty();
}

void EntityManager::spawnItem(
    const glm::dvec3& position, ItemStack stack, const glm::vec3& velocity,
    float pickupDelaySeconds) {
    if (stack.empty()) return;
    Entity entity;
    entity.id = m_nextId++;
    entity.type = EntityType::Item;
    entity.position = position;
    entity.velocity = velocity;
    entity.actionCooldown = std::max(0.0f,pickupDelaySeconds);
    entity.item = stack;
    entity.behaviorSeed = hash32(static_cast<uint32_t>(entity.id));
    m_entities.push_back(entity);
    m_dirtyEntityChunks.insert(entityChunk(entity.position));
}

void EntityManager::spawnArrow(const glm::dvec3& position, const glm::vec3& velocity,
                               float damage, bool playerOwned, uint64_t shooterId) {
    Entity entity;
    entity.id = m_nextId++;
    entity.type = EntityType::Arrow;
    entity.position = position;
    entity.velocity = velocity;
    if (glm::length(velocity) > 0.0001f)
        entity.facing = glm::normalize(velocity);
    entity.projectileDamage = damage;
    entity.playerOwned = playerOwned;
    entity.shooterId = shooterId;
    entity.health = 1.0f;
    m_entities.push_back(entity);
    m_dirtyEntityChunks.insert(entityChunk(entity.position));
}

void EntityManager::primeTnt(const glm::ivec3& position, float fuseSeconds,
                             bool removeBlock) {
    if (removeBlock) {
        if (m_world.getBlock(position.x, position.y, position.z) != BlockId::TNT)
            return;
        m_world.setBlock(position.x, position.y, position.z, BlockId::AIR);
    }
    Entity entity;
    entity.id = m_nextId++;
    entity.type = EntityType::PrimedTnt;
    entity.position = glm::dvec3(position) + glm::dvec3(0.5, 0.0, 0.5);
    entity.velocity = {0.0f, 0.2f, 0.0f};
    entity.health = 1.0f;
    entity.projectileDamage = std::max(0.05f, fuseSeconds);
    entity.behaviorSeed = hash32(static_cast<uint32_t>(entity.id) ^
        static_cast<uint32_t>(position.x) * 73428767u ^
        static_cast<uint32_t>(position.z) * 912931u);
    m_entities.push_back(entity);
}

std::vector<glm::dvec3> EntityManager::takeExplosionEvents() {
    std::vector<glm::dvec3> result;
    result.swap(m_explosionEvents);
    return result;
}

bool EntityManager::spawnMob(EntityType type, const glm::dvec3& position) {
    Entity entity;
    entity.id = m_nextId++;
    entity.type = type;
    entity.position = position;
    entity.behaviorSeed = hash32(static_cast<uint32_t>(entity.id));
    if(!registeredMob(type))return false;
    switch (entityTemplate(type)) {
        case EntityType::Cow:
        case EntityType::Pig:
        case EntityType::Sheep: entity.health = 10.0f; break;
        case EntityType::Chicken: entity.health = 4.0f; break;
        case EntityType::Zombie:
        case EntityType::Skeleton:
        case EntityType::Blastling: entity.health = 20.0f; break;
        case EntityType::Spider: entity.health = 16.0f; break;
        case EntityType::Villager:
            entity.health = 20.0f;
            entity.villager.offerSeed = entity.behaviorSeed;
            break;
        case EntityType::ZombieVillager: entity.health = 20.0f; break;
        case EntityType::IronGolem: entity.health = 100.0f; break;
        case EntityType::Arrow: return false;
        case EntityType::PrimedTnt: return false;
        case EntityType::Item: return false;
    }
    if(const auto* definition=Plugins::entityContent(static_cast<uint16_t>(type))) {
        entity.health=definition->health;entity.pluginStateVersion=definition->stateVersion;
    }
    if(nativeNetherMob(type))entity.health=nativeMobHealth(type);
    if(type==EntityType::MagmaCube)entity.health=16;
    if (collides(entity, position)) return false;
    pluginEvent(entity,MC_ENTITY_SPAWN);
    m_entities.push_back(entity);
    m_aiEntityIndices[entity.id]=m_entities.size()-1;
    m_dirtyEntityChunks.insert(entityChunk(position));
    return true;
}

bool EntityManager::hostile(EntityType type) {
    if(nativeNetherMob(type))return type!=EntityType::Strider;
    if(const auto* e=Plugins::entityContent(static_cast<uint16_t>(type)))return e->hostile;
    return type == EntityType::Zombie || type == EntityType::Skeleton ||
           type == EntityType::Spider || type == EntityType::Blastling ||
           type == EntityType::ZombieVillager;
}

bool EntityManager::hasHostileNear(const glm::dvec3& position, float radius) const {
    return std::any_of(m_entities.begin(), m_entities.end(),
        [&](const Entity& entity) {
            return hostile(entity.type) && entity.health > 0.0f &&
                   glm::distance(entity.position, position) <= radius;
        });
}

const Entity* EntityManager::entityById(uint64_t entityId) const {
    const auto found = std::find_if(m_entities.begin(), m_entities.end(),
        [entityId](const Entity& entity) { return entity.id == entityId; });
    return found == m_entities.end() ? nullptr : &*found;
}

std::optional<uint64_t> EntityManager::useRay(
    const glm::dvec3& origin, const glm::vec3& direction, float reach) const {
    const auto block = m_world.raycast(origin, direction, reach);
    double maximum = block ? block->distance : static_cast<double>(reach);
    std::optional<uint64_t> selected;
    for (const Entity& entity : m_entities) {
        if (entity.type != EntityType::Villager || entity.health <= 0.0f) continue;
        const glm::vec3 size = entitySize(entity);
        const glm::dvec3 minimum = entity.position +
            glm::dvec3(-size.x * .5, 0.0, -size.z * .5);
        const glm::dvec3 maximumBox = entity.position +
            glm::dvec3(size.x * .5, size.y, size.z * .5);
        double nearDistance = 0.0;
        double farDistance = maximum;
        for (int axis = 0; axis < 3; ++axis) {
            const double component = direction[axis];
            if (std::abs(component) < 1e-9) {
                if (origin[axis] < minimum[axis] || origin[axis] > maximumBox[axis]) {
                    nearDistance = maximum + 1.0;
                    break;
                }
                continue;
            }
            double first = (minimum[axis] - origin[axis]) / component;
            double second = (maximumBox[axis] - origin[axis]) / component;
            if (first > second) std::swap(first, second);
            nearDistance = std::max(nearDistance, first);
            farDistance = std::min(farDistance, second);
        }
        if (nearDistance <= farDistance && nearDistance >= 0.0 &&
            nearDistance < maximum) {
            maximum = nearDistance;
            selected = entity.id;
        }
    }
    return selected;
}

TradeResult EntityManager::tradeWith(
    uint64_t entityId, uint8_t offerIndex, InventoryModel& inventory) {
    auto found = std::find_if(m_entities.begin(), m_entities.end(),
        [entityId](const Entity& entity) { return entity.id == entityId; });
    if (found == m_entities.end() || found->type != EntityType::Villager ||
        found->health <= 0.0f)
        return TradeResult::InvalidOffer;
    const TradeResult result = executeVillagerTrade(
        found->villager, offerIndex, inventory);
    if (result == TradeResult::Success)
        m_dirtyEntityChunks.insert(entityChunk(found->position));
    return result;
}

bool EntityManager::isLogicalVillageMember(uint64_t entityId) const {
    const Entity* entity = entityById(entityId);
    return entity && entity->type == EntityType::Villager &&
        entity->villager.hasBed;
}

bool EntityManager::villagerUsable(
    uint64_t entityId, const glm::dvec3& origin,
    const glm::vec3& direction, float reach) const {
    const Entity* entity = entityById(entityId);
    return entity && entity->type == EntityType::Villager &&
        entity->health > 0.0f &&
        useRay(origin, direction, reach) == entityId;
}

void EntityManager::rebuildLogicalVillages() {
    m_logicalVillages.clear();
    auto floorSubchunk = [](int coordinate) {
        int result = coordinate / 16;
        if (coordinate % 16 < 0) --result;
        return result;
    };
    auto overlaps = [](const LogicalVillage& a, const LogicalVillage& b) {
        return a.minimumSubchunk.x <= b.maximumSubchunk.x &&
               a.maximumSubchunk.x >= b.minimumSubchunk.x &&
               a.minimumSubchunk.y <= b.maximumSubchunk.y &&
               a.maximumSubchunk.y >= b.minimumSubchunk.y &&
               a.minimumSubchunk.z <= b.maximumSubchunk.z &&
               a.maximumSubchunk.z >= b.minimumSubchunk.z;
    };
    for (const Entity& entity : m_entities) {
        if (entity.type != EntityType::Villager || entity.health <= 0.0f ||
            !entity.villager.hasBed) continue;
        const glm::ivec3 bed(floorSubchunk(entity.villager.claimedBed.x),
                            floorSubchunk(entity.villager.claimedBed.y),
                            floorSubchunk(entity.villager.claimedBed.z));
        const glm::ivec3 work=entity.villager.hasWorkstation ?
            glm::ivec3(floorSubchunk(entity.villager.claimedWorkstation.x),
                floorSubchunk(entity.villager.claimedWorkstation.y),
                floorSubchunk(entity.villager.claimedWorkstation.z)) : bed;
        LogicalVillage village;
        village.minimumSubchunk = glm::min(bed, work) - glm::ivec3(1);
        village.maximumSubchunk = glm::max(bed, work) + glm::ivec3(1);
        village.members.push_back(entity.id);
        m_logicalVillages.push_back(std::move(village));
    }
    for (size_t i = 0; i < m_logicalVillages.size(); ++i) {
        for (size_t j = i + 1; j < m_logicalVillages.size();) {
            if (!overlaps(m_logicalVillages[i], m_logicalVillages[j])) {
                ++j;
                continue;
            }
            m_logicalVillages[i].minimumSubchunk = glm::min(
                m_logicalVillages[i].minimumSubchunk,
                m_logicalVillages[j].minimumSubchunk);
            m_logicalVillages[i].maximumSubchunk = glm::max(
                m_logicalVillages[i].maximumSubchunk,
                m_logicalVillages[j].maximumSubchunk);
            m_logicalVillages[i].members.insert(
                m_logicalVillages[i].members.end(),
                m_logicalVillages[j].members.begin(),
                m_logicalVillages[j].members.end());
            m_logicalVillages.erase(m_logicalVillages.begin() + j);
            j = i + 1;
        }
    }
}

void EntityManager::spawnAroundPlayer(
    const glm::dvec3& playerPosition, bool spawnHostile, bool underground) {
    const size_t mobCount = static_cast<size_t>(std::count_if(
        m_entities.begin(), m_entities.end(),
        [spawnHostile](const Entity& entity) {
            return registeredMob(entity.type) &&
                   entity.type != EntityType::Villager && entity.type != EntityType::IronGolem &&
                   hostile(entity.type) == spawnHostile;
        }));
    if (mobCount >= (spawnHostile ? 24u : 16u)) return;
    const uint32_t random = hash32(++m_spawnSequence);
    const float angle = static_cast<float>(random % 6283) * 0.001f;
    const float distance = 18.0f + static_cast<float>((random >> 16) % 15);
    const int x = static_cast<int>(std::floor(playerPosition.x + std::cos(angle) * distance));
    const int z = static_cast<int>(std::floor(playerPosition.z + std::sin(angle) * distance));
    if(m_world.dimension()==DimensionId::Nether && (!spawnHostile || (random>>20)%8==0)) {
        for(int y=110;y>=31;--y)if(isLava(m_world.getBlock(x,y-1,z)) &&
            m_world.getBlock(x,y,z)==BlockId::AIR && m_world.getBlock(x,y+1,z)==BlockId::AIR) {
            const glm::dvec3 p(x+.5,y,z+.5);
            if(std::none_of(m_framePlayers.begin(),m_framePlayers.end(),[&](const auto& v){return glm::distance(v.player->getPosition(),p)<18;}))spawnMob(EntityType::Strider,p);
            return;
        }
        return;
    }
    int surface = -1;
    const int scanTop = m_world.dimension()==DimensionId::Nether?std::min(110,int(std::floor(playerPosition.y))+7):underground
        ? std::min(Config::WORLD_MAX_Y - 2,
                   static_cast<int>(std::floor(playerPosition.y)) + 7)
        : Config::WORLD_MAX_Y - 1;
    const int scanBottom = underground
        ? std::max(Config::WORLD_MIN_Y,
                   static_cast<int>(std::floor(playerPosition.y)) - 25)
        : Config::WORLD_MIN_Y;
    for (int y = scanTop; y >= scanBottom; --y) {
        if (isSolid(m_world.getBlock(x, y, z))) {
            const int candidate = y + 1;
            if (candidate + 1 < Config::WORLD_MAX_Y &&
                m_world.getBlock(x, candidate, z) == BlockId::AIR &&
                m_world.getBlock(x, candidate + 1, z) == BlockId::AIR) {
                surface = candidate;
                break;
            }
        }
    }
    if (!Config::isValidWorldY(surface) || surface + 1 >= Config::WORLD_MAX_Y ||
        m_world.getBlock(x, surface, z) != BlockId::AIR ||
        m_world.getBlock(x, surface + 1, z) != BlockId::AIR) return;
    for (const auto& view : m_framePlayers)
        if (!view.player->isSpectator() && glm::distance(glm::dvec3(x+.5, surface, z+.5), view.player->getPosition()) < 18) return;
    if (underground && m_world.hasSkyAccess(x, surface, z)) return;
    if(m_world.dimension()==DimensionId::Nether) {
        const auto& generator=m_world.generator().nether();
        EntityType native=EntityType::ZombifiedPiglin;
        switch(generator.biomeAt(x,z)) {
            case NetherBiome::Wastes:native=random%8==0?EntityType::Ghast:random%3==0?EntityType::Piglin:EntityType::ZombifiedPiglin;break;
            case NetherBiome::SoulSandValley:native=random%4==0?EntityType::Ghast:EntityType::Skeleton;break;
            case NetherBiome::CrimsonForest:native=random%3==0?EntityType::Hoglin:EntityType::Piglin;break;
            case NetherBiome::WarpedForest:native=EntityType::Enderman;break;
            case NetherBiome::BasaltDeltas:native=EntityType::MagmaCube;break;
        }
        for(const auto& structure:generator.structuresAt(x,z))if(structure.block(x,surface-1,z)) {
            if(structure.kind==NetherStructureKind::Fortress)native=random%3==0?EntityType::Blaze:EntityType::WitherSkeleton;
        }
        spawnMob(native,{x+.5,double(surface),z+.5});return;
    }
    if (spawnHostile && !hostileSpawnLightValid(m_world.getBlockLight(x, surface, z))) return;
    const EntityType passiveTypes[] = {
        EntityType::Cow, EntityType::Pig, EntityType::Sheep, EntityType::Chicken
    };
    const EntityType hostileTypes[] = {
        EntityType::Zombie, EntityType::Skeleton, EntityType::Spider,
        EntityType::Blastling
    };
    EntityType type = spawnHostile
        ? (underground ? caveHostileFor(
              m_world.caveBiomeAt(x, surface, z), random)
                       : hostileTypes[random % 4])
        : passiveTypes[random % 4];
    if (type == EntityType::Zombie &&
        naturalZombieBecomesVillager(hash32(random ^ 0x5a17u)))
        type = EntityType::ZombieVillager;
    std::vector<EntityType> pluginTypes;
    for(const auto& e:Plugins::content().extensions.entities)if(e.second.natural&&e.second.hostile==spawnHostile)pluginTypes.push_back(static_cast<EntityType>(e.first));
    if(!pluginTypes.empty()) {
        const auto choice=random%static_cast<uint32_t>(4+pluginTypes.size());if(choice>=4)type=pluginTypes[choice-4];
    }
    spawnMob(type, glm::dvec3(x + 0.5, static_cast<double>(surface), z + 0.5));
}

bool EntityManager::exposedToSky(const Entity& entity) const {
    const int worldX = static_cast<int>(std::floor(entity.position.x));
    const int worldZ = static_cast<int>(std::floor(entity.position.z));
    const int headTop = static_cast<int>(std::floor(
        entity.position.y + entitySize(entity).y - 1e-6));
    const int cx = World::worldToChunkX(static_cast<double>(worldX));
    const int cz = World::worldToChunkZ(static_cast<double>(worldZ));
    const int lx = worldX - cx * Config::CHUNK_SIZE_X;
    const int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    const auto found = m_aiChunks.find({cx,cz});
    if (found != m_aiChunks.end())
        return found->second->getColumnMaxY(lx,lz) <= headTop;
    return false;
}

bool EntityManager::touchesWater(const Entity& entity) const {
    const glm::vec3 size = entitySize(entity);
    const int x = static_cast<int>(std::floor(entity.position.x));
    const int z = static_cast<int>(std::floor(entity.position.z));
    const int feet = static_cast<int>(std::floor(entity.position.y + 0.05));
    const int head = static_cast<int>(std::floor(entity.position.y + size.y - 0.05));
    return isWater(m_world.getBlock(x, feet, z)) ||
           isWater(m_world.getBlock(x, head, z));
}

float EntityManager::damageEntity(Entity& entity, float damage,
                                  const glm::vec3& knockback,
                                  bool playerAttack,
                                  std::optional<glm::dvec3> source,
                                  uint64_t sourceId) {
    if (damage <= 0.0f || entity.health <= 0.0f) return 0.0f;
    const float accepted = PlayerPhysics::damageAfterImmunity(
        entity.hurtImmunity, damage, Config::PLAYER_HURT_IMMUNITY_SECONDS);
    if (accepted <= 0.0f) return 0.0f;
    // Player damage must use current membership, including the victim before
    // a lethal hit removes it and before the periodic village refresh.
    if(playerAttack && entity.type==EntityType::Villager)rebuildLogicalVillages();
    entity.health -= accepted;
    if(nativeNetherMob(entity.type)&&playerAttack) {entity.nether.angerTicks=600;if(entity.type==EntityType::Piglin||entity.type==EntityType::ZombifiedPiglin)angerPiglins(entity.position,entity.type==EntityType::ZombifiedPiglin);}
    if(entity.type==EntityType::Enderman)teleportEnderman(entity);
    if(entity.health<=0 && entity.type==EntityType::IronGolem)
        for(auto& resident:m_entities)if(resident.type==EntityType::Villager && resident.health>0 &&
            glm::distance(resident.position,entity.position)<96)resident.villager.defenseCooldown=Config::VILLAGE_DEFENSE_COOLDOWN;
    if(playerAttack && entity.type==EntityType::Villager)
        changeVillageReputation(entity,entity.health<=0 ? -25 : -10);
    entity.ai.nextDecision = m_aiTime;
    if (source) {
        entity.ai.danger = *source;
        if ((hostile(entity.type) || entity.type==EntityType::IronGolem) && (playerAttack || sourceId != 0)) {
            entity.ai.hasTarget = true;
            entity.ai.retaliating = true;
            entity.ai.targetId = playerAttack ? 0 : sourceId;
            if (playerAttack) entity.ai.targetPlayerId = sourceId;
            entity.ai.lastSeen = *source;
            entity.ai.memoryUntil = m_aiTime + Config::AI_TARGET_MEMORY;
        }
    } else entity.ai.danger = entity.position - glm::dvec3(knockback);
    if (!hostile(entity.type) && entity.type!=EntityType::IronGolem)
        entity.ai.panicUntil = m_aiTime + Config::AI_PANIC_SECONDS;
    entity.hurtFlashSeconds = 0.2f;
    if (registeredMob(entity.type))
        presentation().play(entity.type, entity.id, "hurt");
    entity.velocity += knockback;
    pluginEvent(entity,MC_ENTITY_DAMAGE);
    if (playerAttack && entity.type == EntityType::Spider)
        entity.spiderProvoked = true;
    return accepted;
}

bool EntityManager::collides(const Entity& entity, const glm::dvec3& position) const {
    const glm::vec3 size=entitySize(entity);
    const double halfX=size.x*0.5,halfZ=size.z*0.5;
    const int minX=static_cast<int>(std::floor(position.x-halfX));
    const int maxX=static_cast<int>(std::floor(position.x+halfX-1e-6));
    const int minY=static_cast<int>(std::floor(position.y));
    const int maxY=static_cast<int>(std::floor(position.y+size.y-1e-6));
    const int minZ=static_cast<int>(std::floor(position.z-halfZ));
    const int maxZ=static_cast<int>(std::floor(position.z+halfZ-1e-6));
    for(int y=minY;y<=maxY;++y) for(int z=minZ;z<=maxZ;++z) for(int x=minX;x<=maxX;++x) {
        const BlockId block = m_world.getBlock(x,y,z);
        const BlockCollisionBoxes boxes = blockCollisionBoxes(block);
        for (uint8_t i = 0; i < boxes.count; ++i) {
            const BlockCollisionBox& box = boxes.boxes[i];
            if (position.x-halfX < x+box.max.x && position.x+halfX > x+box.min.x &&
                position.y < y+box.max.y && position.y+size.y > y+box.min.y &&
                position.z-halfZ < z+box.max.z && position.z+halfZ > z+box.min.z)
                return true;
        }
    }
    return false;
}

void EntityManager::update(Player& player, float dt, bool isDay, bool peaceful,
                           bool playerTargetable, bool playerCanPickup,
                           bool thunderstorm, bool raining, uint64_t worldTick) {
    update(std::vector<EntityPlayerView>{{0, &player, playerTargetable, playerCanPickup}},
           dt, isDay, peaceful, thunderstorm, raining, worldTick);
}

void EntityManager::update(const std::vector<EntityPlayerView>& players, float dt,
                           bool isDay, bool peaceful, bool thunderstorm,
                           bool raining, uint64_t worldTick) {
    m_framePlayers = players;
    m_framePlayers.erase(std::remove_if(m_framePlayers.begin(), m_framePlayers.end(),
        [](const EntityPlayerView& view) { return !view.player; }), m_framePlayers.end());
    std::sort(m_framePlayers.begin(), m_framePlayers.end(),
        [](const EntityPlayerView& a, const EntityPlayerView& b) { return a.id < b.id; });
    m_framePlayers.erase(std::unique(m_framePlayers.begin(), m_framePlayers.end(),
        [](const EntityPlayerView& a, const EntityPlayerView& b) { return a.id == b.id; }), m_framePlayers.end());
    if (m_framePlayers.empty()) return;
    struct ClearPlayers { std::vector<EntityPlayerView>& players; ~ClearPlayers() { players.clear(); } } clear{m_framePlayers};
    dt = std::max(0.0f, dt);
    m_aiStats = {};
    for (const auto& entity : m_entities)
        m_dirtyEntityChunks.insert(entityChunk(entity.position));
    for (auto& dead : m_deadEntityRenders) {
        dead.elapsed = advanceDeathPresentation(dead.elapsed, dt);
        presentation().advance(dead.type, dead.id, dt);
    }
    m_deadEntityRenders.erase(std::remove_if(
        m_deadEntityRenders.begin(), m_deadEntityRenders.end(),
        [](const DeadEntityRender& dead) {
            return !deathPresentationVisible(dead.elapsed);
        }), m_deadEntityRenders.end());

    for (auto& entity : m_entities)
        PlayerPhysics::tickHurtImmunity(entity.hurtImmunity, dt);
    struct PendingExplosion {
        glm::dvec3 position;
        uint32_t seed;
        float power;
        bool mobExplosion = false;
        std::optional<uint64_t> playerOwner{};
    };
    std::vector<PendingExplosion> pendingExplosions;
    if (m_naturalSpawningEnabled && m_world.gameRules().boolean(GameRuleId::SpawnMobs)) {
        m_spawnTimer += dt;
        if (m_spawnTimer >= 4.0f) {
            const glm::dvec3 playerPosition = m_framePlayers[m_spawnPlayerCursor++ % m_framePlayers.size()].player->getPosition();
            const int px = static_cast<int>(std::floor(playerPosition.x));
            const int py = static_cast<int>(std::floor(playerPosition.y + 1.0));
            const int pz = static_cast<int>(std::floor(playerPosition.z));
            const bool underground = !m_world.hasSkyAccess(px, py, pz);
            bool spawnHostile = shouldAttemptHostileSpawn(underground, isDay, thunderstorm, peaceful);
            if(m_world.dimension()==DimensionId::Nether && !m_world.gameRules().boolean(GameRuleId::SpawnMonsters))spawnHostile=false;
            if ((m_world.dimension()==DimensionId::Nether || !underground || !peaceful) &&
                (!spawnHostile || m_world.gameRules().boolean(GameRuleId::SpawnMonsters)))
                spawnAroundPlayer(playerPosition, spawnHostile, underground);
            m_spawnTimer = 0.0f;
        }
    } else {
        m_spawnTimer = 0.0f;
    }
    m_spawnerTimer+=dt;
    if(m_spawnerTimer>=1.f){m_spawnerTimer=0;tickSpawners(peaceful);}
    prepareAiFrame(dt);
    refreshPoiIndex();
    m_villageRefreshSeconds += dt;
    if (m_villageRefreshSeconds >= 1.0f) {
        refreshVillageClaims();
        m_villageRefreshSeconds = 0.0f;
    }
    scheduleNavigation(m_framePlayers.front().player->getPosition());

    for (auto& entity : m_entities) {
        if (entity.health <= 0) continue;
        if (peaceful && hostile(entity.type)) { entity.health=0.0f;entity.despawning=true; continue; }
        entity.ageSeconds += dt;
        entity.actionCooldown = std::max(0.0f, entity.actionCooldown - dt);
        entity.hurtFlashSeconds = std::max(0.0f, entity.hurtFlashSeconds - dt);
        if(nativeProjectile(entity.type)){updateNativeProjectile(entity,dt);continue;}
        if (entity.type == EntityType::Arrow) {
            updateArrow(entity, dt);
            continue;
        }
        if (entity.type == EntityType::PrimedTnt) {
            integrateVelocity(entity, dt);
            if (entity.ageSeconds >= std::max(0.05f, entity.projectileDamage))
                entity.health = 0.0f;
            continue;
        }
        if (entity.type == EntityType::Item) {
            const auto cell=glm::ivec3(glm::floor(entity.position));
            const auto block=m_world.getBlock(cell.x,cell.y,cell.z);
            if((isLava(block)||block==BlockId::FIRE||block==BlockId::SOUL_FIRE)&&!isNetheriteEquipment(entity.item.id)&&entity.item.id!=ItemId::NETHERITE_INGOT&&entity.item.id!=ItemId::NETHERITE_SCRAP&&entity.item.id!=ItemId::ANCIENT_DEBRIS){entity.health=0;continue;}
            entity.velocity.y -= 20.0f * dt;
            const glm::dvec3 next = entity.position + glm::dvec3(entity.velocity) *
                static_cast<double>(dt);
            const int belowY = static_cast<int>(std::floor(next.y - 0.05f));
            const BlockId below = m_world.getBlock(
                static_cast<int>(std::floor(next.x)), belowY,
                static_cast<int>(std::floor(next.z)));
            const glm::vec3 localBelow(
                static_cast<float>(next.x-std::floor(next.x)),
                static_cast<float>(next.y-0.05-belowY),
                static_cast<float>(next.z-std::floor(next.z)));
            if (Config::isValidWorldY(belowY) &&
                pointInsideBlockCollision(below,localBelow)) {
                entity.velocity = glm::vec3(0.0f);
                float support=0.0f;
                const BlockCollisionBoxes boxes=blockCollisionBoxes(below);
                for(uint8_t i=0;i<boxes.count;++i) {
                    const auto& box=boxes.boxes[i];
                    if(localBelow.x>=box.min.x&&localBelow.x<=box.max.x&&
                       localBelow.z>=box.min.z&&localBelow.z<=box.max.z)
                        support=std::max(support,box.max.y);
                }
                entity.position.y = belowY + support + 0.05f;
            } else {
                entity.position = next;
            }
            for (const auto& view : m_framePlayers) {
                if (!view.canPickup || entity.actionCooldown > 0.0f || entity.health <= 0.0f ||
                    glm::distance(entity.position, view.player->getPosition()) >= 1.6) continue;
                if (pickupItemStack(view.player->inventory(), entity.item)) entity.health = 0.0f;
            }
            continue;
        }

        entity.ai.previousLocomotion = entity.locomotionVelocity;
        entity.locomotionVelocity = glm::vec3(0.0f);
        tickNativeState(entity,dt);
        if(entity.health<=0)continue;
        if(entity.type!=EntityType::Ghast && entity.type!=EntityType::Blaze)integrateVelocity(entity, dt);

        {
            const bool inWater = touchesWater(entity);
            const int x = static_cast<int>(std::floor(entity.position.x));
            const int y = static_cast<int>(std::floor(entity.position.y));
            const int z = static_cast<int>(std::floor(entity.position.z));
            const bool exposed = exposedToSky(entity);
            const bool wetByRain = raining && exposed &&
                m_world.precipitationAt(x, y, z) == PrecipitationType::Rain;
            const bool fireContact = isLava(m_world.getBlock(x,y,z)) || m_world.getBlock(x,y,z)==BlockId::SOUL_FIRE || m_world.getBlock(x, y, z) == BlockId::FIRE ||
                m_world.getBlock(x, static_cast<int>(std::floor(
                    entity.position.y + entitySize(entity).y * 0.5)), z) ==
                    BlockId::FIRE;
            const bool undead = entityTemplate(entity.type) == EntityType::Zombie ||
                                entityTemplate(entity.type) == EntityType::Skeleton ||
                                entityTemplate(entity.type) == EntityType::ZombieVillager;
            const bool immune=nativeFireImmune(entity.type)||entity.effects.level(StatusEffectType::FireResistance)>0;
            const bool ignited = !immune && (fireContact ||
                (dimensionRules(m_world.dimension()).skyLight && undead && isDay && !thunderstorm && exposed));
            const bool extinguished = immune || inWater || wetByRain;
            const float activeBurnTime = extinguished ? 0.0f :
                (ignited ? dt : std::min(dt, entity.burningSeconds));
            entity.burningSeconds = updateBurning(
                entity.burningSeconds, ignited, extinguished, dt);
            const int burnTicks = accumulateBurnDamage(
                entity.burnDamageSeconds, activeBurnTime);
            for (int tick = 0; tick < burnTicks; ++tick)
                damageEntity(entity, 4.0f, glm::vec3(0.0f), false);
            if (extinguished)
                entity.burnDamageSeconds = 0.0f;
            if (entity.health <= 0.0f) continue;
        }

        const EntityPlayerView* selected = nullptr;
        if (entity.ai.hasTarget && !entity.ai.targetId) {
            for (const auto& view : m_framePlayers)
                if (view.id == entity.ai.targetPlayerId && view.targetable)
                    selected = &view;
            if (!selected) {
                entity.ai.hasTarget = false;
                entity.ai.targetVisible = false;
                cancelAttack(entity);
                cancelNavigation(entity);
                entity.ai.nextDecision = m_aiTime;
            }
        }
        // Acquire the nearest eligible visible player. Keep the existing target
        // during wind-up and sight memory so impacts cannot jump between players.
        if (!selected) {
            double nearest = std::numeric_limits<double>::max();
            for (const auto& view : m_framePlayers) {
                if (!view.targetable) continue;
                const double distance = glm::distance(entity.position, view.player->getPosition());
                if (distance < nearest && aiClearSight(entity.position + glm::dvec3(0, 1.2, 0),
                                                       view.player->getEyePosition())) {
                    nearest = distance;
                    selected = &view;
                }
            }
        }
        const auto& fallback = *std::min_element(m_framePlayers.begin(), m_framePlayers.end(),
            [&](const EntityPlayerView& a, const EntityPlayerView& b) {
                return glm::distance(entity.position, a.player->getPosition()) <
                       glm::distance(entity.position, b.player->getPosition());
            });
        const auto& view = selected ? *selected : fallback;
        const bool nativeTarget=selected && view.targetable && nativeTargetsPlayer(entity,*view.player);
        if(!updateNativeMob(entity,*view.player,dt,nativeTarget) && !pluginAi(entity,dt))
            updateMobAi(entity,*view.player,dt,isDay,nativeTarget,worldTick,thunderstorm);
        if (entity.ai.hasTarget && !entity.ai.targetId)
            entity.ai.targetPlayerId = view.id;
    }
    for(auto& queued:m_nativeSpawns) {
        queued.id=m_nextId++;m_entities.push_back(std::move(queued));
    }
    m_nativeSpawns.clear();
    for(const auto& explosion:m_nativeExplosions)pendingExplosions.push_back({explosion.position,explosion.seed,explosion.power,true,explosion.playerOwner});
    m_nativeExplosions.clear();
    tickVillageLife(dt,worldTick);
    for (const auto& arrow : m_aiArrows)
        spawnArrow(arrow.position, arrow.velocity, arrow.damage, false, arrow.shooterId);
    m_aiArrows.clear();
    for (const auto& explosion : m_aiExplosions)
        pendingExplosions.push_back({explosion.position, explosion.seed, 2.5f, true});
    m_aiExplosions.clear();

    for (const auto& entity : m_entities)
        if (entity.type == EntityType::PrimedTnt && entity.health <= 0.0f &&
            m_world.gameRules().boolean(GameRuleId::TntExplodes))
            pendingExplosions.push_back(
                {entity.position, entity.behaviorSeed, 4.0f});
    for (const auto& explosion : pendingExplosions)
        explode(explosion.position, explosion.power, explosion.seed, explosion.mobExplosion, explosion.playerOwner);

    std::vector<Entity> deadMobs;
    for (const auto& entity : m_entities)
        if (entity.health <= 0.0f && entity.type != EntityType::Item &&
            entity.type != EntityType::PrimedTnt) {
            m_navigation.erase(entity.id);
            deadMobs.push_back(entity);
        }
    for (auto& entity : deadMobs) {
        pluginEvent(entity,MC_ENTITY_DEATH);
        if (registeredMob(entity.type) && !entity.despawning) {
            presentation().play(entity.type, entity.id, "death");
            m_deadEntityRenders.push_back({entity.id, entity.type,
                entity.position, entity.velocity, entity.facing,
                entity.behaviorSeed, 0.0f});
        }
    }
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
        [](const Entity& entity) {
            return entity.health <= 0.0f ||
                   (entity.type == EntityType::Item && entity.ageSeconds >= 300.0f) ||
                   (entity.type == EntityType::Arrow && entity.ageSeconds >= 60.0f) ||
                   (nativeProjectile(entity.type) && entity.ageSeconds>=30.f);
        }), m_entities.end());
    for (const auto& entity : deadMobs) {
        if(entity.despawning)continue;
        if(entity.type==EntityType::MagmaCube && entity.nether.magmaSize>0 && !peaceful) {
            for(int child=0;child<4;++child) {
                const auto position=entity.position+glm::dvec3((child%2-.5)*.9,0,(child/2-.5)*.9);
                // Test the smaller child's footprint, never the parent's bounds.
                Entity split;split.id=m_nextId++;split.type=EntityType::MagmaCube;split.position=position;
                split.nether.magmaSize=entity.nether.magmaSize-1;split.health=float(1u<<(2*split.nether.magmaSize));
                split.behaviorSeed=hash32(entity.behaviorSeed+child+1);
                if(!collides(split,position))m_entities.push_back(split);
            }
        } else dropMobLoot(entity);
    }
    for (const auto& entity : m_entities)
        m_dirtyEntityChunks.insert(entityChunk(entity.position));
}

void EntityManager::strikeLightning(Player& player, const glm::ivec3& position) {
    strikeLightning(std::vector<EntityPlayerView>{{0, &player, true, true}}, position);
}
void EntityManager::strikeLightning(const std::vector<EntityPlayerView>& players, const glm::ivec3& position) {
    const glm::dvec3 center = glm::dvec3(position) + glm::dvec3(0.5, 0.0, 0.5);
    for (auto& entity : m_entities) {
        if (entity.type == EntityType::Item || entity.type == EntityType::Arrow ||
            entity.health <= 0.0f) continue;
        if (glm::distance(entity.position, center) <= 3.0) {
            damageEntity(entity, 5.0f, glm::vec3(0.0f, 0.25f, 0.0f), false, center);
            entity.burningSeconds = std::max(entity.burningSeconds, 8.0f);
        }
    }
    for (const auto& view : players) if (view.player && view.damageable && view.player->isSurvival() && glm::distance(view.player->getPosition(), center) <= 3.0) {
        auto& player = *view.player;
        DamageSourceInfo source;
        source.amount = 5.0f;
        source.cause = DamageCause::Lightning;
        source.hasOrigin = true;
        source.origin = center;
        player.takeDamage(source);
        player.ignite(8.0f);
    }
}

void EntityManager::dropMobLoot(const Entity& entity) {
    if (!m_world.gameRules().boolean(GameRuleId::MobDrops)) return;
    if(const auto* e=Plugins::entityContent(static_cast<uint16_t>(entity.type))) {
        if(!e->drop.empty()&&e->dropCount)spawnItem(entity.position,{Plugins::resolveItem(e->drop),static_cast<uint8_t>(e->dropCount),0});
        return;
    }
    std::vector<ItemStack> loot;
    switch (entity.type) {
        case EntityType::Cow:
            loot = {{ItemId::RAW_BEEF, 1, 0}, {ItemId::LEATHER, 1, 0}}; break;
        case EntityType::Pig: loot = {{ItemId::RAW_PORKCHOP, 1, 0}}; break;
        case EntityType::Sheep:
            loot = {{ItemId::MUTTON, 1, 0}, {ItemId::WHITE_WOOL, 1, 0}}; break;
        case EntityType::Chicken:
            loot = {{ItemId::RAW_CHICKEN, 1, 0}, {ItemId::FEATHER, 1, 0}}; break;
        case EntityType::Zombie: loot = {{ItemId::ROTTEN_FLESH, 1, 0}}; break;
        case EntityType::Skeleton:
            loot = {{ItemId::BONE, 1, 0}, {ItemId::ARROW, 1, 0}}; break;
        case EntityType::Spider:
            loot={{ItemId::STRING,1,0}};if(entity.behaviorSeed%3==0)loot.push_back({ItemId::SPIDER_EYE,1,0});break;
        case EntityType::Ghast:loot={{ItemId::GHAST_TEAR,1,0},{ItemId::GUNPOWDER,1,0}};break;
        case EntityType::Blaze:loot={{ItemId::BLAZE_ROD,1,0}};break;
        case EntityType::WitherSkeleton:
            loot={{ItemId::BONE,1,0},{ItemId::COAL,1,0}};
            if(entity.behaviorSeed%40==0)loot.push_back({ItemId::WITHER_SKELETON_SKULL,1,0});break;
        case EntityType::MagmaCube:if(entity.behaviorSeed%4==0)loot={{ItemId::MAGMA_CREAM,1,0}};break;
        case EntityType::Piglin:case EntityType::PiglinBrute:case EntityType::ZombifiedPiglin:
            loot={{ItemId::GOLD_NUGGET,1,0}};break;
        case EntityType::Hoglin:loot={{ItemId::RAW_PORKCHOP,3,0},{ItemId::LEATHER,1,0}};break;
        case EntityType::Zoglin:loot={{ItemId::ROTTEN_FLESH,2,0}};break;
        case EntityType::Strider:loot={{ItemId::STRING,2,0}};if(entity.nether.saddled)loot.push_back({ItemId::SADDLE,1,0});break;
        case EntityType::Enderman:loot={{ItemId::ENDER_PEARL,1,0}};break;
        case EntityType::Blastling: loot = {{ItemId::GUNPOWDER, 1, 0}}; break;
        case EntityType::Villager: break;
        case EntityType::IronGolem: loot={{ItemId::IRON_INGOT,3,0}};break;
        case EntityType::ZombieVillager:
            loot = {{ItemId::ROTTEN_FLESH, 1, 0}};
            break;
        case EntityType::Arrow:
        case EntityType::Item: break;
        case EntityType::PrimedTnt: break;
    }
    for (const auto& stack : loot) spawnItem(entity.position, stack);
}

namespace {
bool meleeTarget(const Entity& entity) {
    return registeredMob(entity.type)&&entity.health>0;
}

bool rayEntityAabb(const glm::dvec3& origin, const glm::vec3& direction,
                   const Entity& entity, const glm::vec3& size,
                   float maximum, float& along) {
    const glm::dvec3 minimum(entity.position.x - size.x * 0.5,
                             entity.position.y,
                             entity.position.z - size.z * 0.5);
    const glm::dvec3 maximumPoint(entity.position.x + size.x * 0.5,
                                  entity.position.y + size.y,
                                  entity.position.z + size.z * 0.5);
    double nearDistance = 0.0;
    double farDistance = maximum;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 0.000001f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximumPoint[axis])
                return false;
            continue;
        }
        double first = (minimum[axis] - origin[axis]) / direction[axis];
        double second = (maximumPoint[axis] - origin[axis]) / direction[axis];
        if (first > second) std::swap(first, second);
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance) return false;
    }
    if (nearDistance < 0.0 || nearDistance > maximum) return false;
    along = static_cast<float>(nearDistance);
    return true;
}
}

MeleeAttackResult EntityManager::attackRay(
    const glm::dvec3& origin, const glm::vec3& direction,
    const MeleeAttackRequest& attack, uint64_t playerId,const std::string& owner) {
    MeleeAttackResult result;
    Entity* best = nullptr;
    float bestAlong = attack.reach + 1.0f;
    for (auto& entity : m_entities) {
        if (!meleeTarget(entity) && entity.type!=EntityType::GhastFireball) continue;
        float along = 0.0f;
        if (!rayEntityAabb(origin, direction, entity, entitySize(entity),
                           attack.reach, along) || along >= bestAlong)
            continue;
        if (along > 0.001f &&
            m_world.raycast(origin, direction, along).has_value())
            continue;
        best = &entity;
        bestAlong = along;
    }
    Player* otherPlayer = nullptr;
    for (const auto& view : m_pvpPlayers) {
        if (!view.player || !view.damageable || view.id == playerId || !view.player->isSurvival() || view.player->survivalStats().dead()) continue;
        Entity shape; shape.position = view.player->getPosition();
        float along = 0;
        if (!rayEntityAabb(origin, direction, shape, {.6f, view.player->currentHeight(), .6f}, attack.reach, along) || along >= bestAlong ||
            (along > .001f && m_world.raycast(origin, direction, along))) continue;
        otherPlayer = view.player; bestAlong = along;
    }
    if (!best && !otherPlayer) return result;
    if(best && !otherPlayer && best->type==EntityType::GhastFireball) {
        best->velocity=glm::normalize(direction)*18.f;best->playerOwned=true;best->shooterId=playerId==UINT64_MAX?0:playerId;
        best->nativeOwner=owner;best->pluginState=NativeProjectileState::encode(owner);
        result.foundTarget=true;result.primaryPosition=best->position;return result;
    }

    result.foundTarget = true;
    const glm::dvec3 primaryPosition = otherPlayer ? otherPlayer->getPosition() : best->position;
    const glm::vec3 primarySize = otherPlayer ? glm::vec3(.6f, otherPlayer->currentHeight(), .6f) : entitySize(*best);
    result.primaryPosition = primaryPosition + glm::dvec3(0, primarySize.y * .5f, 0);
    glm::vec3 horizontal(direction.x, 0.0f, direction.z);
    if (glm::length(horizontal) > 0.001f)
        horizontal = glm::normalize(horizontal) *
            (attack.sprintKnockback ? 6.0f : 4.0f);
    horizontal.y = 2.0f;
    const auto playerDamage = [&origin](Player& victim, float amount, glm::vec3 impulse) {
        DamageSourceInfo source; source.amount = amount; source.cause = DamageCause::Melee;
        source.shieldBlockable = source.hasOrigin = true; source.origin = origin; source.impulse = impulse;
        return victim.takeDamage(source).appliedDamage;
    };
    result.primaryDamage = otherPlayer ? playerDamage(*otherPlayer, attack.damage, horizontal) :
        damageEntity(*best, attack.damage, horizontal, true, origin, playerId == UINT64_MAX ? 0 : playerId);
    result.primaryDamaged = result.primaryDamage > 0.0f;

    if (!attack.sweeping || !result.primaryDamaged) return result;
    const glm::vec3 bestSize = primarySize;
    const glm::dvec3 sweepMin(
        primaryPosition.x - bestSize.x * 0.5 - 1.0,
        primaryPosition.y - 1.0,
        primaryPosition.z - bestSize.z * 0.5 - 1.0);
    const glm::dvec3 sweepMax(
        primaryPosition.x + bestSize.x * 0.5 + 1.0,
        primaryPosition.y + bestSize.y + 1.0,
        primaryPosition.z + bestSize.z * 0.5 + 1.0);
    for (auto& entity : m_entities) {
        if ((!otherPlayer && &entity == best) || !meleeTarget(entity)) continue;
        const glm::dvec3 center = entity.position +
            glm::dvec3(0.0, entitySize(entity).y * 0.5, 0.0);
        if (center.x < sweepMin.x || center.x > sweepMax.x ||
            center.y < sweepMin.y || center.y > sweepMax.y ||
            center.z < sweepMin.z || center.z > sweepMax.z ||
            glm::distance(center, origin) >= 3.0)
            continue;
        const glm::vec3 delta = glm::vec3(center - origin);
        const float distance = glm::length(delta);
        if (distance > 0.001f &&
            m_world.raycast(origin, delta / distance, distance).has_value())
            continue;
        glm::vec3 sweepKnockback(entity.position.x - primaryPosition.x, 0.0f,
                                 entity.position.z - primaryPosition.z);
        if (glm::length(sweepKnockback) > 0.001f)
            sweepKnockback = glm::normalize(sweepKnockback) * 4.0f;
        sweepKnockback.y = 2.0f;
        if (damageEntity(entity, CombatRules::SWEEP_DAMAGE,
                         sweepKnockback, true, origin, playerId == UINT64_MAX ? 0 : playerId) > 0.0f)
            result.sweptPositions.push_back(center);
    }
    for (const auto& view : m_pvpPlayers) {
        if (!view.player || !view.damageable || view.id == playerId || view.player == otherPlayer || !view.player->isSurvival() || view.player->survivalStats().dead()) continue;
        const auto center = view.player->getPosition() + glm::dvec3(0, view.player->currentHeight() * .5, 0);
        if (center.x < sweepMin.x || center.x > sweepMax.x || center.y < sweepMin.y || center.y > sweepMax.y ||
            center.z < sweepMin.z || center.z > sweepMax.z || glm::distance(center, origin) >= 3) continue;
        const auto delta = glm::vec3(center - origin); const float distance = glm::length(delta);
        if (distance > .001f && m_world.raycast(origin, delta / distance, distance)) continue;
        auto impulse = glm::vec3(center - primaryPosition); impulse.y = 0;
        if (glm::length(impulse) > .001f) impulse = glm::normalize(impulse) * 4.0f;
        impulse.y = 2;
        if (playerDamage(*view.player, CombatRules::SWEEP_DAMAGE, impulse) > 0) result.sweptPositions.push_back(center);
    }
    return result;
}

bool EntityManager::hasAttackTarget(
    const glm::dvec3& origin, const glm::vec3& direction, float reach) const {
    float bestAlong = reach + 1.0f;
    for (const auto& entity : m_entities) {
        if (!meleeTarget(entity) && entity.type!=EntityType::GhastFireball) continue;
        float along = 0.0f;
        if (!rayEntityAabb(origin, direction, entity, entitySize(entity),
                           reach, along) || along >= bestAlong)
            continue;
        if (along > 0.001f &&
            m_world.raycast(origin, direction, along).has_value())
            continue;
        return true;
    }
    return false;
}

void EntityManager::updateArrow(Entity& arrow, float dt) {
    if (arrow.inGround) {
        if (arrow.playerOwned && arrow.ageSeconds > 0.5f)
            for (const auto& view : m_framePlayers) {
                if (!view.canPickup || glm::distance(arrow.position, view.player->getPosition()) >= 1.6) continue;
                if (view.player->inventory().add({ItemId::ARROW, 1, 0}) == 0) {
                    arrow.health = 0.0f;
                    break;
                }
            }
        return;
    }
    const glm::dvec3 start = arrow.position;
    const glm::vec3 initialVelocity = arrow.velocity;
    double contactTime = dt;
    Entity* victim = nullptr;
    Player* hitPlayer = nullptr;
    if (!arrow.playerOwned || m_pvpEnabled) {
        for (const auto& view : m_framePlayers) {
            if (!view.damageable || (arrow.playerOwned && view.id == arrow.shooterId) || !view.player->isSurvival() || view.player->survivalStats().dead()) continue;
            const auto hit = projectileAabbHit(start, initialVelocity,
                view.player->getPosition() + glm::dvec3(-.3, 0, -.3),
                view.player->getPosition() + glm::dvec3(.3, view.player->currentHeight(), .3), contactTime);
            if (hit && (!hitPlayer || *hit < contactTime)) {
                contactTime = *hit;
                hitPlayer = view.player;
            }
        }
    }
    // Test each eligible target once and choose the first contact in flight order.
    for (auto& target : m_entities) {
        if (!meleeTarget(target) || (!arrow.playerOwned && target.id == arrow.shooterId)) continue;
        const glm::vec3 size = entitySize(target);
        const auto hit = projectileAabbHit(start, initialVelocity,
            target.position + glm::dvec3(-size.x * .5, 0, -size.z * .5),
            target.position + glm::dvec3(size.x * .5, size.y, size.z * .5),
            contactTime);
        if (hit && (*hit < contactTime || (*hit == contactTime && !hitPlayer &&
                    (!victim || target.id < victim->id)))) {
            contactTime = *hit;
            victim = &target;
            hitPlayer = nullptr;
        }
    }
    // Stop voxel traversal at the nearest target. Blocks win ties so touching
    // or embedded targets cannot bypass walls.
    const auto blockHit = projectileBlockHit(start, initialVelocity, contactTime,
        [this](int x, int y, int z) { return m_world.getBlock(x, y, z); });
    if (blockHit) {
        contactTime = *blockHit;
        victim = nullptr;
        hitPlayer = nullptr;
    }
    arrow.position = projectilePosition(start, initialVelocity, contactTime);
    arrow.velocity = projectileVelocityAfter(initialVelocity, static_cast<float>(contactTime));
    if (glm::length(arrow.velocity) > 0.0001f)
        arrow.facing = glm::normalize(arrow.velocity);
    if (!hitPlayer && !victim) {
        if (blockHit) {
            arrow.velocity = glm::vec3(0.0f);
            arrow.inGround = true;
            m_world.activateButtonsAtArrow(arrow.position);
        }
        return;
    }
    if(victim && victim->type==EntityType::Enderman && teleportEnderman(*victim))return;
    glm::vec3 knockback(arrow.velocity.x, 0.0f, arrow.velocity.z);
    if (glm::length(knockback) > 0.001f)
        knockback = glm::normalize(knockback) * 3.0f;
    knockback.y = 1.5f;
    // Incoming side of the contact supplies the shield/retaliation direction.
    const glm::dvec3 sourceOrigin = arrow.position - glm::dvec3(arrow.velocity) * .01;
    if (hitPlayer) {
        DamageSourceInfo source;
        source.amount = arrow.projectileDamage;
        source.cause = DamageCause::Projectile;
        source.shieldBlockable = true;
        source.hasOrigin = true;
        source.origin = sourceOrigin;
        source.impulse = knockback;
        if (hitPlayer->takeDamage(source).blocked) {
            arrow.velocity *= -0.2f;
            arrow.playerOwned = true;
            const auto reflected = std::find_if(m_framePlayers.begin(), m_framePlayers.end(), [hitPlayer](const EntityPlayerView& view) { return view.player == hitPlayer; });
            arrow.shooterId = reflected == m_framePlayers.end() ? UINT64_MAX : reflected->id;
            if (glm::length(arrow.velocity) > 0.0001f)
                arrow.facing = glm::normalize(arrow.velocity);
            // Consume the rest of this frame after reflection. Ownership now
            // excludes the player, so this continuation cannot reflect again.
            const float remaining = dt - static_cast<float>(contactTime);
            if (remaining > 0.0f) updateArrow(arrow, remaining);
            return;
        }
    } else {
        damageEntity(*victim, arrow.projectileDamage, knockback,
                     arrow.playerOwned, sourceOrigin, arrow.shooterId);
    }
    arrow.health = 0.0f;
}

void EntityManager::explode(const glm::dvec3& center,
                            float power, uint32_t eventSeed, bool mobExplosion,
                            std::optional<uint64_t> playerOwner) {
    const float effectRadius = power * 2.0f;
    auto impactAt = [&](const glm::dvec3& target) {
        const glm::dvec3 delta = target - center;
        const double distance = glm::length(delta);
        if (distance >= effectRadius) return 0.0f;
        bool clearSight = true;
        if (distance > 0.01) {
            const auto blocked = m_world.raycast(
                center, glm::normalize(glm::vec3(delta)), static_cast<float>(distance));
            clearSight = !blocked.has_value();
        }
        return explosionImpact(
            static_cast<float>(distance), effectRadius, clearSight);
    };

    for (auto& entity : m_entities) {
        if (entity.health <= 0.0f || entity.type == EntityType::PrimedTnt) continue;
        const float impact = impactAt(entity.position + glm::dvec3(0.0, .5, 0.0));
        if (impact <= 0.0f) continue;
        glm::vec3 direction = glm::vec3(entity.position - center);
        if (glm::length(direction) > .001f) direction = glm::normalize(direction);
        direction.y = std::max(direction.y, .25f);
        if (entity.type == EntityType::Item) {
            entity.velocity += direction * impact * 4.0f;
        } else {
            const float damage = std::floor((impact * impact + impact) * 7.0f + 1.0f);
            damageEntity(entity, damage, direction * impact * 3.5f, false, center);
        }
    }
    for (const auto& view : m_framePlayers) {
        Player& player = *view.player;
        if (playerOwner && view.id != *playerOwner && !m_pvpEnabled) continue;
        if (!view.damageable || !player.isSurvival() || player.survivalStats().dead()) continue;
        const float impact = impactAt(player.getPosition() + glm::dvec3(0.0, 1.0, 0.0));
        if (impact > 0.0f) {
            const float damage = std::floor((impact * impact + impact) * 7.0f + 1.0f);
            glm::vec3 direction = glm::vec3(player.getPosition() - center);
            if (glm::length(direction) > .001f) direction = glm::normalize(direction);
            direction.y = std::max(direction.y, .3f);
            DamageSourceInfo source;
            source.amount = damage;
            source.cause = DamageCause::Explosion;
            source.shieldBlockable = true;
            source.hasOrigin = true;
            source.origin = center;
            source.impulse = direction * impact * 3.5f;
            player.takeDamage(source);
        }
    }

    std::vector<glm::ivec3> chain;
    size_t spawnedDrops = 0;
    const bool destroysBlocks = !mobExplosion || m_world.gameRules().boolean(GameRuleId::MobGriefing);
    const bool decay = m_world.gameRules().boolean(mobExplosion
        ? GameRuleId::MobExplosionDropDecay : GameRuleId::TntExplosionDropDecay);
    const int radius = destroysBlocks ? static_cast<int>(std::ceil(power)) : -1;
    const ItemStack diamondPick{ItemId::DIAMOND_PICKAXE, 1, 0};
    const glm::ivec3 blockCenter(glm::floor(center));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const glm::ivec3 p = blockCenter + glm::ivec3(dx, dy, dz);
                if (!Config::isValidWorldY(p.y)) continue;
                const float distance = glm::length(glm::vec3(dx, dy, dz));
                if (distance > power) continue;
                const BlockId block = m_world.getBlock(p.x, p.y, p.z);
                if (block == BlockId::AIR || block == BlockId::BEDROCK ||
                    block == BlockId::OBSIDIAN || isFluid(block)) continue;
                uint32_t random = hash32(eventSeed ^
                    static_cast<uint32_t>(p.x) * 73428767u ^
                    static_cast<uint32_t>(p.y) * 912931u ^
                    static_cast<uint32_t>(p.z) * 438289u);
                const float resistance = std::max(0.0f, getBlockSurvivalProps(block).hardness);
                const float strength = power * (0.75f + (random & 255u) / 512.0f);
                if (distance + resistance * .18f > strength) continue;
                if (block == BlockId::TNT) {
                    chain.push_back(p);
                } else if (m_world.gameRules().boolean(GameRuleId::BlockDrops) && spawnedDrops < 32 &&
                           explosionDropSurvives(random, power, decay)) {
                    for (const ItemStack& drop : getBlockDrops(block, diamondPick, random)) {
                        if (spawnedDrops >= 32) break;
                        const glm::vec3 velocity(
                            (static_cast<int>((random >> 8) & 15) - 7) * .05f,
                            .6f + ((random >> 16) & 7) * .04f,
                            (static_cast<int>((random >> 20) & 15) - 7) * .05f);
                        spawnItem(glm::dvec3(p) + glm::dvec3(.5), drop, velocity);
                        ++spawnedDrops;
                    }
                }
                m_world.setBlock(p.x, p.y, p.z, BlockId::AIR);
            }
        }
    }
    for (const glm::ivec3& p : chain) {
        // The TNT block was already cleared in the destruction loop above;
        // prime it now so a chain reaction actually propagates instead of
        // silently vanishing.
        const uint32_t random = hash32(eventSeed ^ static_cast<uint32_t>(p.x) ^
                                       (static_cast<uint32_t>(p.z) << 16));
        primeTnt(p, .5f + static_cast<float>(random % 1001) / 1000.0f, false);
    }
    m_explosionEvents.push_back(center);
}

glm::vec3 EntityManager::renderSize(EntityType type) {
    switch(type) {
#define NETHER_SIZE(type,egg,key,label,h,x,y,z,s,d,monster,fire) case EntityType::type:return {x,y,z};
        MC_NETHER_MOBS(NETHER_SIZE)
#undef NETHER_SIZE
        default:break;
    }
    if(nativeProjectile(type))return {.3f,.3f,.3f};
    if(const auto* e=Plugins::entityContent(static_cast<uint16_t>(type)))return {e->size[0],e->size[1],e->size[2]};
    switch (type) {
        case EntityType::Item: return {0.25f, 0.25f, 0.25f};
        case EntityType::Arrow: return {0.08f,0.08f,0.75f};
        case EntityType::PrimedTnt: return {0.98f,0.98f,0.98f};
        case EntityType::Chicken: return {0.45f, 0.65f, 0.45f};
        case EntityType::Spider: return {1.2f, 0.55f, 1.2f};
        case EntityType::IronGolem: return {1.35f,2.7f,1.0f};
        case EntityType::Villager:
        case EntityType::ZombieVillager: return {0.62f,1.80f,0.48f};
        case EntityType::Cow:
        case EntityType::Pig:
        case EntityType::Sheep: return {0.9f, 1.2f, 1.3f};
        default: return {0.65f, 1.75f, 0.65f};
    }
}

bool EntityManager::arrowTouchesButton(const glm::ivec3& position) const {
    ButtonState state;
    if(!decodeButton(m_world.getBlock(position.x,position.y,position.z),state)) return false;
    state.pressed=false;
    const auto box=blockSelectionBoxes(buttonBlock(state)).boxes[0];
    for(const auto& arrow:m_entities) {
        if(arrow.type!=EntityType::Arrow || !arrow.inGround || arrow.health<=0) continue;
        const glm::dvec3 local=arrow.position-glm::dvec3(position);
        if(glm::all(glm::greaterThanEqual(local,glm::dvec3(box.min)-.03)) &&
           glm::all(glm::lessThanEqual(local,glm::dvec3(box.max)+.03))) return true;
    }
    return false;
}

void EntityManager::explodeAt(const glm::dvec3& center,float power,const std::vector<EntityPlayerView>& players) {
    auto previous=std::move(m_framePlayers);
    struct Restore {std::vector<EntityPlayerView>& current;std::vector<EntityPlayerView>& previous;~Restore(){current=std::move(previous);}} restore{m_framePlayers,previous};
    m_framePlayers=players;
    explode(center,power,hash32(static_cast<uint32_t>(static_cast<int64_t>(std::floor(center.x)))^static_cast<uint32_t>(static_cast<int64_t>(std::floor(center.z)))),false);
}


void EntityManager::spawnNativeProjectile(EntityType type,const glm::dvec3& origin,
    const glm::vec3& velocity,uint64_t playerId,ItemStack item,const std::string& owner) {
    if(!nativeProjectile(type))return;
    Entity projectile;projectile.id=m_nextId++;projectile.type=type;projectile.health=1;
    projectile.position=origin;projectile.velocity=velocity;projectile.playerOwned=true;
    projectile.shooterId=playerId;projectile.item=item;projectile.nativeOwner=owner;projectile.pluginState=NativeProjectileState::encode(owner);m_entities.push_back(projectile);
}

void EntityManager::applyEntityPotion(Entity& target,const PotionDefinition& potion,float intensity) {
    auto effect=potion.effect;
    if(undeadEntity(target.type)) {
        if(effect.type==StatusEffectType::Healing)effect.type=StatusEffectType::Harming;
        else if(effect.type==StatusEffectType::Harming)effect.type=StatusEffectType::Healing;
        else if(effect.type==StatusEffectType::Regeneration)return;
    }
    float maximum=nativeMobHealth(target.type);
    switch(entityTemplate(target.type)) {
        case EntityType::Cow:case EntityType::Pig:case EntityType::Sheep:maximum=10;break;
        case EntityType::Chicken:maximum=4;break;
        case EntityType::Spider:maximum=16;break;
        case EntityType::IronGolem:maximum=100;break;
        case EntityType::MagmaCube:maximum=float(1u<<(2*target.nether.magmaSize));break;
        default:break;
    }
    if(effect.type==StatusEffectType::Healing)target.health=std::min(maximum,target.health+float(4u<<effect.amplifier)*intensity);
    else if(effect.type==StatusEffectType::Harming)damageEntity(target,float(6u<<effect.amplifier)*intensity,glm::vec3(0),false);
    else target.effects.apply(effect.type,effect.amplifier,uint32_t(effect.remaining*intensity));
}

void EntityManager::updateNativeProjectile(Entity& projectile,float dt) {
    if(projectile.playerOwned) {
        if(projectile.nativeOwner.empty())projectile.nativeOwner=NativeProjectileState::owner(projectile.pluginState);
        const auto owner=std::find_if(m_framePlayers.begin(),m_framePlayers.end(),[&](const auto& v){return v.player->persistentIdentity()==projectile.nativeOwner;});
        // Missing/offline owners cannot fall back to the host's room ID zero.
        if(projectile.nativeOwner.empty()||owner==m_framePlayers.end()){projectile.health=0;return;}
        projectile.shooterId=owner->id;
    }
    const bool curved=projectile.type==EntityType::ThrownPotion||projectile.type==EntityType::EnderPearl;
    const auto start=projectile.position;const auto velocity=projectile.velocity;
    double contact=dt;Entity* victim=nullptr;Player* player=nullptr;bool hit=false;
    auto intersects=[&](const glm::dvec3& minimum,const glm::dvec3& maximum) -> std::optional<double> {
        if(curved)return projectileAabbHit(start,velocity,minimum,maximum,contact);
        const double speed=glm::length(velocity);if(speed<.001)return {};
        double near=0,far=contact;
        for(int axis=0;axis<3;++axis) {
            if(std::abs(velocity[axis])<1e-8) {if(start[axis]<minimum[axis]||start[axis]>maximum[axis])return {};continue;}
            double a=(minimum[axis]-start[axis])/velocity[axis],b=(maximum[axis]-start[axis])/velocity[axis];
            if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);
            if(near>far)return {};
        }
        return near;
    };
    for(auto& target:m_entities) {
        if(!registeredMob(target.type)||target.health<=0||(!projectile.playerOwned&&target.id==projectile.shooterId))continue;
        const auto size=entitySize(target);
        const auto t=intersects(target.position+glm::dvec3(-size.x*.5,0,-size.z*.5),target.position+glm::dvec3(size.x*.5,size.y,size.z*.5));
        if(t&&*t<=contact){contact=*t;victim=&target;player=nullptr;hit=true;}
    }
    for(const auto& view:m_framePlayers) {
        if(!view.damageable||!view.player->isSurvival()||(projectile.playerOwned&&view.id==projectile.shooterId))continue;
        if(projectile.playerOwned && !m_pvpEnabled &&
            (projectile.type==EntityType::GhastFireball||projectile.type==EntityType::BlazeFireball))continue;
        const auto t=intersects(view.player->getPosition()+glm::dvec3(-.3,0,-.3),view.player->getPosition()+glm::dvec3(.3,view.player->currentHeight(),.3));
        if(t&&*t<contact){contact=*t;player=view.player;victim=nullptr;hit=true;}
    }
    if(curved) {
        const auto t=projectileBlockHit(start,velocity,contact,[&](int x,int y,int z){return m_world.getBlock(x,y,z);});
        if(t){contact=*t;player=nullptr;victim=nullptr;hit=true;}
        projectile.position=projectilePosition(start,velocity,contact);
        projectile.velocity=projectileVelocityAfter(velocity,float(contact));
    } else {
        const float speed=glm::length(velocity);
        if(speed>.001f)if(const auto block=m_world.raycast(start,velocity/speed,float(contact)*speed)) {
            contact=block->distance/speed;player=nullptr;victim=nullptr;hit=true;
        }
        projectile.position=start+glm::dvec3(velocity)*contact;
    }
    if(!hit)return;
    projectile.health=0;
    if(projectile.type==EntityType::ThrownPotion) {
        if(const auto potion=potionDefinition(projectile.item.id)) {
            for(auto& target:m_entities)if(registeredMob(target.type)&&target.health>0) {
                const auto center=target.position+glm::dvec3(0,entitySize(target).y*.5,0);
                const double distance=glm::distance(center,projectile.position);
                if((&target==victim||distance<4)&&aiClearSight(projectile.position,center))
                    applyEntityPotion(target,*potion,&target==victim?1.f:float(1-distance/4));
            }
            for(const auto& view:m_framePlayers) {
                const auto center=view.player->getPosition()+glm::dvec3(0,.9,0);
                const double distance=glm::distance(center,projectile.position);
                if(view.damageable&&(view.player==player||distance<4)&&
                    (view.id==projectile.shooterId||m_pvpEnabled||(potion->effect.type!=StatusEffectType::Harming&&potion->effect.type!=StatusEffectType::Weakness))&&aiClearSight(projectile.position,center))
                    view.player->applyPotion(*potion,view.player==player?1.f:float(1-distance/4));
            }
        }
        return;
    }
    if(projectile.type==EntityType::EnderPearl) {
        for(const auto& view:m_framePlayers)if(view.id==projectile.shooterId && !view.player->survivalStats().dead()) {
            // Keep the player on the approached side of the collision surface.
            const auto normal=glm::length(velocity)>.001f?glm::normalize(glm::dvec3(velocity)):glm::dvec3(0);
            const auto proposed=projectile.position-normal*.65;
            if(!riderCollides(proposed)) {view.player->teleport(proposed);view.player->takeDamage(5,true);}
        }
        return;
    }
    if(victim) {
        const bool immune=nativeFireImmune(victim->type)||victim->effects.level(StatusEffectType::FireResistance);
        if(projectile.type==EntityType::GhastFireball || !immune)
            damageEntity(*victim,projectile.playerOwned&&victim->type==EntityType::Ghast?1000.f:projectile.type==EntityType::GhastFireball?6.f:5.f,glm::vec3(0),projectile.playerOwned,projectile.position,projectile.shooterId);
        if(!immune)victim->burningSeconds=5;
    }
    if(player && (projectile.type==EntityType::GhastFireball || !player->statusEffects().level(StatusEffectType::FireResistance))) {
        DamageSourceInfo source;source.amount=projectile.type==EntityType::GhastFireball?6.f:5.f;
        source.cause=DamageCause::Projectile;source.shieldBlockable=true;source.hasOrigin=true;source.origin=start;
        if(player->takeDamage(source).appliedDamage>0)player->ignite(5);
    }
    if(projectile.type==EntityType::GhastFireball)m_nativeExplosions.push_back({projectile.position,projectile.behaviorSeed,1.f,
        projectile.playerOwned?std::optional<uint64_t>(projectile.shooterId):std::nullopt});
    else {
        const auto pos=glm::ivec3(glm::floor(projectile.position));
        if(m_world.getBlock(pos.x,pos.y,pos.z)==BlockId::AIR&&isSolid(m_world.getBlock(pos.x,pos.y-1,pos.z)))m_world.setBlock(pos.x,pos.y,pos.z,BlockId::FIRE);
    }
}

bool EntityManager::riding(uint64_t playerId) const {
    return std::any_of(m_entities.begin(),m_entities.end(),[&](const auto& entity){return entity.riderId==playerId;});
}
void EntityManager::dismount(uint64_t playerId) {
    for(auto& entity:m_entities)if(entity.riderId==playerId)entity.riderId=UINT64_MAX;
}
bool EntityManager::useNativeMob(Player& player,uint64_t playerId) {
    if(player.isSpectator())return false;
    auto& selected=player.inventory().slot(size_t(player.selectedSlot()));
    if(riding(playerId)) {
        if(selected.id==ItemId::WARPED_FUNGUS_ON_A_STICK) {
            for(auto& entity:m_entities)if(entity.riderId==playerId && entity.nether.boostTicks==0) {
                if(player.isSurvival()&&selected.damage+1>=getItemProps(selected.id).maxDurability)return false;
                entity.nether.boostTicks=100;if(player.isSurvival())++selected.damage;return true;
            }
        }
        return false;
    }
    Entity* nearest=nullptr;float along=player.isSurvival()?3.f:6.f;
    for(auto& entity:m_entities) {
        if(!registeredMob(entity.type)||entity.health<=0)continue;
        float hit=0;
        if(rayEntityAabb(player.getEyePosition(),player.getForward(),entity,entitySize(entity),along,hit)&&
            !m_world.raycast(player.getEyePosition(),player.getForward(),hit)) {nearest=&entity;along=hit;}
    }
    if(!nearest)return false;
    auto consume=[&](){if(player.isSurvival()&&--selected.count==0)selected.clear();};
    if(nearest->type==EntityType::Piglin && selected.id==ItemId::GOLD_INGOT && !nearest->nether.angerTicks && !nearest->nether.barterTicks) {
        consume();nearest->nether.barterTicks=120;nearest->ai={};return true;
    }
    if(nearest->type==EntityType::Strider) {
        if(selected.id==ItemId::SADDLE&&!nearest->nether.saddled){consume();nearest->nether.saddled=1;return true;}
        if(nearest->nether.saddled && nearest->riderId==UINT64_MAX && !player.sneakInput()&&!riderCollides(nearest->position+glm::dvec3(0,1.7,0))) {nearest->riderId=playerId;return true;}
    }
    if(nearest->type==EntityType::ZombieVillager && selected.id==ItemId::GOLDEN_APPLE && nearest->effects.level(StatusEffectType::Weakness)>0 && !nearest->nether.cureTicks) {
        consume();nearest->nether.cureTicks=3600;nearest->effects.clear();return true;
    }
    return false;
}


void EntityManager::tickSpawners(bool peaceful) {
    if(peaceful||!m_world.gameRules().boolean(GameRuleId::SpawnMobs)||!m_world.gameRules().boolean(GameRuleId::SpawnMonsters))return;
    const size_t monsters=std::count_if(m_entities.begin(),m_entities.end(),[](const auto& entity){return entity.health>0&&hostile(entity.type);});
    if(monsters>=64)return;
    for(const auto& position:m_world.loadedSpawners()) {
        if(std::count_if(m_entities.begin(),m_entities.end(),[](const auto& e){return e.health>0&&hostile(e.type);})>=64)break;
        const glm::dvec3 center=glm::dvec3(position)+glm::dvec3(.5);
        if(std::none_of(m_framePlayers.begin(),m_framePlayers.end(),[&](const auto& view){return view.targetable&&glm::distance(view.player->getPosition(),center)<16;}))continue;
        auto* state=m_world.getBlockEntity(position);if(!state)continue;
        if(state->burnRemaining>20){state->burnRemaining-=20;continue;}
        const auto block=m_world.getBlock(position.x,position.y,position.z);
        if(block!=BlockId::BLAZE_SPAWNER&&block!=BlockId::MAGMA_SPAWNER)continue;
        const auto type=block==BlockId::BLAZE_SPAWNER?EntityType::Blaze:EntityType::MagmaCube;
        const auto count=std::count_if(m_entities.begin(),m_entities.end(),[&](const auto& e){return e.type==type&&e.health>0&&glm::distance(e.position,center)<9;});
        if(count>=6){state->burnRemaining=100;continue;}
        const auto seed=hash32(uint32_t(position.x)^uint32_t(position.z)*31u^uint32_t(m_aiTime*20));
        for(int i=0;i<4 && count+i<6 && monsters+size_t(i)<64;++i) {
            if(std::count_if(m_entities.begin(),m_entities.end(),[](const auto& e){return e.health>0&&hostile(e.type);})>=64)break;
            const auto roll=hash32(seed+uint32_t(i));
            spawnMob(type,center+glm::dvec3(int(roll%7)-3,int((roll>>8)%3)-1,int((roll>>16)%7)-3));
        }
        state->burnRemaining=200+seed%401;
    }
}


std::vector<uint64_t> EntityManager::portalCandidates(float dt) {
    std::vector<uint64_t> ready;
    for(auto& entity:m_entities) {
        if(entity.health<=0||(entity.type!=EntityType::Item&&!registeredMob(entity.type))||entity.riderId!=UINT64_MAX)continue;
        const bool cooling=entity.portalCooldown>0;
        entity.portalCooldown=std::max(0.f,entity.portalCooldown-dt);entity.portalRetry=std::max(0.f,entity.portalRetry-dt);
        const auto p=glm::ivec3(glm::floor(entity.position));
        const auto block=m_world.getBlock(p.x,p.y,p.z);
        if(!NetherPortal::isPortal(block))continue;
        if(cooling){entity.portalCooldown=1;continue;}
        if(entity.portalRetry>0)continue;
        const auto frame=NetherPortal::find([&](glm::ivec3 q){return m_world.getBlock(q.x,q.y,q.z);},p,block==BlockId::NETHER_PORTAL_Z,true);
        if(frame&&ready.size()<16)ready.push_back(entity.id);
    }
    return ready;
}
void EntityManager::deferPortal(uint64_t id) {for(auto& entity:m_entities)if(entity.id==id){entity.portalRetry=1;break;}}

bool EntityManager::transferTo(uint64_t id,EntityManager& target,const glm::dvec3& desired,const std::filesystem::path& root) {
    if(&target==this||!m_saveStore||!target.m_saveStore)return false;
    const auto found=std::find_if(m_entities.begin(),m_entities.end(),[&](const auto& e){return e.id==id;});
    if(found==m_entities.end()||found->health<=0||found->riderId!=UINT64_MAX||
        (found->type!=EntityType::Item&&!registeredMob(found->type)))return false;
    Entity moved=*found;std::optional<glm::dvec3> landing;
    const auto size=entitySize(moved);
    for(const auto offset:{glm::dvec3(0),glm::dvec3(0,0,3),glm::dvec3(0,0,-3),glm::dvec3(3,0,0),glm::dvec3(-3,0,0)}) {
        const auto p=desired+offset;if(target.collides(moved,p))continue;
        bool safe=true;
        for(int z=int(std::floor(p.z-size.z*.5));z<=int(std::floor(p.z+size.z*.5-1e-6))&&safe;++z)
            for(int x=int(std::floor(p.x-size.x*.5));x<=int(std::floor(p.x+size.x*.5-1e-6))&&safe;++x) {
                for(int y=int(std::floor(p.y));y<=int(std::floor(p.y+size.y-1e-6));++y) {
                    const auto block=target.m_world.getLoadedBlock(x,y,z);
                    if(!block||isLava(*block)||*block==BlockId::FIRE||*block==BlockId::SOUL_FIRE){safe=false;break;}
                }
                if(moved.type!=EntityType::Ghast&&moved.type!=EntityType::Blaze) {
                    const auto floor=target.m_world.getLoadedBlock(x,int(std::floor(p.y-.05)),z);
                    if(!floor||!isFullCollisionBlock(*floor)||*floor==BlockId::MAGMA_BLOCK)safe=false;
                }
            }
        if(safe){landing=p;break;}
    }
    if(!landing)return false;
    const auto sourceKey=entityChunk(found->position),targetKey=entityChunk(*landing);
    const size_t index=size_t(found-m_entities.begin());auto saved=saveEntities();auto record=saved[index];saved.erase(saved.begin()+index);
    auto belongs=[](const auto& e,const auto& key){return entityChunk(e.position)==key;};
    saved.erase(std::remove_if(saved.begin(),saved.end(),[&](const auto& e){return !belongs(e,sourceKey);}),saved.end());
    auto targetSaved=target.saveEntities();targetSaved.erase(std::remove_if(targetSaved.begin(),targetSaved.end(),[&](const auto& e){return !belongs(e,targetKey);}),targetSaved.end());
    record.position=*landing;record.velocity=glm::vec3(0);targetSaved.push_back(record);
    moved.position=*landing;moved.velocity=glm::vec3(0);moved.ai={};cancelAttack(moved);
    moved.portalCooldown=1;moved.portalRetry=0;moved.id=target.m_nextId;
    target.m_entities.reserve(target.m_entities.size()+1);
    EntityTransferJournal::commit(root,{m_world.dimension(),sourceKey.first,sourceKey.second,std::move(saved)},
        {target.m_world.dimension(),targetKey.first,targetKey.second,std::move(targetSaved)});
    m_navigation.erase(id);m_entities.erase(m_entities.begin()+index);
    ++target.m_nextId;target.m_entities.push_back(std::move(moved));
    m_dirtyEntityChunks.insert(sourceKey);target.m_dirtyEntityChunks.insert(targetKey);return true;
}


bool EntityManager::riderCollides(const glm::dvec3& position) const {
    // The existing conservative zombie footprint plus the top six centimeters
    // covers the standing player's complete body without changing mob sizes.
    Entity shape;shape.type=EntityType::Zombie;
    for(int z=int(std::floor(position.z-.325));z<=int(std::floor(position.z+.325));++z)
        for(int x=int(std::floor(position.x-.325));x<=int(std::floor(position.x+.325));++x)
            for(int y=int(std::floor(position.y));y<=int(std::floor(position.y+Config::PLAYER_HEIGHT));++y)
                if(!m_world.getLoadedBlock(x,y,z))return true;
    return collides(shape,position)||collides(shape,position+glm::dvec3(0,.06,0));
}

bool EntityManager::tryDismount(Player& player,uint64_t playerId) {
    const auto mount=std::find_if(m_entities.begin(),m_entities.end(),[&](const auto& e){return e.riderId==playerId;});
    if(mount==m_entities.end())return true;
    const glm::ivec3 origin(glm::floor(mount->position));
    for(int radius=1;radius<=8;++radius)for(int dy:{0,1,-1,2,-2,3,-3,4,-4})
        for(int z=-radius;z<=radius;++z)for(int x=-radius;x<=radius;++x) {
            if(std::max(std::abs(x),std::abs(z))!=radius)continue;
            const auto p=origin+glm::ivec3(x,dy,z);
            const auto floor=m_world.getLoadedBlock(p.x,p.y-1,p.z),feet=m_world.getLoadedBlock(p.x,p.y,p.z),head=m_world.getLoadedBlock(p.x,p.y+1,p.z);
            if(!floor||!feet||!head||!isFullCollisionBlock(*floor)||*floor==BlockId::MAGMA_BLOCK||isFluid(*feet)||isFluid(*head)||
                *feet==BlockId::FIRE||*feet==BlockId::SOUL_FIRE||*head==BlockId::FIRE||*head==BlockId::SOUL_FIRE)continue;
            const auto point=glm::dvec3(p)+glm::dvec3(.5,.01,.5);
            if(riderCollides(point))continue;
            dismount(playerId);player.teleport(point);return true;
        }
    return false;
}
