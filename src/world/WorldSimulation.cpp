#include "world/WorldSimulation.h"

#include "Config.h"
#include "game/SurvivalBlockLogic.h"
#include "world/Chunk.h"
#include "world/ChunkStore.h"
#include "world/World.h"
#include "world/WorldGenContext.h"
#include "world/WorldPersistence.h"

#include <algorithm>

void WorldSimulation::tickSurvival(const glm::dvec3& playerPosition,
                                   uint64_t tick, bool raining) {
    (void)playerPosition;
    auto hash = [](uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    struct Candidate { glm::ivec3 position; BlockId block; };
    std::vector<Candidate> candidates;
    m_persistence.forEachOverride(
        [&](const std::pair<int,int>& key, uint32_t index, BlockId block) {
            if (!isFarmland(block) && !isSapling(block) &&
                !(block >= BlockId::WHEAT_0 && block < BlockId::WHEAT_7)) return;
            int x = 0, z = 0, y = 0;
            decodeChunkIndex(index, x, z, y);
            candidates.push_back({{key.first * Config::CHUNK_SIZE_X + x, y,
                                   key.second * Config::CHUNK_SIZE_Z + z}, block});
        });
    for (const auto& candidate : candidates) {
        const glm::ivec3 p = candidate.position;
        if (m_world.getBlock(p.x, p.y, p.z) != candidate.block) continue;
        uint64_t random = static_cast<uint64_t>(static_cast<uint32_t>(p.x));
        random = hash(random ^ (static_cast<uint64_t>(static_cast<uint32_t>(p.z)) << 32) ^
                      static_cast<uint64_t>(p.y * 131 + tick * 37));
        if (isFarmland(candidate.block) && random % 20 == 0) {
            const BlockId next = nextFarmlandState(
                candidate.block, m_world.getBlock(p.x, p.y + 1, p.z),
                hasWaterForFarmland(p, raining), random);
            if (next != candidate.block) m_world.setBlock(p.x, p.y, p.z, next);
        } else if (candidate.block >= BlockId::WHEAT_0 &&
                   candidate.block < BlockId::WHEAT_7) {
            const BlockId soil = m_world.getBlock(p.x, p.y - 1, p.z);
            const BlockId next = nextCropState(candidate.block, soil, random);
            if (next != candidate.block) m_world.setBlock(p.x, p.y, p.z, next);
        } else if (isSapling(candidate.block) && random % 300 == 0) {
            growSapling(p, candidate.block);
        }
    }
}

void WorldSimulation::tickWeather(const WeatherSystem& weather, bool daytime,
                                  uint64_t tick) {
    auto hash = [](uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    auto positionRandom = [&](const glm::ivec3& p, uint64_t salt) {
        uint64_t value = static_cast<uint32_t>(p.x);
        value ^= static_cast<uint64_t>(static_cast<uint32_t>(p.z)) << 32;
        value ^= static_cast<uint64_t>(static_cast<uint32_t>(p.y)) *
                 0x9e3779b97f4a7c15ULL;
        return hash(value ^ Config::WORLD_SEED ^ tick * 37ULL ^ salt);
    };

    // One deterministic precipitation candidate per active chunk per second.
    // This keeps accumulation bounded independently of render distance.
    for (const Chunk* chunk : m_world.getActiveChunks()) {
        if (!chunk->generated.load()) continue;
        const uint64_t random = hash(
            Config::WORLD_SEED ^ tick * 131ULL ^
            static_cast<uint64_t>(static_cast<uint32_t>(chunk->cx)) ^
            (static_cast<uint64_t>(static_cast<uint32_t>(chunk->cz)) << 32));
        const int x = chunk->worldX() + static_cast<int>(random % 16);
        const int z = chunk->worldZ() + static_cast<int>((random >> 8) % 16);
        const int surface = m_world.getSurfaceY(x, z);
        if (!Config::isValidWorldY(surface)) continue;
        const BlockId top = m_world.getBlock(x, surface, z);
        if (weather.raining() &&
            m_world.precipitationAt(x, surface + 1, z) == PrecipitationType::Snow) {
            const int layerY = surface + 1;
            if (Config::isValidWorldY(layerY) && isSolid(top) &&
                !isFarmland(top) && top != BlockId::ICE &&
                m_world.getBlock(x, layerY, z) == BlockId::AIR &&
                m_world.getBlockLight(x, layerY, z) <= 9) {
                m_world.setBlock(x, layerY, z, BlockId::SNOW_LAYER);
            }
        } else if (weather.type() == WeatherType::Clear && daytime &&
                   top == BlockId::SNOW_LAYER && random % 2 == 0) {
            m_world.setBlock(x, surface, z, BlockId::AIR);
        }
    }

    struct FireCell { glm::ivec3 position; };
    std::vector<FireCell> fires;
    fires.reserve(256);
    m_persistence.forEachOverride(
        [&](const std::pair<int,int>& key, uint32_t index, BlockId block) {
            if (block != BlockId::FIRE || fires.size() >= 256) return;
            int x = 0, z = 0, y = 0;
            decodeChunkIndex(index, x, z, y);
            fires.push_back({{key.first * Config::CHUNK_SIZE_X + x, y,
                              key.second * Config::CHUNK_SIZE_Z + z}});
        });

    for (const FireCell& cell : fires) {
        const glm::ivec3 p = cell.position;
        if (m_world.getBlock(p.x, p.y, p.z) != BlockId::FIRE) {
            m_fireAges.erase(p);
            continue;
        }
        uint8_t& age = m_fireAges[p];
        const uint64_t random = positionRandom(p, age);
        age = static_cast<uint8_t>(std::min<int>(15, age + random % 3));
        const bool exposedRain = weather.raining() &&
            m_world.precipitationAt(p.x, p.y, p.z) == PrecipitationType::Rain &&
            m_world.hasSkyAccess(p.x, p.y + 1, p.z);
        if ((exposedRain && random % 100 <
                static_cast<uint64_t>(20 + age * 3)) ||
            (age == 15 && (random >> 9) % 4 == 0)) {
            m_world.setBlock(p.x, p.y, p.z, BlockId::AIR);
            m_fireAges.erase(p);
            continue;
        }

        bool hasFuel = false;
        for (size_t direction = 0; direction < FACE_OFFSETS.size(); ++direction) {
            const glm::ivec3 q = p + FACE_OFFSETS[direction];
            const BlockId block = m_world.getBlock(q.x, q.y, q.z);
            if (block == BlockId::TNT) {
                m_world.setBlock(q.x, q.y, q.z, BlockId::AIR);
                m_tntIgnitions.push_back(q);
                hasFuel = true;
                continue;
            }
            if (!isFlammable(block)) continue;
            hasFuel = true;
            const uint64_t roll = positionRandom(q, direction + age * 17ULL);
            if (roll % 300 < burnOdds(block)) {
                m_world.setBlock(q.x, q.y, q.z,
                                 (roll >> 10) % 2 == 0 ? BlockId::FIRE : BlockId::AIR);
            }
        }

        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint64_t roll = hash(random + static_cast<uint64_t>(attempt) *
                                       0x9e3779b97f4a7c15ULL);
            glm::ivec3 q = p + glm::ivec3(
                static_cast<int>(roll % 3) - 1,
                static_cast<int>((roll >> 8) % 6) - 1,
                static_cast<int>((roll >> 16) % 3) - 1);
            if (!Config::isValidWorldY(q.y) ||
                m_world.getBlock(q.x, q.y, q.z) != BlockId::AIR) continue;
            uint8_t encouragement = 0;
            for (const glm::ivec3& offset : FACE_OFFSETS) {
                encouragement = std::max(
                    encouragement, fireEncouragement(m_world.getBlock(
                        q.x + offset.x, q.y + offset.y, q.z + offset.z)));
            }
            if (encouragement > 0 &&
                (roll >> 24) % (300 + 20 * age) < encouragement)
                m_world.setBlock(q.x, q.y, q.z, BlockId::FIRE);
        }

        const bool supported = isSolid(m_world.getBlock(p.x, p.y - 1, p.z));
        if (age > 3 && !supported && !hasFuel) {
            m_world.setBlock(p.x, p.y, p.z, BlockId::AIR);
            m_fireAges.erase(p);
        }
    }
}

std::vector<glm::ivec3> WorldSimulation::takeTntIgnitions() {
    std::vector<glm::ivec3> result;
    result.swap(m_tntIgnitions);
    return result;
}

bool WorldSimulation::hasWaterForFarmland(const glm::ivec3& position,
                                          bool raining) const {
    if (raining && m_world.precipitationAt(position.x, position.y + 1, position.z) ==
                       PrecipitationType::Rain &&
        m_world.hasSkyAccess(position.x, position.y + 1, position.z)) return true;
    for (int y = position.y; y <= position.y + 1; ++y)
        for (int x = position.x - 4; x <= position.x + 4; ++x)
            for (int z = position.z - 4; z <= position.z + 4; ++z)
                if (isWater(m_world.getBlock(x, y, z))) return true;
    return false;
}

bool WorldSimulation::growSapling(const glm::ivec3& p, BlockId sapling) {
    const int type = static_cast<int>(sapling) - static_cast<int>(BlockId::OAK_SAPLING);
    const BlockId woods[] = {BlockId::WOOD, BlockId::BIRCH_WOOD, BlockId::SPRUCE_WOOD,
                             BlockId::JUNGLE_WOOD, BlockId::ACACIA_WOOD};
    const BlockId leaves[] = {BlockId::LEAVES, BlockId::BIRCH_LEAVES, BlockId::SPRUCE_LEAVES,
                              BlockId::JUNGLE_LEAVES, BlockId::ACACIA_LEAVES};
    uint64_t h = WorldGenContext::hashPosition(Config::WORLD_SEED, p.x, p.y, p.z);
    const int heights[] = {4 + static_cast<int>(h % 3), 5 + static_cast<int>(h % 3),
                           6 + static_cast<int>(h % 5), 8 + static_cast<int>(h % 5),
                           5 + static_cast<int>(h % 3)};
    const int height = heights[type];
    struct Placement { glm::ivec3 position; BlockId block; bool trunk; };
    std::vector<Placement> placements;
    for (int y = 0; y < height; ++y) placements.push_back({p + glm::ivec3(0,y,0), woods[type], true});
    auto leaf = [&](int dx, int y, int dz) {
        placements.push_back({p + glm::ivec3(dx,y,dz), leaves[type], false});
    };
    if (type == 0 || type == 1) {
        const int base = height - 2, layers = type == 0 ? 4 : 3;
        for (int y = 0; y < layers; ++y) {
            const int radius = type == 0 && y < 2 ? 2 : 1;
            for (int dx=-radius; dx<=radius; ++dx) for (int dz=-radius; dz<=radius; ++dz)
                if (!(type == 0 && std::abs(dx)==radius && std::abs(dz)==radius &&
                      (static_cast<int>(h) + dx*7 + dz*13)%3==0)) leaf(dx,base+y,dz);
        }
        if (type == 1) leaf(0, base + 3, 0);
    } else if (type == 2) {
        const int base = height - 4;
        for (int y=0;y<5;++y) { const int r=y<2?2:y<4?1:0;
            for(int dx=-r;dx<=r;++dx)for(int dz=-r;dz<=r;++dz)leaf(dx,base+y,dz); }
        leaf(0,base+5,0);
    } else if (type == 3) {
        const int base=height-3;
        for(int y=0;y<5;++y){const int r=y<2?3:2;
            for(int dx=-r;dx<=r;++dx)for(int dz=-r;dz<=r;++dz)
                if(dx*dx+dz*dz<=r*r)leaf(dx,base+y,dz);}
    } else {
        const int y=height-1;
        for(int dx=-2;dx<=2;++dx)for(int dz=-2;dz<=2;++dz)
            if(!(std::abs(dx)==2&&std::abs(dz)==2))leaf(dx,y,dz);
        leaf(0,y+1,0);
    }
    for (const auto& placement : placements) {
        const auto& q = placement.position;
        if (!Config::isValidWorldY(q.y)) return false;
        const int cx = World::worldToChunkX(q.x), cz = World::worldToChunkZ(q.z);
        if (!m_chunks.isGenerated(cx, cz)) return false;
        const BlockId current = m_world.getBlock(q.x,q.y,q.z);
        const bool replaceable = current == BlockId::AIR || isSapling(current) ||
            current == BlockId::LEAVES || current == BlockId::BIRCH_LEAVES ||
            current == BlockId::SPRUCE_LEAVES || current == BlockId::JUNGLE_LEAVES ||
            current == BlockId::ACACIA_LEAVES;
        if (!replaceable && !(q == p)) return false;
    }
    for (const auto& placement : placements) {
        const BlockId current = m_world.getBlock(placement.position.x, placement.position.y, placement.position.z);
        if (placement.trunk || current == BlockId::AIR || isSapling(current))
            m_world.setBlock(placement.position.x, placement.position.y, placement.position.z, placement.block);
    }
    return true;
}
