#include "world/World.h"
#include "world/VoxelRaycast.h"
#include "world/BiomeLocator.h"
#include "core/RuntimeClock.h"
#include "world/ChunkMesh.h"
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

namespace {
bool rayIntersectsBlockBounds(const glm::dvec3& origin,
                              const glm::dvec3& direction,
                              double maximumDistance,
                              const glm::ivec3& block,
                              float height) {
    double minimum = 0.0;
    double maximum = maximumDistance;
    const glm::dvec3 boundsMin(block);
    const glm::dvec3 boundsMax = boundsMin + glm::dvec3(1.0, height, 1.0);
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12) {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis])
                return false;
            continue;
        }
        double nearDistance = (boundsMin[axis] - origin[axis]) / direction[axis];
        double farDistance = (boundsMax[axis] - origin[axis]) / direction[axis];
        if (nearDistance > farDistance) std::swap(nearDistance, farDistance);
        minimum = std::max(minimum, nearDistance);
        maximum = std::min(maximum, farDistance);
        if (minimum > maximum) return false;
    }
    return maximum >= 0.0 && minimum <= maximumDistance;
}

}

void World::beginFluidBatch(uint64_t tick) {
    m_currentFluidTick = tick;
    m_fluidBatchActive = true;
    m_fluidMutations.clear();
    m_fluidMutations.reserve(512);
    m_uniqueFluidMutations.clear();
    m_fluidLightingPositions.clear();
    m_fluidMutationIndices.clear();
}

const std::vector<World::FluidMutation>& World::endFluidBatch() {
    if (!m_fluidBatchActive) return m_uniqueFluidMutations;
    m_fluidBatchActive = false;

    m_uniqueFluidMutations.reserve(m_fluidMutations.size());
    m_fluidMutationIndices.reserve(m_fluidMutations.size());
    for (const FluidMutation& mutation : m_fluidMutations) {
        const auto [it, inserted] = m_fluidMutationIndices.emplace(
            mutation.position, m_uniqueFluidMutations.size());
        if (inserted) {
            m_uniqueFluidMutations.push_back(mutation);
        } else {
            FluidMutation& existing = m_uniqueFluidMutations[it->second];
            existing.current = mutation.current;
        }
    }
    m_fluidMutations.clear();

    m_fluidLightingPositions.reserve(m_uniqueFluidMutations.size());
    for (const FluidMutation& mutation : m_uniqueFluidMutations) {
        if (mutation.previous == mutation.current) continue;
        if (getLightEmission(mutation.previous) !=
                getLightEmission(mutation.current) ||
            getLightDampening(mutation.previous) !=
                getLightDampening(mutation.current))
            m_fluidLightingPositions.push_back(mutation.position);
    }
    if (!m_fluidLightingPositions.empty())
        m_lighting.updateLightingBatch(m_fluidLightingPositions);
    return m_uniqueFluidMutations;
}

World::World()
    : m_generator(Config::WORLD_SEED, WorldType::Normal,
                  DimensionId::Overworld) {}

void World::resetForNewSeed(
    uint64_t newSeed, WorldType worldType, DimensionId dimension) {
    if (m_threadPool) m_threadPool->waitIdle();
    m_meshes.releaseAllMeshes();
    // Flush the streaming cache lane while chunk snapshots are still owned by
    // the store; the next world must not inherit a half-written cache chain.
    m_streamer.clear();
    m_chunks.withUnique([&](ChunkStore& store) {
        store.clearUnlocked();
    });
    m_fluids.clear();
    m_fluidBatchActive = false;
    m_fluidMutations.clear();
    m_uniqueFluidMutations.clear();
    m_fluidLightingPositions.clear();
    m_fluidMutationIndices.clear();
    m_currentFluidTick = 0;
    m_persistence.clear();
    m_simulation.clear();
    m_lighting.reset();
    // Placement-new: WorldGenerator contains reference members (Noise&),
    // so move assignment is deleted. Reconstruct in-place.
    m_generator.~WorldGenerator();
    new (&m_generator) WorldGenerator(newSeed, worldType, dimension);
    Config::WORLD_SEED = newSeed;
}

World::~World() {
    if (m_threadPool) m_threadPool->waitIdle();
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
                if (m_generator.isHeaven()) {
                    // Heaven spawn points must sit on a primary island with
                    // a broad walkable apron.  Sampling a small world-space
                    // square rejects narrow ledges and detached satellites
                    // before the streaming pipeline is started.
                    bool broadIsland = true;
                    for (int dz = -6; dz <= 6 && broadIsland; dz += 3) {
                        for (int dx = -6; dx <= 6; dx += 3) {
                            const SurfaceColumn neighbor =
                                m_generator.sampleTerrainColumn(x + dx, z + dz);
                            if (neighbor.height <= neighbor.waterLevel ||
                                std::abs(neighbor.height - center.height) > 8) {
                                broadIsland = false;
                                break;
                            }
                        }
                    }
                    if (!broadIsland) continue;
                }
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

bool World::placeBed(const glm::ivec3& foot, BedDirection direction) {
    const glm::ivec3 head = foot + bedDirectionOffset(direction);
    if (!Config::isValidWorldY(foot.y) || !Config::isValidWorldY(head.y) ||
        !generatedAt(foot.x, foot.z) || !generatedAt(head.x, head.z) ||
        getBlock(foot.x, foot.y, foot.z) != BlockId::AIR ||
        getBlock(head.x, head.y, head.z) != BlockId::AIR ||
        !isFullCollisionBlock(getBlock(foot.x, foot.y - 1, foot.z)) ||
        !isFullCollisionBlock(getBlock(head.x, head.y - 1, head.z))) {
        return false;
    }
    setBlockInternal(foot.x, foot.y, foot.z,
                     bedBlock(BedPart::Foot, direction), true);
    setBlockInternal(head.x, head.y, head.z,
                     bedBlock(BedPart::Head, direction), true);
    return true;
}

std::optional<glm::ivec3> World::validBedFoot(
    const glm::ivec3& position) const {
    const BlockId selected = getBlock(position.x, position.y, position.z);
    BedPart part = BedPart::Foot;
    BedDirection direction = BedDirection::North;
    if (!decodeBed(selected, part, direction)) return std::nullopt;
    const glm::ivec3 foot = part == BedPart::Foot
        ? position : position - bedDirectionOffset(direction);
    const glm::ivec3 head = foot + bedDirectionOffset(direction);
    if (getBlock(foot.x, foot.y, foot.z) != bedBlock(BedPart::Foot, direction) ||
        getBlock(head.x, head.y, head.z) != bedBlock(BedPart::Head, direction)) {
        return std::nullopt;
    }
    return foot;
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
    // Flowing/falling states are derived simulation output. Even public
    // placement calls must not turn them into persisted overrides; only
    // source states and ordinary player blocks belong in saves.
    if (isDerivedFluidState(id)) recordOverride = false;
    const BlockId previous = getBlock(worldX, worldY, worldZ);
    if (previous == id) return;
    // Replacing fluid with air is still a fluid-surface change. Replacing it
    // with an ordinary block is a player edit and must bypass the fluid mesh
    // merge window immediately.
    const bool fluidMutation = isFluid(id) ||
        (isFluid(previous) && id == BlockId::AIR);

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
    const glm::ivec3 position{worldX, worldY, worldZ};
    if (m_fluidBatchActive) {
        if (fluidMutation) chunk->markFluidMutation(m_currentFluidTick);
        else chunk->markNonFluidMutation();
        m_fluidMutations.push_back({position, previous, id});
    } else {
        if (fluidMutation) chunk->markFluidMutation(m_currentFluidTick);
        else chunk->markNonFluidMutation();
        m_lighting.updateLightingAt(position);
    }

    auto markNeighbor = [&](int neighborX, int neighborZ) {
        Chunk* neighbor = m_chunks.find(neighborX, neighborZ);
        if (neighbor == nullptr) return;
        neighbor->markDirty();
        if (fluidMutation)
            neighbor->markFluidMutation(m_currentFluidTick);
        else
            neighbor->markNonFluidMutation();
    };
    if (lx == 0) markNeighbor(cx - 1, cz);
    if (lx == Config::CHUNK_SIZE_X - 1) markNeighbor(cx + 1, cz);
    if (lz == 0) markNeighbor(cx, cz - 1);
    if (lz == Config::CHUNK_SIZE_Z - 1) markNeighbor(cx, cz + 1);
    if (!m_fluidBatchActive) {
        m_fluids.onBlockChanged(position, previous, id);
        m_fluids.scheduleAround(position);
    }
    if (isBed(previous)) {
        BedPart previousPart = BedPart::Foot;
        BedDirection previousDirection = BedDirection::North;
        decodeBed(previous, previousPart, previousDirection);
        const glm::ivec3 partner = glm::ivec3(worldX, worldY, worldZ) +
                                   bedPartnerOffset(previous);
        const BlockId expected = bedBlock(
            previousPart == BedPart::Foot ? BedPart::Head : BedPart::Foot,
            previousDirection);
        if (getBlock(partner.x, partner.y, partner.z) == expected) {
            setBlockInternal(partner.x, partner.y, partner.z,
                             BlockId::AIR, recordOverride);
        }
    }
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
    const double directionLength = glm::length(glm::dvec3(direction));
    const glm::dvec3 normalizedDirection = directionLength > 1e-12
        ? glm::dvec3(direction) / directionLength : glm::dvec3(0.0);
    const auto hit = voxelRaycast(
        origin, direction, static_cast<double>(maxDistance),
        [this, &origin, &normalizedDirection, maxDistance](
            const glm::ivec3& blockPos) {
            if (!Config::isValidWorldY(blockPos.y)) return false;
            const BlockId id = getBlock(blockPos.x, blockPos.y, blockPos.z);
            if (id == BlockId::AIR) return false;
            const BlockProperties& props = getBlockProps(id);
            if (isBed(id)) {
                return rayIntersectsBlockBounds(
                    origin, normalizedDirection, maxDistance, blockPos,
                    blockCollisionHeight(id));
            }
            return props.solid || props.shape == RenderShape::Cross;
        });
    if (!hit) return std::nullopt;
    return RaycastHit{hit->blockPos, hit->faceNormal};
}
