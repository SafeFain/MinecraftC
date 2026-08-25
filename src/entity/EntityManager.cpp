#include "entity/EntityManager.h"
#include "entity/EntityLogic.h"
#include "entity/ProjectileLogic.h"

#include "player/Player.h"
#include "world/World.h"
#include "game/SurvivalSession.h"
#include "game/SurvivalRules.h"
#include "renderer/GameRenderer.h"

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
}

void EntityManager::clear() {
    m_entities.clear();
    m_deadEntityRenders.clear();
    m_spawnTimer = 0.0f;
    m_spawnSequence = 0;
    m_loadedChunks.clear();
    m_dirtyEntityChunks.clear();
    m_pendingEntitySaves.clear();
    m_lastStreamingRevision = std::numeric_limits<uint64_t>::max();
    m_explosionEvents.clear();
    m_poiChunks.clear();
    m_logicalVillages.clear();
    m_villageRefreshSeconds = 0.0f;
    m_modelRegistry.clearInstances();
}

std::vector<WorldMetadata::PersistedEntity> EntityManager::saveEntities() const {
    std::vector<WorldMetadata::PersistedEntity> saved;
    saved.reserve(m_entities.size());
    for (const auto& entity : m_entities) {
        saved.push_back({
            static_cast<uint8_t>(entity.type), entity.position, entity.velocity,
            entity.health, entity.ageSeconds, entity.item, entity.behaviorSeed,
            static_cast<uint8_t>((entity.inGround ? 1 : 0) |
                                 (entity.playerOwned ? 2 : 0)), entity.projectileDamage
            , entity.villager
        });
    }
    return saved;
}

void EntityManager::loadEntities(
    const std::vector<WorldMetadata::PersistedEntity>& saved) {
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
        m_entities.push_back(entity);
    }
}

void EntityManager::syncChunks() {
    if (!m_saveStore) return;
    const uint64_t revision = m_world.streamingRevision();
    if (revision == m_lastStreamingRevision) return;
    std::set<std::pair<int,int>> active;
    for (const Chunk* chunk : m_world.getActiveChunks())
        if (chunk->generated.load()) active.insert({chunk->cx,chunk->cz});
    for (const auto& key : m_loadedChunks) if (!active.count(key)) {
        std::vector<WorldMetadata::PersistedEntity> saved;
        for (const auto& entity : m_entities) if (entityChunk(entity.position)==key) {
            saved.push_back({static_cast<uint8_t>(entity.type),entity.position,entity.velocity,
                entity.health,entity.ageSeconds,entity.item,entity.behaviorSeed,
                static_cast<uint8_t>((entity.inGround?1:0)|(entity.playerOwned?2:0)),
                entity.projectileDamage, entity.villager});
        }
        m_saveStore->saveChunkEntities(key.first,key.second,saved);
        m_dirtyEntityChunks.erase(key);
        m_pendingEntitySaves.erase(key);
    }
    m_entities.erase(std::remove_if(m_entities.begin(),m_entities.end(),[&](const Entity& entity){
        const auto key=entityChunk(entity.position);
        return m_loadedChunks.count(key) && !active.count(key);
    }),m_entities.end());
    for (const auto& key : active) if (!m_loadedChunks.count(key)) {
        if (auto prefetched = m_world.takePrefetchedChunkEntities(
                key.first, key.second))
            loadEntities(std::move(*prefetched));
        else
            loadEntities(m_saveStore->loadChunkEntities(key.first,key.second));
        if (m_saveStore->loadChunkEntityPopulationVersion(
                key.first, key.second) < 1u) {
            for (const auto& request :
                 m_world.villageSpawnsForChunk(key.first, key.second)) {
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
            std::vector<WorldMetadata::PersistedEntity> populated;
            for (const Entity& entity : m_entities) {
                if (entityChunk(entity.position) != key) continue;
                populated.push_back({static_cast<uint8_t>(entity.type),
                    entity.position,entity.velocity,entity.health,
                    entity.ageSeconds,entity.item,entity.behaviorSeed,
                    static_cast<uint8_t>((entity.inGround?1:0) |
                                         (entity.playerOwned?2:0)),
                    entity.projectileDamage,entity.villager});
            }
            // Persist the merged entities first. If a crash interrupts this
            // sequence, the absent revision marker safely retries and merges
            // the same deterministic requests on the next load.
            m_saveStore->saveChunkEntities(
                key.first, key.second, populated);
            m_saveStore->saveChunkEntityPopulationVersion(
                key.first, key.second, 1u);
            m_dirtyEntityChunks.insert(key);
        }
    }
    m_loadedChunks=std::move(active);
    m_lastStreamingRevision = revision;
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
        std::vector<WorldMetadata::PersistedEntity> saved;
        for(const auto& entity:m_entities) if(entityChunk(entity.position)==key)
            saved.push_back({static_cast<uint8_t>(entity.type),entity.position,entity.velocity,
                entity.health,entity.ageSeconds,entity.item,entity.behaviorSeed,
                static_cast<uint8_t>((entity.inGround?1:0)|(entity.playerOwned?2:0)),
                entity.projectileDamage, entity.villager});
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
                               float damage, bool playerOwned) {
    Entity entity;
    entity.id = m_nextId++;
    entity.type = EntityType::Arrow;
    entity.position = position;
    entity.velocity = velocity;
    if (glm::length(velocity) > 0.0001f)
        entity.facing = glm::normalize(velocity);
    entity.projectileDamage = damage;
    entity.playerOwned = playerOwned;
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
    switch (type) {
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
        case EntityType::Arrow: return false;
        case EntityType::PrimedTnt: return false;
        case EntityType::Item: return false;
    }
    if (collides(entity, position)) return false;
    m_entities.push_back(entity);
    m_dirtyEntityChunks.insert(entityChunk(position));
    return true;
}

bool EntityManager::hostile(EntityType type) {
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
        const glm::vec3 size = renderSize(entity.type);
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
        entity->villager.hasBed && entity->villager.hasWorkstation;
}

bool EntityManager::villagerUsable(
    uint64_t entityId, const glm::dvec3& origin,
    const glm::vec3& direction, float reach) const {
    const Entity* entity = entityById(entityId);
    return entity && entity->type == EntityType::Villager &&
        entity->health > 0.0f &&
        useRay(origin, direction, reach) == entityId;
}

bool EntityManager::poiAccessible(const glm::ivec3& position) const {
    static constexpr glm::ivec3 offsets[] = {
        {1,0,0},{-1,0,0},{0,0,1},{0,0,-1}
    };
    for (const glm::ivec3& offset : offsets) {
        const glm::ivec3 feet = position + offset;
        if (m_world.getBlock(feet.x, feet.y, feet.z) == BlockId::AIR &&
            m_world.getBlock(feet.x, feet.y + 1, feet.z) == BlockId::AIR)
            return true;
    }
    return false;
}

bool EntityManager::groundPathReachable(
    const glm::dvec3& origin, const glm::ivec3& poi) const {
    const glm::ivec3 originBlock(glm::floor(origin));
    auto walkable = [&](const glm::ivec3& position) {
        if (!Config::isValidWorldY(position.y) ||
            !Config::isValidWorldY(position.y + 1)) return false;
        return blockCollisionBoxes(m_world.getBlock(
                   position.x, position.y, position.z)).count == 0 &&
               blockCollisionBoxes(m_world.getBlock(
                   position.x, position.y + 1, position.z)).count == 0 &&
               blockCollisionBoxes(m_world.getBlock(
                   position.x, position.y - 1, position.z)).count != 0;
    };
    glm::ivec3 start = originBlock;
    if (!walkable(start)) {
        bool found = false;
        for (int dy = -2; dy <= 2 && !found; ++dy) {
            const glm::ivec3 candidate = originBlock + glm::ivec3(0,dy,0);
            if (walkable(candidate)) { start = candidate; found = true; }
        }
        if (!found) return false;
    }
    std::queue<glm::ivec3> open;
    std::set<std::tuple<int,int,int>> visited;
    open.push(start);
    visited.emplace(start.x,start.y,start.z);
    constexpr size_t budget = 4096;
    while (!open.empty() && visited.size() <= budget) {
        const glm::ivec3 current = open.front();
        open.pop();
        if (std::abs(current.x - poi.x) + std::abs(current.z - poi.z) == 1 &&
            std::abs(current.y - poi.y) <= 1)
            return true;
        static constexpr glm::ivec2 directions[] = {
            {1,0},{-1,0},{0,1},{0,-1}
        };
        for (const glm::ivec2& direction : directions) {
            for (int dy : {0, 1, -1}) {
                const glm::ivec3 next(current.x + direction.x,
                                      current.y + dy,
                                      current.z + direction.y);
                const glm::dvec3 fromOrigin = glm::dvec3(next) - origin;
                if (glm::dot(fromOrigin, fromOrigin) > 48.0 * 48.0 ||
                    !walkable(next)) continue;
                if (visited.emplace(next.x,next.y,next.z).second) {
                    open.push(next);
                    break;
                }
            }
        }
    }
    return false;
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
            !entity.villager.hasBed || !entity.villager.hasWorkstation) continue;
        const glm::ivec3 bed(floorSubchunk(entity.villager.claimedBed.x),
                            floorSubchunk(entity.villager.claimedBed.y),
                            floorSubchunk(entity.villager.claimedBed.z));
        const glm::ivec3 work(floorSubchunk(entity.villager.claimedWorkstation.x),
                             floorSubchunk(entity.villager.claimedWorkstation.y),
                             floorSubchunk(entity.villager.claimedWorkstation.z));
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

void EntityManager::refreshVillageClaims() {
    std::set<std::pair<int,int>> active;
    for (const Chunk* chunk : m_world.getActiveChunks()) {
        if (!chunk->generated.load()) continue;
        const auto key = std::make_pair(chunk->cx, chunk->cz);
        active.insert(key);
        PoiChunk& indexed = m_poiChunks[key];
        if (indexed.revision == chunk->dataRevision()) continue;
        indexed = {};
        indexed.revision = chunk->dataRevision();
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                const int top = chunk->getColumnMaxY(x, z);
                for (int y = Config::WORLD_MIN_Y; y <= top; ++y) {
                    const BlockId block = chunk->getBlock(x, y, z);
                    const glm::ivec3 worldPosition(
                        chunk->worldX() + x, y, chunk->worldZ() + z);
                    BedPart part = BedPart::Foot;
                    BedDirection direction = BedDirection::North;
                    if (decodeBed(block, part, direction) && part == BedPart::Foot)
                        indexed.beds.push_back(worldPosition);
                    else if (isVillagerWorkstation(block))
                        indexed.workstations.push_back(worldPosition);
                }
            }
        }
    }
    for (auto it = m_poiChunks.begin(); it != m_poiChunks.end();) {
        if (active.count(it->first) == 0) it = m_poiChunks.erase(it);
        else ++it;
    }
    std::set<std::tuple<int,int,int>> usedBeds;
    std::set<std::tuple<int,int,int>> usedWorkstations;
    for (Entity& entity : m_entities) {
        if (entity.type != EntityType::Villager || entity.health <= 0.0f) continue;
        VillagerData& data = entity.villager;
        if (data.hasBed) {
            const auto valid = m_world.validBedFoot(data.claimedBed);
            if (!valid || *valid != data.claimedBed || !poiAccessible(data.claimedBed))
                data.hasBed = false;
            else if (!usedBeds.emplace(data.claimedBed.x,data.claimedBed.y,
                                       data.claimedBed.z).second)
                data.hasBed = false;
        }
        if (data.hasWorkstation) {
            const BlockId block = m_world.getBlock(
                data.claimedWorkstation.x, data.claimedWorkstation.y,
                data.claimedWorkstation.z);
            const VillagerProfession profession = professionForWorkstation(block);
            if (profession == VillagerProfession::Unemployed ||
                (data.professionLocked && profession != data.profession) ||
                !poiAccessible(data.claimedWorkstation)) {
                data.hasWorkstation = false;
                if (!data.professionLocked) data.profession = VillagerProfession::Unemployed;
            } else {
                if (!data.professionLocked) data.profession = profession;
                if (!usedWorkstations.emplace(data.claimedWorkstation.x,
                                               data.claimedWorkstation.y,
                                               data.claimedWorkstation.z).second) {
                    data.hasWorkstation = false;
                    if (!data.professionLocked)
                        data.profession = VillagerProfession::Unemployed;
                }
            }
        }
    }
    auto nearest = [&](const Entity& entity, bool bed,
                       VillagerProfession required) -> std::optional<glm::ivec3> {
        double best = 48.0 * 48.0 + 1.0;
        std::optional<glm::ivec3> result;
        for (const auto& [key, indexed] : m_poiChunks) {
            (void)key;
            const auto& positions = bed ? indexed.beds : indexed.workstations;
            for (const glm::ivec3& position : positions) {
                const auto tuple = std::make_tuple(position.x,position.y,position.z);
                if ((bed ? usedBeds : usedWorkstations).count(tuple) != 0) continue;
                if (!bed && required != VillagerProfession::Unemployed &&
                    professionForWorkstation(m_world.getBlock(
                        position.x,position.y,position.z)) != required) continue;
                const glm::dvec3 delta = entity.position -
                    (glm::dvec3(position) + glm::dvec3(.5));
                const double distance = glm::dot(delta, delta);
                if (distance > 48.0 * 48.0 || !poiAccessible(position) ||
                    !groundPathReachable(entity.position, position)) continue;
                if (!result || distance < best ||
                    (distance == best && std::tie(position.x,position.y,position.z) <
                                         std::tie(result->x,result->y,result->z))) {
                    best = distance;
                    result = position;
                }
            }
        }
        return result;
    };
    for (Entity& entity : m_entities) {
        if (entity.type != EntityType::Villager || entity.health <= 0.0f) continue;
        VillagerData& data = entity.villager;
        if (!data.hasBed) {
            if (const auto bed = nearest(entity, true, VillagerProfession::Unemployed)) {
                data.claimedBed = *bed;
                data.hasBed = true;
                usedBeds.emplace(bed->x,bed->y,bed->z);
            }
        }
        if (!data.hasWorkstation) {
            const VillagerProfession required = data.professionLocked
                ? data.profession : VillagerProfession::Unemployed;
            if (const auto work = nearest(entity, false, required)) {
                data.claimedWorkstation = *work;
                data.hasWorkstation = true;
                data.profession = professionForWorkstation(m_world.getBlock(
                    work->x,work->y,work->z));
                usedWorkstations.emplace(work->x,work->y,work->z);
            }
        }
    }
    rebuildLogicalVillages();
}

void EntityManager::spawnAroundPlayer(
    const glm::dvec3& playerPosition, bool spawnHostile) {
    const size_t mobCount = static_cast<size_t>(std::count_if(
        m_entities.begin(), m_entities.end(),
        [spawnHostile](const Entity& entity) {
            return entity.type != EntityType::Item && entity.type != EntityType::Arrow &&
                   hostile(entity.type) == spawnHostile;
        }));
    if (mobCount >= (spawnHostile ? 24u : 16u)) return;
    const uint32_t random = hash32(++m_spawnSequence);
    const float angle = static_cast<float>(random % 6283) * 0.001f;
    const float distance = 18.0f + static_cast<float>((random >> 16) % 14);
    const int x = static_cast<int>(std::floor(playerPosition.x + std::cos(angle) * distance));
    const int z = static_cast<int>(std::floor(playerPosition.z + std::sin(angle) * distance));
    int surface = -1;
    for (int y = Config::WORLD_MAX_Y - 1; y >= Config::WORLD_MIN_Y; --y) {
        if (isSolid(m_world.getBlock(x, y, z))) {
            surface = y + 1;
            break;
        }
    }
    if (!Config::isValidWorldY(surface) || surface + 1 >= Config::WORLD_MAX_Y ||
        m_world.getBlock(x, surface, z) != BlockId::AIR ||
        m_world.getBlock(x, surface + 1, z) != BlockId::AIR) return;
    if (spawnHostile && !hostileSpawnLightValid(m_world.getBlockLight(x, surface, z))) return;
    const EntityType passiveTypes[] = {
        EntityType::Cow, EntityType::Pig, EntityType::Sheep, EntityType::Chicken
    };
    const EntityType hostileTypes[] = {
        EntityType::Zombie, EntityType::Skeleton, EntityType::Spider,
        EntityType::Blastling
    };
    EntityType type = spawnHostile
        ? hostileTypes[random % 4] : passiveTypes[random % 4];
    if (type == EntityType::Zombie &&
        naturalZombieBecomesVillager(hash32(random ^ 0x5a17u)))
        type = EntityType::ZombieVillager;
    spawnMob(type, glm::dvec3(x + 0.5, static_cast<double>(surface), z + 0.5));
}

void EntityManager::moveWithTerrain(
    Entity& entity, const glm::vec3& horizontal, float dt) {
    const glm::dvec3 start = entity.position;
    const double distance = glm::length(horizontal) * dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / 0.2)));
    const glm::dvec3 delta = glm::dvec3(horizontal) *
        (static_cast<double>(dt) / steps);
    bool movedThisFrame = false;
    for (int i=0;i<steps;++i) {
        bool moved = false;
        glm::dvec3 candidate = entity.position;
        candidate.x += delta.x;
        if (!collides(entity,candidate)) { entity.position.x=candidate.x; moved=true; }
        candidate=entity.position; candidate.z+=delta.z;
        if (!collides(entity,candidate)) { entity.position.z=candidate.z; moved=true; }
        if (!moved) {
            candidate=entity.position+glm::dvec3(delta.x,1.0,delta.z);
            if (!collides(entity,candidate)) { entity.position=candidate;moved=true; }
        }
        candidate=entity.position; candidate.y-=0.25;
        if (!collides(entity,candidate)) entity.position.y-=0.25;
        movedThisFrame = movedThisFrame || moved;
    }
    entity.stuckSeconds = movedThisFrame ? 0.0f : entity.stuckSeconds + dt;
    if (dt > 0.0f) {
        const glm::vec3 displacement(
            static_cast<float>(entity.position.x - start.x), 0.0f,
            static_cast<float>(entity.position.z - start.z));
        entity.locomotionVelocity = autonomousHorizontalVelocity(
            start, entity.position, dt);
        if (glm::length(displacement) > 0.00001f)
            entity.facing = glm::normalize(displacement);
    }
}

void EntityManager::integrateVelocity(Entity& entity, float dt) {
    entity.velocity.y -= 20.0f * dt;
    const glm::dvec3 total = glm::dvec3(entity.velocity) * static_cast<double>(dt);
    const int steps = sweptCollisionSteps(glm::length(total), 0.15);
    const glm::dvec3 step = total / static_cast<double>(steps);
    for (int i = 0; i < steps; ++i) {
        glm::dvec3 candidate = entity.position;
        candidate.x += step.x;
        if (!collides(entity, candidate)) entity.position.x = candidate.x;
        else entity.velocity.x = 0.0f;
        candidate = entity.position;
        candidate.y += step.y;
        if (!collides(entity, candidate)) entity.position.y = candidate.y;
        else entity.velocity.y = 0.0f;
        candidate = entity.position;
        candidate.z += step.z;
        if (!collides(entity, candidate)) entity.position.z = candidate.z;
        else entity.velocity.z = 0.0f;
    }
    const float drag = std::pow(0.12f, dt);
    entity.velocity.x *= drag;
    entity.velocity.z *= drag;
}

bool EntityManager::exposedToSky(const Entity& entity) const {
    const int worldX = static_cast<int>(std::floor(entity.position.x));
    const int worldZ = static_cast<int>(std::floor(entity.position.z));
    const int headTop = static_cast<int>(std::floor(
        entity.position.y + renderSize(entity.type).y - 1e-6));
    const int cx = World::worldToChunkX(static_cast<double>(worldX));
    const int cz = World::worldToChunkZ(static_cast<double>(worldZ));
    const int lx = worldX - cx * Config::CHUNK_SIZE_X;
    const int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    for (const Chunk* chunk : m_world.getActiveChunks()) {
        if (chunk->cx == cx && chunk->cz == cz && chunk->generated.load())
            return chunk->getColumnMaxY(lx, lz) <= headTop;
    }
    return false;
}

bool EntityManager::touchesWater(const Entity& entity) const {
    const glm::vec3 size = renderSize(entity.type);
    const int x = static_cast<int>(std::floor(entity.position.x));
    const int z = static_cast<int>(std::floor(entity.position.z));
    const int feet = static_cast<int>(std::floor(entity.position.y + 0.05));
    const int head = static_cast<int>(std::floor(entity.position.y + size.y - 0.05));
    return isWater(m_world.getBlock(x, feet, z)) ||
           isWater(m_world.getBlock(x, head, z));
}

float EntityManager::damageEntity(Entity& entity, float damage,
                                  const glm::vec3& knockback,
                                  bool playerAttack) {
    if (damage <= 0.0f || entity.health <= 0.0f) return 0.0f;
    const float accepted = PlayerPhysics::damageAfterImmunity(
        entity.hurtImmunity, damage, Config::PLAYER_HURT_IMMUNITY_SECONDS);
    if (accepted <= 0.0f) return 0.0f;
    entity.health -= accepted;
    entity.hurtFlashSeconds = 0.2f;
    if ((entity.type >= EntityType::Cow && entity.type <= EntityType::Blastling) ||
        entity.type == EntityType::Villager ||
        entity.type == EntityType::ZombieVillager)
        m_modelRegistry.playAction(entity.type, entity.id, "hurt");
    entity.velocity += knockback;
    if (playerAttack && entity.type == EntityType::Spider)
        entity.spiderProvoked = true;
    return accepted;
}

bool EntityManager::collides(const Entity& entity, const glm::dvec3& position) const {
    const glm::vec3 size=renderSize(entity.type);
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
    for (const auto& entity : m_entities)
        m_dirtyEntityChunks.insert(entityChunk(entity.position));
    for (auto& dead : m_deadEntityRenders) {
        dead.elapsed = advanceDeathPresentation(dead.elapsed, dt);
        m_modelRegistry.advance(dead.type, dead.id, dt);
    }
    m_deadEntityRenders.erase(std::remove_if(
        m_deadEntityRenders.begin(), m_deadEntityRenders.end(),
        [](const DeadEntityRender& dead) {
            return !deathPresentationVisible(dead.elapsed);
        }), m_deadEntityRenders.end());

    for (auto& entity : m_entities)
        PlayerPhysics::tickHurtImmunity(entity.hurtImmunity, dt);
    struct PendingArrow { glm::dvec3 position; glm::vec3 velocity; float damage; };
    std::vector<PendingArrow> pendingArrows;
    struct PendingExplosion {
        glm::dvec3 position;
        uint32_t seed;
        float power;
    };
    std::vector<PendingExplosion> pendingExplosions;
    if (m_naturalSpawningEnabled) {
        m_spawnTimer += dt;
        if (m_spawnTimer >= 4.0f) {
            spawnAroundPlayer(
                player.getPosition(), (!isDay || thunderstorm) && !peaceful);
            m_spawnTimer = 0.0f;
        }
    } else {
        m_spawnTimer = 0.0f;
    }
    m_villageRefreshSeconds += dt;
    if (m_villageRefreshSeconds >= 1.0f) {
        refreshVillageClaims();
        m_villageRefreshSeconds = 0.0f;
    }

    for (auto& entity : m_entities) {
        if (peaceful && hostile(entity.type)) { entity.health=0.0f; continue; }
        entity.ageSeconds += dt;
        entity.actionCooldown = std::max(0.0f, entity.actionCooldown - dt);
        entity.hurtFlashSeconds = std::max(0.0f, entity.hurtFlashSeconds - dt);
        if (entity.type == EntityType::Arrow) {
            updateArrow(entity, player, dt);
            continue;
        }
        if (entity.type == EntityType::PrimedTnt) {
            integrateVelocity(entity, dt);
            if (entity.ageSeconds >= std::max(0.05f, entity.projectileDamage))
                entity.health = 0.0f;
            continue;
        }
        if (entity.type == EntityType::Item) {
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
            if (playerCanPickup && entity.actionCooldown<=0.0f &&
                glm::distance(entity.position, player.getPosition()) < 1.6) {
                if (pickupItemStack(player.inventory(), entity.item)) entity.health = 0.0f;
            }
            continue;
        }

        const glm::vec3 previousLocomotionVelocity = entity.locomotionVelocity;
        entity.locomotionVelocity = glm::vec3(0.0f);
        integrateVelocity(entity, dt);

        {
            const bool inWater = touchesWater(entity);
            const int x = static_cast<int>(std::floor(entity.position.x));
            const int y = static_cast<int>(std::floor(entity.position.y));
            const int z = static_cast<int>(std::floor(entity.position.z));
            const bool exposed = exposedToSky(entity);
            const bool wetByRain = raining && exposed &&
                m_world.precipitationAt(x, y, z) == PrecipitationType::Rain;
            const bool fireContact = m_world.getBlock(x, y, z) == BlockId::FIRE ||
                m_world.getBlock(x, static_cast<int>(std::floor(
                    entity.position.y + renderSize(entity.type).y * 0.5)), z) ==
                    BlockId::FIRE;
            const bool undead = entity.type == EntityType::Zombie ||
                                entity.type == EntityType::Skeleton ||
                                entity.type == EntityType::ZombieVillager;
            const bool ignited = fireContact ||
                (undead && isDay && !thunderstorm && exposed);
            const bool extinguished = inWater || wetByRain;
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

        if (entity.type == EntityType::Villager) {
            VillagerData& villager = entity.villager;
            const uint32_t tickInDay = static_cast<uint32_t>(worldTick % 24000u);
            const uint32_t day = static_cast<uint32_t>(worldTick / 24000u);
            if (villager.lastRestockDay != day) {
                villager.lastRestockDay = day;
                villager.restocksToday = 0;
            }
            const bool firstWorkWindow = tickInDay >= 2000u && tickInDay < 4000u;
            const bool secondWorkWindow = tickInDay >= 9000u && tickInDay < 11000u;
            const bool working = firstWorkWindow || secondWorkWindow;
            glm::dvec3 destination = entity.position;
            bool hasDestination = false;
            if (!isDay && villager.hasBed) {
                destination = glm::dvec3(villager.claimedBed) +
                    glm::dvec3(.5, .6, .5);
                hasDestination = true;
            } else if (working && villager.hasWorkstation) {
                destination = glm::dvec3(villager.claimedWorkstation) +
                    glm::dvec3(.5, 0.0, .5);
                hasDestination = true;
            } else if (villager.hasBed && villager.hasWorkstation) {
                const glm::dvec3 center =
                    (glm::dvec3(villager.claimedBed) +
                     glm::dvec3(villager.claimedWorkstation)) * .5 +
                    glm::dvec3(.5, 0.0, .5);
                if (glm::distance(entity.position, center) > 12.0) {
                    destination = center;
                    hasDestination = true;
                }
            }
            const double destinationDistance =
                hasDestination ? glm::distance(entity.position, destination) : 0.0;
            entity.sleeping = !isDay && villager.hasBed &&
                destinationDistance < 1.25;
            if (!entity.sleeping) {
                if (hasDestination && destinationDistance > .65) {
                    glm::vec3 direction(destination - entity.position);
                    direction.y = 0.0f;
                    if (glm::length(direction) > .001f)
                        moveWithTerrain(entity, glm::normalize(direction) * .75f, dt);
                } else if (!hasDestination) {
                    const float angle = static_cast<float>(hash32(
                        entity.behaviorSeed + static_cast<uint32_t>(
                            entity.ageSeconds / 3.0f)) % 6283) * .001f;
                    moveWithTerrain(entity, {std::cos(angle) * .45f, 0.0f,
                                             std::sin(angle) * .45f}, dt);
                }
            }
            if (working && villager.hasWorkstation && destinationDistance < 1.6 &&
                ((firstWorkWindow && villager.restocksToday < 1) ||
                 (secondWorkWindow && villager.restocksToday < 2)))
                restockVillager(villager, day, true);
            m_modelRegistry.advance(entity.type, entity.id, dt);
            m_modelRegistry.setLocomotion(entity.type, entity.id,
                std::hypot(entity.locomotionVelocity.x,
                           entity.locomotionVelocity.z));
            continue;
        }

        if (entity.type == EntityType::Zombie ||
            entity.type == EntityType::ZombieVillager) {
            Entity* villagerTarget = nullptr;
            double nearest = 18.0;
            for (Entity& candidate : m_entities) {
                if (candidate.type != EntityType::Villager ||
                    candidate.health <= 0.0f) continue;
                const double distance = glm::distance(
                    candidate.position, entity.position);
                if (distance < nearest) {
                    nearest = distance;
                    villagerTarget = &candidate;
                }
            }
            if (villagerTarget) {
                const glm::dvec3 targetCenter = villagerTarget->position +
                    glm::dvec3(0.0, .9, 0.0);
                const glm::dvec3 origin = entity.position +
                    glm::dvec3(0.0, 1.2, 0.0);
                const glm::dvec3 delta = targetCenter - origin;
                const float targetDistance = static_cast<float>(glm::length(delta));
                const glm::vec3 direction = targetDistance > .001f
                    ? glm::vec3(delta / static_cast<double>(targetDistance))
                    : glm::vec3(0.0f);
                const bool clear = targetDistance <= .001f ||
                    !m_world.raycast(origin, direction, targetDistance).has_value();
                if (targetDistance > 1.45f) {
                    glm::vec3 horizontal(direction.x, 0.0f, direction.z);
                    if (glm::length(horizontal) > .001f)
                        moveWithTerrain(entity, glm::normalize(horizontal) * 2.0f, dt);
                } else if (clear && entity.actionCooldown <= 0.0f) {
                    entity.actionCooldown = 1.0f;
                    glm::vec3 knockback(direction.x, 0.0f, direction.z);
                    if (glm::length(knockback) > .001f)
                        knockback = glm::normalize(knockback) * 3.0f;
                    knockback.y = 1.5f;
                    damageEntity(*villagerTarget, 3.0f, knockback, false);
                    if (villagerTarget->health <= 0.0f) {
                        const uint32_t roll = hash32(entity.behaviorSeed ^
                            villagerTarget->behaviorSeed ^
                            static_cast<uint32_t>(worldTick));
                        const bool convert = villagerInfectionConverts(
                            player.difficulty(), roll);
                        if (convert) {
                            villagerTarget->type = EntityType::ZombieVillager;
                            villagerTarget->health = 20.0f;
                            villagerTarget->villager.hasBed = false;
                            villagerTarget->villager.hasWorkstation = false;
                            villagerTarget->sleeping = false;
                        }
                    }
                }
                m_modelRegistry.advance(entity.type, entity.id, dt);
                m_modelRegistry.setLocomotion(entity.type, entity.id,
                    std::hypot(entity.locomotionVelocity.x,
                               entity.locomotionVelocity.z));
                continue;
            }
        }

        const glm::vec3 delta = glm::vec3(player.getPosition() - entity.position);
        glm::vec3 horizontal(delta.x, 0.0f, delta.z);
        const float distance = glm::length(horizontal);
        const glm::dvec3 attackOrigin = entity.position + glm::dvec3(0,1.2,0);
        const glm::dvec3 meleeDelta =
            player.getPosition() + glm::dvec3(0.0, 0.9, 0.0) - attackOrigin;
        const float meleeDistance = static_cast<float>(glm::length(meleeDelta));
        const glm::vec3 meleeDirection = meleeDistance > 0.001f
            ? glm::vec3(meleeDelta / static_cast<double>(meleeDistance))
            : glm::vec3(0.0f);
        const bool meleeClearSight = meleeDistance <= 0.001f ||
            !m_world.raycast(
                attackOrigin, meleeDirection, meleeDistance).has_value();
        const glm::dvec3 rangedOrigin =
            entity.position + glm::dvec3(0.0, 1.45, 0.0);
        const glm::dvec3 rangedDelta = player.getEyePosition() - rangedOrigin;
        const float rangedDistance = static_cast<float>(glm::length(rangedDelta));
        const glm::vec3 rangedDirection = rangedDistance > 0.001f
            ? glm::vec3(rangedDelta / static_cast<double>(rangedDistance))
            : glm::vec3(0.0f);
        const bool rangedClearSight = rangedDistance <= 0.001f ||
            !m_world.raycast(
                rangedOrigin, rangedDirection, rangedDistance).has_value();
        const auto animationEvents = m_modelRegistry.advance(
            entity.type, entity.id, dt);
        for (const auto& event : animationEvents) {
            if (!entity.attackPending) continue;
            if (event.name == "melee") {
                if (attackImpactValid(meleeDistance, 1.5f, meleeClearSight)) {
                    DamageSourceInfo source;
                    source.amount = 3.0f;
                    source.cause = DamageCause::Melee;
                    source.shieldBlockable = true;
                    source.hasOrigin = true;
                    source.origin = attackOrigin;
                    glm::vec3 impulse(meleeDirection.x, 0.0f, meleeDirection.z);
                    if (glm::length(impulse) > 0.001f)
                        impulse = glm::normalize(impulse) * 4.0f;
                    impulse.y = 2.0f;
                    source.impulse = impulse;
                    player.takeDamage(source);
                }
                entity.attackPending = false;
            } else if (event.name == "shoot") {
                if (attackImpactValid(
                        rangedDistance, 14.0f, rangedClearSight)) {
                    const glm::vec3 inheritedVelocity =
                        entity.velocity + previousLocomotionVelocity;
                    const auto launchVelocity = lowArcBallisticVelocity(
                        rangedOrigin, player.getEyePosition(),
                        bowLaunchSpeed(0.9f), inheritedVelocity);
                    if (launchVelocity) {
                        const glm::vec3 launchDirection =
                            glm::normalize(*launchVelocity);
                        pendingArrows.push_back({rangedOrigin +
                            glm::dvec3(launchDirection) * 0.75,
                            *launchVelocity, 2.0f});
                    }
                }
                entity.attackPending = false;
            } else if (event.name == "explode") {
                if (attackImpactValid(
                        meleeDistance, 1.5f, meleeClearSight)) {
                    pendingExplosions.push_back(
                        {entity.position, entity.behaviorSeed, 2.5f});
                    entity.health = 0.0f;
                }
                entity.attackPending = false;
            }
        }
        if (entity.attackPending &&
            !m_modelRegistry.playing(entity.type, entity.id, "attack"))
            entity.attackPending = false;
        if (entity.health <= 0.0f) continue;
        if (entity.type == EntityType::Spider && distance >= 18.0f)
            entity.spiderProvoked = false;
        if (hostile(entity.type) && shouldHostileDespawn(distance,entity.ageSeconds,
            hash32(entity.behaviorSeed + static_cast<uint32_t>(entity.ageSeconds)))) {
            entity.health = 0.0f;
            continue;
        }
        const bool behaviorTargetsPlayer = entity.type == EntityType::Spider
            ? spiderTargetsPlayer(isDay && !thunderstorm,
                                  entity.spiderProvoked, distance)
            : hostile(entity.type) && distance < 18.0f;
        const bool targetsPlayer = mobTargetsPlayer(
            playerTargetable, behaviorTargetsPlayer);
        if (entity.attackPending) {
            if (distance > 0.01f) entity.facing = glm::normalize(horizontal);
        } else if (targetsPlayer) {
            if (distance > 0.01f) horizontal /= distance;
            const float speed = entity.type == EntityType::Spider ? 3.0f : 2.0f;
            if (entity.type == EntityType::Skeleton) {
                if (distance > 7.0f && distance > 0.01f)
                    moveWithTerrain(entity, horizontal * speed, dt);
                if (attackImpactValid(
                        rangedDistance, 14.0f, rangedClearSight) &&
                    entity.actionCooldown <= 0.0f) {
                    entity.actionCooldown = 2.0f;
                    entity.attackPending = m_modelRegistry.playAction(
                        entity.type, entity.id, "attack");
                }
            } else if (distance > 0.01f) {
                moveWithTerrain(entity, horizontal * speed, dt);
            }
            if (entity.type != EntityType::Skeleton &&
                attackImpactValid(meleeDistance, 1.5f, meleeClearSight) &&
                entity.actionCooldown <= 0.0f) {
                entity.actionCooldown = 1.0f;
                entity.attackPending = m_modelRegistry.playAction(
                    entity.type, entity.id, "attack");
            }
        } else {
            const float angle = static_cast<float>(
                hash32(entity.behaviorSeed + static_cast<uint32_t>(entity.ageSeconds / 3.0f))
                % 6283) * 0.001f;
            const float offset=entity.stuckSeconds>0.6f ? 1.5707963f : 0.0f;
            moveWithTerrain(entity, {std::cos(angle+offset) * 0.65f, 0.0f,
                                     std::sin(angle+offset) * 0.65f}, dt);
        }
        m_modelRegistry.setLocomotion(
            entity.type, entity.id,
            std::hypot(entity.locomotionVelocity.x, entity.locomotionVelocity.z));
    }

    for (const auto& arrow : pendingArrows)
        spawnArrow(arrow.position, arrow.velocity, arrow.damage, false);

    for (const auto& entity : m_entities)
        if (entity.type == EntityType::PrimedTnt && entity.health <= 0.0f)
            pendingExplosions.push_back(
                {entity.position, entity.behaviorSeed, 4.0f});
    for (const auto& explosion : pendingExplosions)
        explode(player, explosion.position, explosion.power, explosion.seed);

    std::vector<Entity> deadMobs;
    for (const auto& entity : m_entities)
        if (entity.health <= 0.0f && entity.type != EntityType::Item &&
            entity.type != EntityType::PrimedTnt)
            deadMobs.push_back(entity);
    for (const auto& entity : deadMobs) {
        if (entity.type != EntityType::Arrow) {
            m_modelRegistry.playAction(entity.type, entity.id, "death");
            m_deadEntityRenders.push_back({entity.id, entity.type,
                entity.position, entity.velocity, entity.facing,
                entity.behaviorSeed, 0.0f});
        }
    }
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
        [](const Entity& entity) {
            return entity.health <= 0.0f ||
                   (entity.type == EntityType::Item && entity.ageSeconds >= 300.0f) ||
                   (entity.type == EntityType::Arrow && entity.ageSeconds >= 60.0f);
        }), m_entities.end());
    for (const auto& entity : deadMobs) dropMobLoot(entity);
    for (const auto& entity : m_entities)
        m_dirtyEntityChunks.insert(entityChunk(entity.position));
}

void EntityManager::strikeLightning(Player& player, const glm::ivec3& position) {
    const glm::dvec3 center = glm::dvec3(position) + glm::dvec3(0.5, 0.0, 0.5);
    for (auto& entity : m_entities) {
        if (entity.type == EntityType::Item || entity.type == EntityType::Arrow ||
            entity.health <= 0.0f) continue;
        if (glm::distance(entity.position, center) <= 3.0) {
            damageEntity(entity, 5.0f, glm::vec3(0.0f, 0.25f, 0.0f), false);
            entity.burningSeconds = std::max(entity.burningSeconds, 8.0f);
        }
    }
    if (player.isSurvival() && glm::distance(player.getPosition(), center) <= 3.0) {
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
        case EntityType::Spider: loot = {{ItemId::STRING, 1, 0}}; break;
        case EntityType::Blastling: loot = {{ItemId::GUNPOWDER, 1, 0}}; break;
        case EntityType::Villager: break;
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
    return ((entity.type >= EntityType::Cow && entity.type <= EntityType::Blastling) ||
            entity.type == EntityType::Villager ||
            entity.type == EntityType::ZombieVillager) &&
        entity.health > 0.0f;
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
    const MeleeAttackRequest& attack) {
    MeleeAttackResult result;
    Entity* best = nullptr;
    float bestAlong = attack.reach + 1.0f;
    for (auto& entity : m_entities) {
        if (!meleeTarget(entity)) continue;
        float along = 0.0f;
        if (!rayEntityAabb(origin, direction, entity, renderSize(entity.type),
                           attack.reach, along) || along >= bestAlong)
            continue;
        if (along > 0.001f &&
            m_world.raycast(origin, direction, along).has_value())
            continue;
        best = &entity;
        bestAlong = along;
    }
    if (!best) return result;

    result.foundTarget = true;
    result.primaryPosition = best->position +
        glm::dvec3(0.0, renderSize(best->type).y * 0.5, 0.0);
    glm::vec3 horizontal(direction.x, 0.0f, direction.z);
    if (glm::length(horizontal) > 0.001f)
        horizontal = glm::normalize(horizontal) *
            (attack.sprintKnockback ? 6.0f : 4.0f);
    horizontal.y = 2.0f;
    result.primaryDamage = damageEntity(
        *best, attack.damage, horizontal, true);
    result.primaryDamaged = result.primaryDamage > 0.0f;

    if (!attack.sweeping || !result.primaryDamaged) return result;
    const glm::vec3 bestSize = renderSize(best->type);
    const glm::dvec3 sweepMin(
        best->position.x - bestSize.x * 0.5 - 1.0,
        best->position.y - 1.0,
        best->position.z - bestSize.z * 0.5 - 1.0);
    const glm::dvec3 sweepMax(
        best->position.x + bestSize.x * 0.5 + 1.0,
        best->position.y + bestSize.y + 1.0,
        best->position.z + bestSize.z * 0.5 + 1.0);
    for (auto& entity : m_entities) {
        if (&entity == best || !meleeTarget(entity)) continue;
        const glm::dvec3 center = entity.position +
            glm::dvec3(0.0, renderSize(entity.type).y * 0.5, 0.0);
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
        glm::vec3 sweepKnockback(entity.position.x - best->position.x, 0.0f,
                                 entity.position.z - best->position.z);
        if (glm::length(sweepKnockback) > 0.001f)
            sweepKnockback = glm::normalize(sweepKnockback) * 4.0f;
        sweepKnockback.y = 2.0f;
        if (damageEntity(entity, CombatRules::SWEEP_DAMAGE,
                         sweepKnockback, true) > 0.0f)
            result.sweptPositions.push_back(center);
    }
    return result;
}

bool EntityManager::hasAttackTarget(
    const glm::dvec3& origin, const glm::vec3& direction, float reach) const {
    float bestAlong = reach + 1.0f;
    for (const auto& entity : m_entities) {
        if (!meleeTarget(entity)) continue;
        float along = 0.0f;
        if (!rayEntityAabb(origin, direction, entity, renderSize(entity.type),
                           reach, along) || along >= bestAlong)
            continue;
        if (along > 0.001f &&
            m_world.raycast(origin, direction, along).has_value())
            continue;
        return true;
    }
    return false;
}

void EntityManager::updateArrow(Entity& arrow, Player& player, float dt) {
    if (arrow.inGround) {
        if (arrow.playerOwned && arrow.ageSeconds > 0.5f &&
            glm::distance(arrow.position,player.getPosition())<1.6) {
            ItemStack stack{ItemId::ARROW,1,0};
            if (player.inventory().add(stack)==0) arrow.health=0.0f;
        }
        return;
    }
    const glm::dvec3 start=arrow.position;
    const glm::dvec3 delta=projectilePosition(
        glm::dvec3(0.0), arrow.velocity, static_cast<double>(dt));
    const int steps=sweptCollisionSteps(glm::length(delta));
    for(int step=1;step<=steps;++step) {
        const glm::dvec3 next=start+delta*(static_cast<double>(step)/steps);
        const glm::ivec3 block(glm::floor(next));
        const BlockId hitBlock = m_world.getBlock(block.x,block.y,block.z);
        if(pointInsideBlockCollision(hitBlock,
                glm::vec3(next - glm::dvec3(block)))) {
            arrow.position=next;arrow.velocity={0,0,0};arrow.inGround=true;return;
        }
        if (!arrow.playerOwned) {
            const glm::dvec3 playerMin=player.getPosition()+glm::dvec3(-.3,0,-.3);
            const glm::dvec3 playerMax=player.getPosition()+glm::dvec3(.3,1.8,.3);
            if(next.x>=playerMin.x&&next.x<=playerMax.x&&next.y>=playerMin.y&&
               next.y<=playerMax.y&&next.z>=playerMin.z&&next.z<=playerMax.z) {
                DamageSourceInfo source;
                source.amount = arrow.projectileDamage;
                source.cause = DamageCause::Projectile;
                source.shieldBlockable = true;
                source.hasOrigin = true;
                source.origin = arrow.position;
                glm::vec3 impulse(arrow.velocity.x, 0.0f, arrow.velocity.z);
                if (glm::length(impulse) > 0.001f)
                    impulse = glm::normalize(impulse) * 3.0f;
                impulse.y = 1.5f;
                source.impulse = impulse;
                const DamageOutcome outcome = player.takeDamage(source);
                if (outcome.blocked) {
                    arrow.position = next;
                    arrow.velocity *= -0.2f;
                    arrow.playerOwned = true;
                    return;
                }
                arrow.health=0;return;
            }
        }
        for(auto& target:m_entities) {
            if(&target==&arrow || target.type==EntityType::Item || target.type==EntityType::Arrow || target.health<=0) continue;
            const glm::vec3 size=renderSize(target.type);
            const glm::dvec3 min=target.position+glm::dvec3(-size.x*.5,0,-size.z*.5);
            const glm::dvec3 max=target.position+glm::dvec3(size.x*.5,size.y,size.z*.5);
            if(next.x>=min.x&&next.x<=max.x&&next.y>=min.y&&next.y<=max.y&&next.z>=min.z&&next.z<=max.z) {
                glm::vec3 knockback(arrow.velocity.x, 0.0f, arrow.velocity.z);
                if (glm::length(knockback) > 0.001f)
                    knockback = glm::normalize(knockback) * 3.0f;
                knockback.y = 1.5f;
                damageEntity(target, arrow.projectileDamage, knockback,
                             arrow.playerOwned);
                arrow.health=0;return;
            }
        }
        arrow.position=next;
    }
    arrow.velocity=projectileVelocityAfter(arrow.velocity,dt);
    if(glm::length(arrow.velocity)>0.0001f)
        arrow.facing=glm::normalize(arrow.velocity);
}

void EntityManager::explode(Player& player, const glm::dvec3& center,
                            float power, uint32_t eventSeed) {
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
            damageEntity(entity, damage, direction * impact * 3.5f, false);
        }
    }
    if (player.isSurvival()) {
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
    const int radius = static_cast<int>(std::ceil(power));
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
                } else if (spawnedDrops < 32 && random % 3 == 0) {
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

glm::vec3 EntityManager::renderColor(EntityType type) {
    switch (type) {
        case EntityType::Item: return {0.95f, 0.78f, 0.22f};
        case EntityType::Cow: return {0.34f, 0.20f, 0.12f};
        case EntityType::Pig: return {0.92f, 0.52f, 0.58f};
        case EntityType::Sheep: return {0.88f, 0.88f, 0.82f};
        case EntityType::Chicken: return {0.92f, 0.90f, 0.72f};
        case EntityType::Zombie: return {0.20f, 0.48f, 0.28f};
        case EntityType::Skeleton: return {0.72f, 0.72f, 0.68f};
        case EntityType::Spider: return {0.16f, 0.08f, 0.07f};
        case EntityType::Blastling: return {0.35f, 0.72f, 0.30f};
        case EntityType::Villager: return {0.56f,0.36f,0.22f};
        case EntityType::ZombieVillager: return {0.27f,0.48f,0.29f};
        case EntityType::Arrow: return {0.58f,0.42f,0.20f};
        case EntityType::PrimedTnt: return {0.86f,0.18f,0.12f};
    }
    return {1.0f, 0.0f, 1.0f};
}

glm::vec3 EntityManager::renderSize(EntityType type) {
    switch (type) {
        case EntityType::Item: return {0.25f, 0.25f, 0.25f};
        case EntityType::Arrow: return {0.08f,0.08f,0.75f};
        case EntityType::PrimedTnt: return {0.98f,0.98f,0.98f};
        case EntityType::Chicken: return {0.45f, 0.65f, 0.45f};
        case EntityType::Spider: return {1.2f, 0.55f, 1.2f};
        case EntityType::Villager:
        case EntityType::ZombieVillager: return {0.62f,1.80f,0.48f};
        case EntityType::Cow:
        case EntityType::Pig:
        case EntityType::Sheep: return {0.9f, 1.2f, 1.3f};
        default: return {0.65f, 1.75f, 0.65f};
    }
}

void EntityManager::render(
    IGameRenderer& renderer, const glm::mat4& viewProjection,
    const glm::dvec3& renderOrigin) const {
    m_modelRegistry.beginFrame();
    for (const auto& entity : m_entities) {
        int textureIndex = 8;
        switch (entity.type) {
            case EntityType::Cow: textureIndex = 0; break;
            case EntityType::Pig: textureIndex = 1; break;
            case EntityType::Sheep: textureIndex = 2; break;
            case EntityType::Chicken: textureIndex = 3; break;
            case EntityType::Zombie: textureIndex = 4; break;
            case EntityType::Skeleton: textureIndex = 5; break;
            case EntityType::Spider: textureIndex = 6; break;
            case EntityType::Blastling: textureIndex = 7; break;
            case EntityType::Villager: textureIndex = 9; break;
            case EntityType::ZombieVillager: textureIndex = 10; break;
            case EntityType::Item: textureIndex = 8; break;
            case EntityType::Arrow: textureIndex = 8; break;
            case EntityType::PrimedTnt: textureIndex = 8; break;
        }
        glm::vec3 color = renderColor(entity.type);
        glm::vec3 visualTint(1.0f);
        if (entity.type == EntityType::PrimedTnt) {
            const float flash = std::fmod(entity.ageSeconds, 0.25f) < 0.10f ? 1.0f : 0.0f;
            color = glm::mix(glm::vec3(0.86f, 0.18f, 0.12f), glm::vec3(1.0f), flash);
        } else if (entity.hurtFlashSeconds > 0.0f) {
            color = {1.0f, 0.16f, 0.16f};
            visualTint = color;
        } else if (entity.burningSeconds > 0.0f) {
            const float pulse = 0.08f * std::sin(entity.ageSeconds * 18.0f);
            color = {1.0f, 0.32f + pulse, 0.08f};
            visualTint = color;
        }
        const glm::vec3 position(
            glm::dvec3(entity.position) - renderOrigin);
        const glm::vec3 size=renderSize(entity.type);
        const SmoothLightSample light=m_world.sampleLight(
            entity.position+glm::dvec3(0.0,size.y*0.5,0.0));
        const bool passive = entity.type == EntityType::Cow ||
                             entity.type == EntityType::Pig ||
                             entity.type == EntityType::Sheep ||
                             entity.type == EntityType::Chicken ||
                             entity.type == EntityType::Villager;
        const bool hostileMob = entity.type == EntityType::Zombie ||
                                entity.type == EntityType::Skeleton ||
                             entity.type == EntityType::Spider ||
                             entity.type == EntityType::Blastling ||
                             entity.type == EntityType::ZombieVillager;
        if ((passive || hostileMob) && renderer.capabilities().gameplay) {
            m_modelRegistry.queue(entity.type, entity.id, entity.position,
                entity.facing, entity.behaviorSeed,
                renderOrigin, glm::vec3(0.0f), renderer.modelRenderer(),
                visualTint, light, entity.sleeping);
            continue;
        }
        if ((!passive && !hostileMob) || !renderer.capabilities().gameplay) {
            const float facingLength = std::hypot(entity.facing.x, entity.facing.z);
            const float yaw = facingLength > 0.001f
                ? std::atan2(-entity.facing.x, -entity.facing.z)
                : static_cast<float>(entity.behaviorSeed % 628u) * 0.01f;
            renderer.renderCompatibilityEntityCube(
                position, size, color, textureIndex, yaw, viewProjection, light);
            continue;
        }

    }
    for (const auto& dead : m_deadEntityRenders) {
        m_modelRegistry.queue(dead.type, dead.id, dead.position,
            dead.facing, dead.behaviorSeed, renderOrigin,
            glm::vec3(0.0f), renderer.modelRenderer(),
            glm::vec3(1.0f),
            m_world.sampleLight(dead.position+glm::dvec3(0.0,0.8,0.0)));
    }
    m_modelRegistry.endFrame();
    renderer.flushModels(viewProjection);
}

void EntityManager::initializeModels(const std::filesystem::path& assetRoot,
                                     IGameRenderer& renderer) {
    m_modelRegistry.loadAll(assetRoot);
    if (renderer.capabilities().gameplay)
        m_modelRegistry.uploadAll(renderer.modelRenderer());
}
