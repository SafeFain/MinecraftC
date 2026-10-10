#include "world/World.h"
#include "world/VoxelRaycast.h"
#include "world/BiomeLocator.h"
#include "core/RuntimeClock.h"
#include "world/ChunkMesh.h"
#include "renderer/GameRenderer.h"
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
                              const BlockCollisionBox& box,
                              double& hitDistance,
                              glm::ivec3& faceNormal) {
    double minimum = 0.0;
    double maximum = maximumDistance;
    glm::ivec3 enteringFace(0);
    const glm::dvec3 boundsMin = glm::dvec3(block) + glm::dvec3(box.min);
    const glm::dvec3 boundsMax = glm::dvec3(block) + glm::dvec3(box.max);
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-12) {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis])
                return false;
            continue;
        }
        double nearDistance = (boundsMin[axis] - origin[axis]) / direction[axis];
        double farDistance = (boundsMax[axis] - origin[axis]) / direction[axis];
        int normalSign = -1;
        if (nearDistance > farDistance) {
            std::swap(nearDistance, farDistance);
            normalSign = 1;
        }
        if (nearDistance > minimum) {
            minimum = nearDistance;
            enteringFace = glm::ivec3(0);
            enteringFace[axis] = normalSign;
        }
        maximum = std::min(maximum, farDistance);
        if (minimum > maximum) return false;
    }
    if (maximum < 0.0 || minimum > maximumDistance) return false;
    hitDistance = minimum;
    faceNormal = enteringFace;
    return true;
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
                  DimensionId::Overworld) {
    m_lod.reset(&m_generator);
}

void World::resetForNewSeed(
    uint64_t newSeed, WorldType worldType, DimensionId dimension) {
    if (m_threadPool) m_threadPool->waitIdle();
    m_meshes.releaseAllMeshes();
    m_lod.reset(nullptr);
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
    m_replicaMode = false;
    m_streamer.setExternalSnapshots(false);
    m_streamer.setRenderDistanceLimit(std::numeric_limits<int>::max());
    m_persistence.clear();
    m_supportDrops.clear();
    m_pendingDoorPower.clear();
    m_interactionSounds.clear();
    m_simulation.clear();
    m_lighting.reset();
    // Placement-new: WorldGenerator contains reference members (Noise&),
    // so move assignment is deleted. Reconstruct in-place.
    m_generator.~WorldGenerator();
    new (&m_generator) WorldGenerator(newSeed, worldType, dimension);
    m_lod.reset(&m_generator);
    Config::WORLD_SEED = newSeed;
}

World::~World() {
    if (m_threadPool) m_threadPool->waitIdle();
    m_meshes.releaseAllMeshes();
    m_lod.releaseGpuMeshes();
}

void World::retireChunkUnlocked(Chunk& chunk, bool keepWarm) {
    const int cx = chunk.cx;
    const int cz = chunk.cz;
    // The streamer holds the chunk-store lock. Persist the live chunk before
    // releasing its GPU mesh or erasing any in-memory save bookkeeping.
    m_persistence.saveOverrides(cx, cz);
    m_persistence.saveBlockEntities(cx, cz);
    m_meshes.releaseChunkMesh(&chunk);
    if (keepWarm) {
        chunk.markDirty();
        chunk.lifecycle = Chunk::LifecycleState::Warm;
        return;
    }
    m_chunks.eraseUnlocked(cx, cz);
    m_persistence.eraseOverridesApplied(cx, cz);
    m_persistence.eraseBlockEntities(cx, cz);
    m_persistence.eraseBlockEntitiesApplied(cx, cz);
}

void World::installLoadedChunkDataUnlocked(
    int cx, int cz, const std::vector<BlockOverride>& overrides,
    const std::vector<PersistedBlockEntity>& entities) {
    m_persistence.installLoadedChunkDataUnlocked(cx, cz, overrides, entities);
}

void World::forEachOverrideInChunkUnlocked(
    int cx, int cz,
    const std::function<void(uint32_t, BlockId)>& visitor) const {
    m_persistence.forEachOverrideInChunkUnlocked(cx, cz, visitor);
}

bool World::overridesAppliedUnlocked(int cx, int cz) const {
    return m_persistence.isOverridesApplied(cx, cz);
}

void World::applySavedChunkDataUnlocked(int cx, int cz) {
    m_persistence.applySavedOverridesUnlocked(cx, cz);
    m_persistence.loadBlockEntities(cx, cz);
}

void World::registerGeneratedBlockEntityUnlocked(
    int cx, int cz, uint32_t localIndex, BlockId id,
    StructureLootProfile lootProfile, uint64_t lootSeed) {
    m_persistence.registerGeneratedBlockEntityUnlocked(
        cx, cz, localIndex, id, lootProfile, lootSeed);
}

void World::markStreamingLightDirty() {
    m_lighting.markDirty();
}

void World::scheduleStreamingFluid(const glm::ivec3& position) {
    m_fluids.scheduleAround(position);
}

void World::rebuildStreamingLightingIfDirty() {
    if (m_lighting.dirty()) m_lighting.rebuild();
}

// ── Block queries ─────────────────────────────────────────────────────

std::optional<BlockId> World::getLoadedBlock(int worldX, int worldY, int worldZ) const {
    if (!Config::isValidWorldY(worldY)) return std::nullopt;
    const int cx = worldToChunkX(static_cast<double>(worldX));
    const int cz = worldToChunkZ(static_cast<double>(worldZ));
    const Chunk* chunk = m_chunks.find(cx,cz);
    if (!chunk || !chunk->generated.load() ||
        chunk->lifecycle.load() == Chunk::LifecycleState::Warm) return std::nullopt;
    return chunk->getBlock(worldX-cx*Config::CHUNK_SIZE_X,worldY,
                           worldZ-cz*Config::CHUNK_SIZE_Z);
}

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

CaveBiome World::caveBiomeAt(int worldX, int worldY, int worldZ) const {
    return m_generator.caveBiomeAt(worldX, worldY, worldZ);
}

int World::heavenBiomePaletteIndex(int worldX, int worldZ) const {
    if (!m_generator.isHeaven()) return 0;
    return static_cast<int>(m_generator.heavenBiomeAt(worldX, worldZ));
}

std::optional<glm::ivec2> World::locateBiome(
    Biome biome, int worldX, int worldZ) const {
    return locateNearestBiome(glm::ivec2(worldX, worldZ), biome,
        [this](int x, int z) { return biomeAt(x, z); });
}

std::optional<glm::ivec3> World::locateStructure(
    StructureType type, int worldX, int worldZ) const {
    if (m_generator.worldType() != WorldType::Normal) return {};
    const auto location = m_generator.isHeaven()
        ? m_generator.locateNearestHeavenStructure(type, worldX, worldZ)
        : m_generator.getStructureGenerator().locateNearest(
              type, worldX, worldZ);
    if (!location) return {};
    return glm::ivec3(location->worldX, location->baseY, location->worldZ);
}

glm::dvec3 World::findSafeSpawn(int maximumRadius) const {
    if (m_generator.isHeaven()) {
        const glm::ivec3 spawn = m_generator.heavenSpawnBlock();
        return {spawn.x + 0.5, spawn.y + 1.01, spawn.z + 0.5};
    }
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

std::optional<glm::dvec3> World::heavenSkywayDestination(
    const glm::ivec3& core, bool upward) const {
    if (getBlock(core.x, core.y, core.z) != BlockId::STAR_CRYSTAL)
        return {};
    const auto destination = m_generator.heavenSkywayDestination(core, upward);
    if (!destination) return {};
    const int x = static_cast<int>(std::floor(destination->x));
    const int y = static_cast<int>(std::floor(destination->y));
    const int z = static_cast<int>(std::floor(destination->z));
    if (!generatedAt(x, z) ||
        !isFullCollisionBlock(getBlock(x, y - 1, z)) ||
        blockCollisionBoxes(getBlock(x, y, z)).count != 0 ||
        blockCollisionBoxes(getBlock(x, y + 1, z)).count != 0)
        return {};
    return destination;
}

bool World::isHeavenSkywayCore(const glm::ivec3& core) const {
    if (getBlock(core.x, core.y, core.z) != BlockId::STAR_CRYSTAL)
        return false;
    return m_generator.heavenSkywayDestination(core, true).has_value() ||
           m_generator.heavenSkywayDestination(core, false).has_value();
}

std::optional<glm::dvec3> World::starstepDestination(
    const glm::dvec3& origin, const glm::vec3& direction,
    float maxDistance) const {
    if (!m_generator.isHeaven()) return {};
    const auto hit = raycast(origin, direction, maxDistance);
    if (!hit) return {};
    std::optional<glm::dvec3> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = hit->blockPos.x + dx;
            const int z = hit->blockPos.z + dz;
            if (!generatedAt(x, z)) continue;
            int top = hit->blockPos.y;
            if (!isFullCollisionBlock(getBlock(x, top, z))) continue;
            while (top + 1 < Config::WORLD_MAX_Y &&
                   isFullCollisionBlock(getBlock(x, top + 1, z))) {
                ++top;
            }
            const int standY = top + 1;
            if (standY + 1 >= Config::WORLD_MAX_Y ||
                blockCollisionBoxes(getBlock(x, standY, z)).count != 0 ||
                blockCollisionBoxes(getBlock(x, standY + 1, z)).count != 0)
                continue;
            const glm::dvec3 candidate{x + 0.5, standY + 0.01, z + 0.5};
            const double distance = glm::distance(origin, candidate);
            if (distance > maxDistance || distance >= bestDistance) continue;
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
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
    if (m_replicaMode || !Config::isValidWorldY(worldY)) return;
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
    if (m_blockMutationCallback)
        m_blockMutationCallback(cx, cz, chunk->blockRevision(), localIndex, id, recordOverride);
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
    DoorState oldDoor, newDoor;
    if (!m_pairMutation && decodeDoor(previous,oldDoor) &&
        (!decodeDoor(id,newDoor) || newDoor.material!=oldDoor.material || newDoor.upper!=oldDoor.upper)) {
        const glm::ivec3 partner=position+glm::ivec3(0,oldDoor.upper?-1:1,0);
        DoorState other;
        if(decodeDoor(getBlock(partner.x,partner.y,partner.z),other) &&
           other.material==oldDoor.material && other.upper!=oldDoor.upper)
            setBlockInternal(partner.x,partner.y,partner.z,BlockId::AIR,recordOverride);
    }
    ButtonState oldButton;
    if(decodeButton(previous,oldButton)) updateButtonPower(position,oldButton);
    if (!m_pairMutation) validateInteractiveNeighbors(position);
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
    double preciseDistance = 0.0;
    glm::ivec3 preciseFace(0);
    bool hasPreciseHit = false;
    const auto hit = voxelRaycast(
        origin, direction, static_cast<double>(maxDistance),
        [this, &origin, &normalizedDirection, maxDistance,
         &preciseDistance, &preciseFace, &hasPreciseHit](
            const glm::ivec3& blockPos) {
            if (!Config::isValidWorldY(blockPos.y)) return false;
            const BlockId id = getBlock(blockPos.x, blockPos.y, blockPos.z);
            if (id == BlockId::AIR) return false;
            const BlockProperties& props = getBlockProps(id);
            if (props.solid || props.shape==RenderShape::Button) {
                const BlockCollisionBoxes boxes = blockSelectionBoxes(id);
                double nearest = maxDistance + 1.0;
                glm::ivec3 nearestFace(0);
                for (uint8_t i = 0; i < boxes.count; ++i) {
                    double candidateDistance = 0.0;
                    glm::ivec3 candidateFace(0);
                    if (rayIntersectsBlockBounds(origin, normalizedDirection,
                            maxDistance, blockPos, boxes.boxes[i],
                            candidateDistance, candidateFace) &&
                        candidateDistance < nearest) {
                        nearest = candidateDistance;
                        nearestFace = candidateFace;
                    }
                }
                if (nearest > maxDistance) return false;
                preciseDistance = nearest;
                preciseFace = nearestFace;
                hasPreciseHit = true;
                return true;
            }
            return props.shape == RenderShape::Cross ||
                   props.shape == RenderShape::CeilingCross;
        });
    if (!hit) return std::nullopt;
    const double distance = hasPreciseHit ? preciseDistance : hit->distance;
    return RaycastHit{hit->blockPos,
                      hasPreciseHit ? preciseFace : hit->faceNormal,
                      origin + normalizedDirection * distance,
                      distance};
}

bool World::supportsFace(const glm::ivec3& p, FaceDir outward) const {
    if(!generatedAt(p.x,p.z)) return false;
    const BlockId support=getBlock(p.x,p.y,p.z);
    if(getBlockProps(support).shape==RenderShape::Door)return false;
    const auto boxes=blockCollisionBoxes(support);
    const glm::ivec3 normal=faceOffset(outward);
    const int axis=normal.x?0:normal.y?1:2, u=(axis+1)%3,v=(axis+2)%3;
    for(uint8_t i=0;i<boxes.count;++i) {
        const auto& box=boxes.boxes[i];
        if(box.min[u]==0 && box.max[u]==1 && box.min[v]==0 && box.max[v]==1 &&
           (normal[axis]>0?box.max[axis]==1:box.min[axis]==0)) return true;
    }
    return false;
}
bool World::placeDoor(const glm::ivec3& p, DoorState state) {
    if(p.y<Config::WORLD_MIN_Y+1 || p.y+1>=Config::WORLD_MAX_Y ||
       !generatedAt(p.x,p.z) || !supportsFace(p-glm::ivec3(0,1,0),FaceDir::TOP) ||
       getBlock(p.x,p.y,p.z)!=BlockId::AIR || getBlock(p.x,p.y+1,p.z)!=BlockId::AIR) return false;
    state.upper=false;
    m_pairMutation=true;
    setBlock(p.x,p.y,p.z,doorBlock(state));
    state.upper=true; setBlock(p.x,p.y+1,p.z,doorBlock(state));
    m_pairMutation=false;
    refreshDoorPower(p);
    return true;
}
bool World::setDoorOpen(const glm::ivec3& target, bool open) {
    DoorState state;
    if(!decodeDoor(getBlock(target.x,target.y,target.z),state) || state.material==DoorMaterial::Iron) return false;
    const glm::ivec3 bottom=target-glm::ivec3(0,state.upper?1:0,0);
    DoorState upper;
    if(!decodeDoor(getBlock(bottom.x,bottom.y+1,bottom.z),upper) || !upper.upper ||
       upper.material!=state.material) return false;
    if(state.open==open) return true;
    state.upper=false;state.open=open;
    m_pairMutation=true;
    setBlock(bottom.x,bottom.y,bottom.z,doorBlock(state));
    state.upper=true;setBlock(bottom.x,bottom.y+1,bottom.z,doorBlock(state));
    m_pairMutation=false;
    m_interactionSounds.push_back({bottom,false,open,false});
    return true;
}
bool World::interactDoor(const glm::ivec3& p) {
    DoorState state;
    if(!decodeDoor(getBlock(p.x,p.y,p.z),state)) return false;
    if(state.material!=DoorMaterial::Iron) setDoorOpen(p,!state.open);
    return true; // iron doors consume use without placing the held item
}
bool World::placeButton(const glm::ivec3& p, ButtonState state) {
    if(!generatedAt(p.x,p.z) || !Config::isValidWorldY(p.y) ||
       getBlock(p.x,p.y,p.z)!=BlockId::AIR || !supportsFace(p-faceOffset(state.attachment),state.attachment)) return false;
    state.pressed=false;setBlock(p.x,p.y,p.z,buttonBlock(state));return true;
}
bool World::activateButton(const glm::ivec3& p) {
    ButtonState state;
    if(!decodeButton(getBlock(p.x,p.y,p.z),state)) return false;
    if(state.pressed) return true;
    state.pressed=true;setBlock(p.x,p.y,p.z,buttonBlock(state));
    if(auto* entity=m_persistence.getBlockEntity(p))
        entity->buttonRemaining=state.material==DoorMaterial::Iron?20:30;
    updateButtonPower(p,state);
    m_interactionSounds.push_back({p,state.material==DoorMaterial::Iron,true,true});
    return true;
}
void World::updateButtonPower(const glm::ivec3& p, const ButtonState& state) {
    const glm::ivec3 support=p-faceOffset(state.attachment);
    for(uint8_t f=0;f<6;++f) {
        const auto offset=faceOffset(static_cast<FaceDir>(f));
        refreshDoorPower(p+offset);
        refreshDoorPower(support+offset);
    }
}
void World::refreshDoorPower(const glm::ivec3& target) {
    if(!generatedAt(target.x,target.z)) {
        m_pendingDoorPower.emplace(target.x,target.y,target.z);return;
    }
    DoorState state;
    if(!decodeDoor(getBlock(target.x,target.y,target.z),state)) return;
    const glm::ivec3 bottom=target-glm::ivec3(0,state.upper?1:0,0);
    bool powered=false, unknown=false;
    for(int half=0;half<2;++half) for(uint8_t f=0;f<6;++f) {
        const glm::ivec3 adjacent=bottom+glm::ivec3(0,half,0)+faceOffset(static_cast<FaceDir>(f));
        if(!generatedAt(adjacent.x,adjacent.z)) {unknown=true;continue;}
        ButtonState button;
        if(decodeButton(getBlock(adjacent.x,adjacent.y,adjacent.z),button) && button.pressed) powered=true;
        const BlockId support=getBlock(adjacent.x,adjacent.y,adjacent.z);
        if(!isFullCollisionBlock(support) || getBlockProps(support).layer!=RenderLayer::Opaque) continue;
        for(uint8_t bf=0;bf<6;++bf) {
            const glm::ivec3 bp=adjacent+faceOffset(static_cast<FaceDir>(bf));
            if(!generatedAt(bp.x,bp.z)) {unknown=true;continue;}
            if(decodeButton(getBlock(bp.x,bp.y,bp.z),button) && button.pressed &&
                bp-faceOffset(button.attachment)==adjacent) powered=true;
        }
    }
    if(unknown) {
        m_pendingDoorPower.emplace(bottom.x,bottom.y,bottom.z);
        if(!powered)return; // Unknown neighbors must not cancel a previously live pulse.
    }
    if(powered==state.powered) return;
    const bool changed=state.open!=powered;
    state.powered=powered;state.open=powered;state.upper=false;
    m_pairMutation=true;
    setBlock(bottom.x,bottom.y,bottom.z,doorBlock(state));
    state.upper=true;setBlock(bottom.x,bottom.y+1,bottom.z,doorBlock(state));
    m_pairMutation=false;
    if(changed) m_interactionSounds.push_back({bottom,state.material==DoorMaterial::Iron,powered,false});
}
void World::validateInteractiveNeighbors(const glm::ivec3& p) {
    for(int i=-1;i<6;++i) {
        const glm::ivec3 q=i<0?p:p+faceOffset(static_cast<FaceDir>(i));
        if(!generatedAt(q.x,q.z)) continue;
        const BlockId id=getBlock(q.x,q.y,q.z);
        DoorState door; ButtonState button;
        bool invalid=false;
        if(decodeDoor(id,door)) {
            const glm::ivec3 bottom=q-glm::ivec3(0,door.upper?1:0,0);
            invalid=!supportsFace(bottom-glm::ivec3(0,1,0),FaceDir::TOP);
        } else if(decodeButton(id,button))
            invalid=!supportsFace(q-faceOffset(button.attachment),button.attachment);
        if(invalid) {
            if(gameRules().boolean(GameRuleId::BlockDrops)) m_supportDrops.push_back({q,{itemForBlock(id),1,0}});
            setBlock(q.x,q.y,q.z,BlockId::AIR);
        }
        if(decodeDoor(id,door)) refreshDoorPower(q);
    }
}
void World::tickInteractiveBlocks(const std::function<bool(const glm::ivec3&)>& arrowPresent) {
    std::vector<glm::ivec3> ready;
    for(auto it=m_pendingDoorPower.begin();it!=m_pendingDoorPower.end() && ready.size()<Config::BUTTON_TRANSITIONS_PER_TICK;) {
        const auto [x,y,z]=*it;
        if(generatedAt(x,z)) {ready.emplace_back(x,y,z);it=m_pendingDoorPower.erase(it);}
        else ++it;
    }
    for(const auto& p:ready)refreshDoorPower(p);
    for(const glm::ivec3& p:m_persistence.tickButtons()) {
        ButtonState button;
        if(!decodeButton(getBlock(p.x,p.y,p.z),button)) continue;
        if(button.material!=DoorMaterial::Iron && arrowPresent(p)) {
            if(auto* entity=m_persistence.getBlockEntity(p)) entity->buttonRemaining=30;
            continue;
        }
        button.pressed=false;setBlock(p.x,p.y,p.z,buttonBlock(button));
        updateButtonPower(p,button);
        m_interactionSounds.push_back({p,button.material==DoorMaterial::Iron,false,true});
    }
}
void World::activateButtonsAtArrow(const glm::dvec3& arrow) {
    const glm::ivec3 cell(glm::floor(arrow));
    for(int y=-1;y<=1;++y) for(int z=-1;z<=1;++z) for(int x=-1;x<=1;++x) {
        const glm::ivec3 p=cell+glm::ivec3(x,y,z);
        if(!generatedAt(p.x,p.z)) continue;
        ButtonState state;
        if(!decodeButton(getBlock(p.x,p.y,p.z),state) || state.material==DoorMaterial::Iron) continue;
        state.pressed=false;
        const auto box=blockSelectionBoxes(buttonBlock(state)).boxes[0];
        const glm::dvec3 local=arrow-glm::dvec3(p);
        if(glm::all(glm::greaterThanEqual(local,glm::dvec3(box.min)-.03)) &&
           glm::all(glm::lessThanEqual(local,glm::dvec3(box.max)+.03))) activateButton(p);
    }
}
