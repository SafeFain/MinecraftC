#include "world/World.h"
#include "world/VoxelRaycast.h"
#include "world/BiomeLocator.h"
#include "core/RuntimeClock.h"
#include "world/ChunkMesh.h"
#include "world/RegionGenerator.h"
#include "renderer/Renderer.h"
#include "threading/ThreadPool.h"
#include "debug/Log.h"
#include "Config.h"
#include "game/SaveStore.h"
#include "game/SurvivalRules.h"
#include "game/SurvivalBlockLogic.h"
#include "world/BlockEntityLogic.h"
#include "world/BlockLightLogic.h"
#include "world/FluidLogic.h"
#include "world/WorldGenContext.h"

#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <unordered_set>
#include <limits>

World::World() : m_generator(Config::WORLD_SEED) {}

void World::resetForNewSeed(uint64_t newSeed) {
    m_meshes.releaseAllMeshes();
    m_chunks.withUnique([&](ChunkStore& store) {
        store.clearUnlocked();
    });
    m_fluids.clear();
    m_streamer.clear();
    m_persistence.clear();
    m_simulation.clear();
    m_lighting.reset();
    // Placement-new: WorldGenerator contains reference members (Noise&),
    // so move assignment is deleted. Reconstruct in-place.
    m_generator.~WorldGenerator();
    new (&m_generator) WorldGenerator(newSeed);
    Config::WORLD_SEED = newSeed;
}

World::~World() {
    m_meshes.releaseAllMeshes();
}

// ── Block queries ─────────────────────────────────────────────────────

BlockId World::getBlock(int worldX, int worldY, int worldZ) const {
    if (!Config::isValidWorldY(worldY)) {
        return BlockId::AIR;
    }

    int cx = worldToChunkX(static_cast<double>(worldX));
    int cz = worldToChunkZ(static_cast<double>(worldZ));

    int lx = worldX - cx * Config::CHUNK_SIZE_X;
    int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    if (lx < 0) { cx -= 1; lx += Config::CHUNK_SIZE_X; }
    if (lz < 0) { cz -= 1; lz += Config::CHUNK_SIZE_Z; }

    const Chunk* chunk = m_chunks.find(cx, cz);
    if (chunk != nullptr) {
        return chunk->getBlock(lx, worldY, lz);
    }

    return BlockId::AIR;
}
uint8_t World::getBlockLight(int worldX, int worldY, int worldZ) const {
    return getLight(worldX, worldY, worldZ).block;
}

uint8_t World::getSkyLight(int worldX, int worldY, int worldZ) const {
    return getLight(worldX, worldY, worldZ).sky;
}

LightSample World::getLight(int worldX, int worldY, int worldZ) const {
    if (!Config::isValidWorldY(worldY)) return {};
    const int cx = worldToChunkX(worldX), cz = worldToChunkZ(worldZ);
    const int lx = worldX - cx * Config::CHUNK_SIZE_X;
    const int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    const Chunk* chunk = m_chunks.find(cx, cz);
    return chunk == nullptr ? LightSample{} :
        unpackLight(chunk->getPackedLight(lx, worldY, lz));
}

SmoothLightSample World::sampleLight(const glm::dvec3& position) const {
    const int x0=static_cast<int>(std::floor(position.x));
    const int y0=static_cast<int>(std::floor(position.y));
    const int z0=static_cast<int>(std::floor(position.z));
    const glm::dvec3 fraction=position-glm::dvec3(x0,y0,z0);
    double sky=0.0,block=0.0;
    for(int dz=0;dz<=1;++dz)for(int dy=0;dy<=1;++dy)for(int dx=0;dx<=1;++dx){
        const double weight=(dx?fraction.x:1.0-fraction.x)*
            (dy?fraction.y:1.0-fraction.y)*(dz?fraction.z:1.0-fraction.z);
        const LightSample light=getLight(x0+dx,y0+dy,z0+dz);
        sky+=weight*light.sky;block+=weight*light.block;
    }
    return {static_cast<float>(sky/15.0),static_cast<float>(block/15.0)};
}

int World::getSurfaceY(int worldX, int worldZ) const {
    const int cx = worldToChunkX(worldX), cz = worldToChunkZ(worldZ);
    const int lx = worldX - cx * Config::CHUNK_SIZE_X;
    const int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    const Chunk* chunk = m_chunks.find(cx, cz);
    if (chunk == nullptr || !chunk->generated.load())
        return Config::WORLD_MAX_Y;
    return chunk->getColumnMaxY(lx, lz);
}

bool World::hasSkyAccess(int worldX, int worldY, int worldZ) const {
    return worldY >= getSurfaceY(worldX, worldZ);
}

PrecipitationType World::precipitationAt(
    int worldX, int worldY, int worldZ) const {
    const HeightBiome sample = m_generator.queryHeightBiome(worldX, worldZ);
    return precipitationFor(sample.biome, worldY);
}

Biome World::biomeAt(int worldX, int worldZ) const {
    return m_generator.queryHeightBiome(worldX, worldZ).biome;
}

std::optional<glm::ivec2> World::locateBiome(
    Biome biome, int worldX, int worldZ) const {
    return locateNearestBiome(glm::ivec2(worldX, worldZ), biome,
        [this](int x, int z) { return biomeAt(x, z); });
}

glm::dvec3 World::findSafeSpawn(int maximumRadius) const {
    glm::ivec2 best{0};
    glm::ivec2 fallback{0};
    bool hasFallback = false;
    int bestScore = std::numeric_limits<int>::max();
    constexpr int step = 8;
    for (int radius = 0; radius <= maximumRadius; radius += step) {
        for (int z = -radius; z <= radius; z += step) {
            for (int x = -radius; x <= radius; x += step) {
                if (radius > 0 && std::abs(x) != radius && std::abs(z) != radius)
                    continue;
                const SurfaceColumn center = m_generator.sampleTerrainColumn(x, z);
                if (center.height <= center.waterLevel || center.river ||
                    center.biome == Biome::OCEAN || center.biome == Biome::DEEP_OCEAN ||
                    center.biome == Biome::BLACK_SAND_COAST ||
                    center.height >= Config::WORLD_MAX_Y - 8)
                    continue;
                if (!hasFallback) {
                    fallback = {x, z};
                    hasFallback = true;
                }
                int relief = 0;
                for (const glm::ivec2 offset : {glm::ivec2{-2, 0}, {2, 0},
                                                {0, -2}, {0, 2}}) {
                    const SurfaceColumn neighbor = m_generator.sampleTerrainColumn(
                        x + offset.x, z + offset.y);
                    relief = std::max(relief, std::abs(neighbor.height - center.height));
                }
                const int score = relief * 100 + radius;
                if (relief <= 2 && score < bestScore) {
                    best = {x, z};
                    bestScore = score;
                }
            }
        }
        if (bestScore != std::numeric_limits<int>::max()) break;
    }
    if (bestScore == std::numeric_limits<int>::max() && hasFallback)
        best = fallback;
    const SurfaceColumn chosen = m_generator.sampleTerrainColumn(best.x, best.y);
    return {static_cast<double>(best.x) + 0.5,
            static_cast<double>(chosen.height) + 1.01,
            static_cast<double>(best.y) + 0.5};
}

void World::setBlock(int worldX, int worldY, int worldZ, BlockId id) {
    setBlockInternal(worldX,worldY,worldZ,id,true);
}

void World::setDerivedBlock(const glm::ivec3& position, BlockId id) {
    if (!generatedAt(position.x,position.z)) return;
    // Flow depth is reconstructed from persisted sources and terrain. Keeping
    // it out of overrides prevents waterfalls from turning into huge saves.
    setBlockInternal(position.x,position.y,position.z,id,false);
}

void World::setBlockInternal(int worldX, int worldY, int worldZ, BlockId id,
                             bool recordOverride) {
    if (!Config::isValidWorldY(worldY)) return;
    const BlockId previous = getBlock(worldX, worldY, worldZ);
    if (previous == id) return;

    int cx = worldToChunkX(static_cast<double>(worldX));
    int cz = worldToChunkZ(static_cast<double>(worldZ));

    int lx = worldX - cx * Config::CHUNK_SIZE_X;
    int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    if (lx < 0) { cx -= 1; lx += Config::CHUNK_SIZE_X; }
    if (lz < 0) { cz -= 1; lz += Config::CHUNK_SIZE_Z; }

    Chunk* chunk = getChunk(cx, cz);
    chunk->setBlock(lx, worldY, lz, id);
    const uint32_t localIndex = static_cast<uint32_t>(
        lx + lz * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(worldY) *
            Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
    if (recordOverride) {
        m_persistence.recordOverride(cx, cz, localIndex, id);
    }
    m_lighting.updateLightingAt({worldX,worldY,worldZ});

    if (lx == 0)                   m_chunks.markDirty(cx - 1, cz);
    if (lx == Config::CHUNK_SIZE_X - 1) m_chunks.markDirty(cx + 1, cz);
    if (lz == 0)                   m_chunks.markDirty(cx, cz - 1);
    if (lz == Config::CHUNK_SIZE_Z - 1) m_chunks.markDirty(cx, cz + 1);
    m_fluids.scheduleAround({worldX, worldY, worldZ});
    if (id == BlockId::AIR && previous == BlockId::SUNFLOWER_BOTTOM &&
        worldY + 1 < Config::WORLD_MAX_Y &&
        getBlock(worldX, worldY + 1, worldZ) == BlockId::SUNFLOWER_TOP)
        setBlockInternal(worldX,worldY+1,worldZ,BlockId::AIR,recordOverride);
    if (id == BlockId::AIR && previous == BlockId::SUNFLOWER_TOP &&
        worldY > Config::WORLD_MIN_Y &&
        getBlock(worldX, worldY - 1, worldZ) == BlockId::SUNFLOWER_BOTTOM)
        setBlockInternal(worldX,worldY-1,worldZ,BlockId::AIR,recordOverride);
}

bool World::generatedAt(int worldX, int worldZ) const {
    const int cx = worldToChunkX(worldX);
    const int cz = worldToChunkZ(worldZ);
    return m_chunks.isGenerated(cx, cz);
}

// ── Chunk access ──────────────────────────────────────────────────────

Chunk* World::getChunk(int cx, int cz) {
    return m_chunks.get(cx, cz);
}

std::optional<World::RaycastHit> World::raycast(const glm::dvec3& origin,
                                                 const glm::vec3& direction,
                                                 float maxDistance) const {
    const auto hit = voxelRaycast(
        origin, direction, static_cast<double>(maxDistance),
        [this](const glm::ivec3& blockPos) {
            if (!Config::isValidWorldY(blockPos.y)) return false;
            const BlockId id = getBlock(blockPos.x, blockPos.y, blockPos.z);
            if (id == BlockId::AIR) return false;
            const BlockProperties& props = getBlockProps(id);
            return props.solid || props.shape == RenderShape::Cross;
        });
    if (!hit) return std::nullopt;
    return RaycastHit{hit->blockPos, hit->faceNormal};
}
