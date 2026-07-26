#include "entity/EntityManager.h"
#include "entity/EntityLogic.h"

#include "player/Player.h"
#include "renderer/Renderer.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>

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
    m_spawnTimer = 0.0f;
    m_spawnSequence = 0;
    m_loadedChunks.clear();
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
        m_entities.push_back(entity);
    }
}

void EntityManager::syncChunks() {
    if (!m_saveStore) return;
    std::set<std::pair<int,int>> active;
    for (const Chunk* chunk : m_world.getActiveChunks())
        if (chunk->generated.load()) active.insert({chunk->cx,chunk->cz});
    for (const auto& key : m_loadedChunks) if (!active.count(key)) {
        std::vector<WorldMetadata::PersistedEntity> saved;
        for (const auto& entity : m_entities) if (entityChunk(entity.position)==key) {
            saved.push_back({static_cast<uint8_t>(entity.type),entity.position,entity.velocity,
                entity.health,entity.ageSeconds,entity.item,entity.behaviorSeed,
                static_cast<uint8_t>((entity.inGround?1:0)|(entity.playerOwned?2:0)),
                entity.projectileDamage});
        }
        m_saveStore->saveChunkEntities(key.first,key.second,saved);
    }
    m_entities.erase(std::remove_if(m_entities.begin(),m_entities.end(),[&](const Entity& entity){
        const auto key=entityChunk(entity.position);
        return m_loadedChunks.count(key) && !active.count(key);
    }),m_entities.end());
    for (const auto& key : active) if (!m_loadedChunks.count(key))
        loadEntities(m_saveStore->loadChunkEntities(key.first,key.second));
    m_loadedChunks=std::move(active);
}

void EntityManager::flushChunkEntities() {
    if (!m_saveStore) return;
    for (const auto& key:m_loadedChunks) {
        std::vector<WorldMetadata::PersistedEntity> saved;
        for(const auto& entity:m_entities) if(entityChunk(entity.position)==key)
            saved.push_back({static_cast<uint8_t>(entity.type),entity.position,entity.velocity,
                entity.health,entity.ageSeconds,entity.item,entity.behaviorSeed,
                static_cast<uint8_t>((entity.inGround?1:0)|(entity.playerOwned?2:0)),
                entity.projectileDamage});
        m_saveStore->saveChunkEntities(key.first,key.second,saved);
    }
}

void EntityManager::spawnItem(
    const glm::dvec3& position, ItemStack stack, const glm::vec3& velocity) {
    if (stack.empty()) return;
    Entity entity;
    entity.id = m_nextId++;
    entity.type = EntityType::Item;
    entity.position = position;
    entity.velocity = velocity;
    entity.item = stack;
    entity.behaviorSeed = hash32(static_cast<uint32_t>(entity.id));
    m_entities.push_back(entity);
}

void EntityManager::spawnArrow(const glm::dvec3& position, const glm::vec3& velocity,
                               float damage, bool playerOwned) {
    Entity entity;
    entity.id = m_nextId++;
    entity.type = EntityType::Arrow;
    entity.position = position;
    entity.velocity = velocity;
    entity.projectileDamage = damage;
    entity.playerOwned = playerOwned;
    entity.health = 1.0f;
    m_entities.push_back(entity);
}

void EntityManager::spawnMob(EntityType type, const glm::dvec3& position) {
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
        case EntityType::Arrow: return;
        case EntityType::Item: return;
    }
    m_entities.push_back(entity);
}

bool EntityManager::hostile(EntityType type) {
    return type == EntityType::Zombie || type == EntityType::Skeleton ||
           type == EntityType::Spider || type == EntityType::Blastling;
}

bool EntityManager::hasHostileNear(const glm::dvec3& position, float radius) const {
    return std::any_of(m_entities.begin(), m_entities.end(),
        [&](const Entity& entity) {
            return hostile(entity.type) && entity.health > 0.0f &&
                   glm::distance(entity.position, position) <= radius;
        });
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
    for (int y = 127; y >= 0; --y) {
        if (isSolid(m_world.getBlock(x, y, z))) {
            surface = y + 1;
            break;
        }
    }
    if (surface < 1 || surface >= 126 ||
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
    const EntityType type = spawnHostile
        ? hostileTypes[random % 4] : passiveTypes[random % 4];
    spawnMob(type, glm::dvec3(x + 0.5, static_cast<double>(surface), z + 0.5));
}

void EntityManager::moveWithTerrain(
    Entity& entity, const glm::vec3& horizontal, float dt) {
    const double distance = glm::length(horizontal) * dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / 0.2)));
    const glm::dvec3 delta = glm::dvec3(horizontal) *
        (static_cast<double>(dt) / steps);
    bool moved = false;
    for (int i=0;i<steps;++i) {
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
    }
    entity.stuckSeconds = moved ? 0.0f : entity.stuckSeconds + dt;
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
    for(int y=minY;y<=maxY;++y) for(int z=minZ;z<=maxZ;++z) for(int x=minX;x<=maxX;++x)
        if(isSolid(m_world.getBlock(x,y,z))) return true;
    return false;
}

void EntityManager::update(Player& player, float dt, bool hostileSpawning, bool peaceful) {
    struct PendingArrow { glm::dvec3 position; glm::vec3 velocity; float damage; };
    std::vector<PendingArrow> pendingArrows;
    m_spawnTimer += dt;
    if (m_spawnTimer >= 4.0f) {
        spawnAroundPlayer(player.getPosition(), hostileSpawning);
        m_spawnTimer = 0.0f;
    }

    for (auto& entity : m_entities) {
        if (peaceful && hostile(entity.type)) { entity.health=0.0f; continue; }
        entity.ageSeconds += dt;
        entity.actionCooldown = std::max(0.0f, entity.actionCooldown - dt);
        if (entity.type == EntityType::Arrow) {
            updateArrow(entity, player, dt);
            continue;
        }
        if (entity.type == EntityType::Item) {
            entity.velocity.y -= 20.0f * dt;
            const glm::dvec3 next = entity.position + glm::dvec3(entity.velocity) *
                static_cast<double>(dt);
            const int belowY = static_cast<int>(std::floor(next.y - 0.05f));
            if (belowY >= 0 && isSolid(m_world.getBlock(
                    static_cast<int>(std::floor(next.x)), belowY,
                    static_cast<int>(std::floor(next.z))))) {
                entity.velocity = glm::vec3(0.0f);
                entity.position.y = belowY + 1.05f;
            } else {
                entity.position = next;
            }
            if (glm::distance(entity.position, player.getPosition()) < 1.6) {
                const uint32_t remaining = player.inventory().add(entity.item);
                entity.item.count = static_cast<uint8_t>(remaining);
                if (remaining == 0) entity.health = 0.0f;
            }
            continue;
        }

        const glm::vec3 delta = glm::vec3(player.getPosition() - entity.position);
        glm::vec3 horizontal(delta.x, 0.0f, delta.z);
        const float distance = glm::length(horizontal);
        if (hostile(entity.type) && shouldHostileDespawn(distance,entity.ageSeconds,
            hash32(entity.behaviorSeed + static_cast<uint32_t>(entity.ageSeconds)))) {
            entity.health = 0.0f;
            continue;
        }
        if (hostile(entity.type) && distance < 18.0f && distance > 0.01f) {
            horizontal /= distance;
            const glm::dvec3 attackOrigin=entity.position+glm::dvec3(0,1.2,0);
            const glm::vec3 sightDirection=glm::normalize(glm::vec3(player.getEyePosition()-attackOrigin));
            const bool clearSight=!m_world.raycast(attackOrigin,sightDirection,distance).has_value();
            const float speed = entity.type == EntityType::Spider ? 3.0f : 2.0f;
            if (entity.type == EntityType::Skeleton) {
                if (distance > 7.0f) moveWithTerrain(entity, horizontal * speed, dt);
                if (distance < 14.0f && clearSight && entity.actionCooldown <= 0.0f) {
                    glm::vec3 aim=glm::normalize(glm::vec3(player.getEyePosition()-
                        (entity.position+glm::dvec3(0,1.45,0))));
                    pendingArrows.push_back({entity.position+glm::dvec3(0,1.45,0)+
                        glm::dvec3(aim)*0.75,aim*22.0f,2.0f});
                    entity.actionCooldown = 2.0f;
                }
            } else {
                moveWithTerrain(entity, horizontal * speed, dt);
            }
            if (entity.type != EntityType::Skeleton &&
                distance < 1.5f && clearSight && entity.actionCooldown <= 0.0f) {
                player.takeDamage(entity.type == EntityType::Blastling ? 6.0f : 3.0f);
                entity.actionCooldown = 1.0f;
                if (entity.type == EntityType::Blastling) {
                    const glm::ivec3 center(glm::floor(entity.position));
                    for (int dz = -2; dz <= 2; ++dz)
                        for (int dy = -2; dy <= 2; ++dy)
                            for (int dx = -2; dx <= 2; ++dx) {
                                if (dx * dx + dy * dy + dz * dz > 4) continue;
                                const glm::ivec3 position = center + glm::ivec3(dx, dy, dz);
                                const BlockId block = m_world.getBlock(
                                    position.x, position.y, position.z);
                                if (block != BlockId::AIR && block != BlockId::BEDROCK)
                                    m_world.setBlock(position.x, position.y, position.z,
                                                     BlockId::AIR);
                            }
                    entity.health = 0.0f;
                }
            }
        } else {
            const float angle = static_cast<float>(
                hash32(entity.behaviorSeed + static_cast<uint32_t>(entity.ageSeconds / 3.0f))
                % 6283) * 0.001f;
            const float offset=entity.stuckSeconds>0.6f ? 1.5707963f : 0.0f;
            moveWithTerrain(entity, {std::cos(angle+offset) * 0.65f, 0.0f,
                                     std::sin(angle+offset) * 0.65f}, dt);
        }
    }

    for (const auto& arrow : pendingArrows)
        spawnArrow(arrow.position, arrow.velocity, arrow.damage, false);

    std::vector<Entity> deadMobs;
    for (const auto& entity : m_entities)
        if (entity.health <= 0.0f && entity.type != EntityType::Item)
            deadMobs.push_back(entity);
    m_entities.erase(std::remove_if(m_entities.begin(), m_entities.end(),
        [](const Entity& entity) {
            return entity.health <= 0.0f ||
                   (entity.type == EntityType::Item && entity.ageSeconds >= 300.0f) ||
                   (entity.type == EntityType::Arrow && entity.ageSeconds >= 60.0f);
        }), m_entities.end());
    for (const auto& entity : deadMobs) dropMobLoot(entity);
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
        case EntityType::Blastling:
        case EntityType::Arrow:
        case EntityType::Item: break;
    }
    for (const auto& stack : loot) spawnItem(entity.position, stack);
}

bool EntityManager::attackRay(
    const glm::dvec3& origin, const glm::vec3& direction, float reach, float damage) {
    Entity* best = nullptr;
    float bestAlong = reach + 1.0f;
    for (auto& entity : m_entities) {
        if (entity.type == EntityType::Item || entity.type == EntityType::Arrow || entity.health <= 0.0f) continue;
        const glm::dvec3 center = entity.position + glm::dvec3(renderSize(entity.type)) * 0.5;
        const glm::vec3 toEntity = glm::vec3(center - origin);
        const float along = glm::dot(toEntity, direction);
        if (along < 0.0f || along > reach) continue;
        const float perpendicular = glm::length(toEntity - direction * along);
        if (perpendicular < 0.75f && along < bestAlong) {
            best = &entity;
            bestAlong = along;
        }
    }
    if (!best) return false;
    best->health -= damage;
    best->velocity += direction * 4.0f;
    return true;
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
    arrow.velocity.y-=9.8f*dt;
    const glm::dvec3 start=arrow.position;
    const glm::dvec3 delta=glm::dvec3(arrow.velocity)*static_cast<double>(dt);
    const int steps=sweptCollisionSteps(glm::length(delta));
    for(int step=1;step<=steps;++step) {
        const glm::dvec3 next=start+delta*(static_cast<double>(step)/steps);
        const glm::ivec3 block(glm::floor(next));
        if(isSolid(m_world.getBlock(block.x,block.y,block.z))) {
            arrow.position=next;arrow.velocity={0,0,0};arrow.inGround=true;return;
        }
        if (!arrow.playerOwned) {
            const glm::dvec3 playerMin=player.getPosition()+glm::dvec3(-.3,0,-.3);
            const glm::dvec3 playerMax=player.getPosition()+glm::dvec3(.3,1.8,.3);
            if(next.x>=playerMin.x&&next.x<=playerMax.x&&next.y>=playerMin.y&&
               next.y<=playerMax.y&&next.z>=playerMin.z&&next.z<=playerMax.z) {
                player.takeDamage(arrow.projectileDamage);
                arrow.health=0;return;
            }
        }
        for(auto& target:m_entities) {
            if(&target==&arrow || target.type==EntityType::Item || target.type==EntityType::Arrow || target.health<=0) continue;
            const glm::vec3 size=renderSize(target.type);
            const glm::dvec3 min=target.position+glm::dvec3(-size.x*.5,0,-size.z*.5);
            const glm::dvec3 max=target.position+glm::dvec3(size.x*.5,size.y,size.z*.5);
            if(next.x>=min.x&&next.x<=max.x&&next.y>=min.y&&next.y<=max.y&&next.z>=min.z&&next.z<=max.z) {
                target.health-=arrow.projectileDamage;
                target.velocity+=glm::normalize(arrow.velocity)*3.0f;
                arrow.health=0;return;
            }
        }
        arrow.position=next;
    }
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
        case EntityType::Arrow: return {0.58f,0.42f,0.20f};
    }
    return {1.0f, 0.0f, 1.0f};
}

glm::vec3 EntityManager::renderSize(EntityType type) {
    switch (type) {
        case EntityType::Item: return {0.25f, 0.25f, 0.25f};
        case EntityType::Arrow: return {0.08f,0.08f,0.75f};
        case EntityType::Chicken: return {0.45f, 0.65f, 0.45f};
        case EntityType::Spider: return {1.2f, 0.55f, 1.2f};
        case EntityType::Cow:
        case EntityType::Pig:
        case EntityType::Sheep: return {0.9f, 1.2f, 1.3f};
        default: return {0.65f, 1.75f, 0.65f};
    }
}

void EntityManager::render(
    Renderer& renderer, const glm::mat4& viewProjection,
    const glm::dvec3& renderOrigin) const {
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
            case EntityType::Item: textureIndex = 8; break;
            case EntityType::Arrow: textureIndex = 8; break;
        }
        renderer.renderEntity(
                              glm::vec3(glm::dvec3(entity.position) - renderOrigin),
                              renderSize(entity.type),
                              renderColor(entity.type), textureIndex,
                              viewProjection);
    }
}
