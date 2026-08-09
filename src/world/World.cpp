#include "world/World.h"
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
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (m_renderer) m_renderer->releaseChunkMesh(chunk->getMesh());
    }
    m_chunks.clear();
    m_activeChunks.clear();
    m_fireAges.clear();
    m_fluidTicks = {};
    m_scheduledFluidDue.clear();
    m_tntIgnitions.clear();
    m_currentWorldTick = 0;
    m_pendingBlocks.clear();
    m_blockOverrides.clear();
    m_dirtyOverrideChunks.clear();
    m_pendingOverrideSaves.clear();
    m_overridesApplied.clear();
    m_blockEntities.clear();
    m_dirtyBlockEntityChunks.clear();
    m_pendingBlockEntitySaves.clear();
    m_blockEntitiesApplied.clear();
    m_lightDirty = true;
    m_lightHasSources = false;
    m_firstUpdate = true;
    m_chunksPerFrame = 16;
    m_streamCenterChunkX = std::numeric_limits<int>::max();
    m_streamCenterChunkZ = std::numeric_limits<int>::max();
    m_streamRenderDistance = -1;
    m_desiredChunks.clear();
    m_desiredChunkSet.clear();
    m_streamCursor = 0;
    m_streamCleanupPending = false;
    ++m_streamingRevision;
    // Placement-new: WorldGenerator contains reference members (Noise&),
    // so move assignment is deleted. Reconstruct in-place.
    m_generator.~WorldGenerator();
    new (&m_generator) WorldGenerator(newSeed);
    Config::WORLD_SEED = newSeed;
}

World::~World() {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (m_renderer) m_renderer->releaseChunkMesh(chunk->getMesh());
    }
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

    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx, cz});
    if (it != m_chunks.end()) {
        return it->second->getBlock(lx, worldY, lz);
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
    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx,cz});
    return it == m_chunks.end() ? LightSample{} :
        unpackLight(it->second->getPackedLight(lx,worldY,lz));
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
    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx, cz});
    if (it == m_chunks.end() || !it->second->generated.load())
        return Config::WORLD_MAX_Y;
    return it->second->getColumnMaxY(lx, lz);
}

bool World::hasSkyAccess(int worldX, int worldY, int worldZ) const {
    return worldY >= getSurfaceY(worldX, worldZ);
}

PrecipitationType World::precipitationAt(
    int worldX, int worldY, int worldZ) const {
    const HeightBiome sample = m_generator.queryHeightBiome(worldX, worldZ);
    return precipitationFor(sample.biome, worldY);
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
        std::unique_lock lock(m_chunkMutex);
        m_blockOverrides[{cx, cz}][localIndex] = id;
        m_dirtyOverrideChunks.insert({cx, cz});
        m_overridesApplied.insert({cx, cz});
        auto& entities = m_blockEntities[{cx, cz}];
        if (id == BlockId::CHEST || id == BlockId::FURNACE) {
            if (entities.count(localIndex) == 0) {
                BlockEntity entity;
                entity.type = id == BlockId::CHEST
                    ? BlockEntityType::Chest : BlockEntityType::Furnace;
                entities.emplace(localIndex, entity);
                m_dirtyBlockEntityChunks.insert({cx, cz});
            }
        } else if (entities.erase(localIndex) != 0) {
            m_dirtyBlockEntityChunks.insert({cx, cz});
        }
    }
    updateLightingAt({worldX,worldY,worldZ});

    if (lx == 0)                   markDirty(cx - 1, cz);
    if (lx == Config::CHUNK_SIZE_X - 1) markDirty(cx + 1, cz);
    if (lz == 0)                   markDirty(cx, cz - 1);
    if (lz == Config::CHUNK_SIZE_Z - 1) markDirty(cx, cz + 1);
    scheduleFluidAround({worldX, worldY, worldZ});
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
    std::shared_lock lock(m_chunkMutex);
    const auto it = m_chunks.find({cx, cz});
    return it != m_chunks.end() && it->second->generated.load();
}

void World::scheduleFluidAround(const glm::ivec3& position, uint64_t minimumDelay) {
    auto schedule = [&](const glm::ivec3& p) {
        if (!Config::isValidWorldY(p.y) || !generatedAt(p.x, p.z)) return;
        const BlockId block = getBlock(p.x, p.y, p.z);
        if (!isFluid(block)) return;
        const uint64_t delay = std::max<uint64_t>(
            minimumDelay, fluidTickDelay(isLava(block)));
        const uint64_t due = m_currentWorldTick + delay;
        const auto existing = m_scheduledFluidDue.find(p);
        if (existing != m_scheduledFluidDue.end() && existing->second <= due) return;
        m_scheduledFluidDue[p] = due;
        m_fluidTicks.push({due, p});
    };
    schedule(position);
    for (const glm::ivec3& offset : FACE_OFFSETS) schedule(position + offset);
}

std::vector<glm::ivec3> World::takeTntIgnitions() {
    std::vector<glm::ivec3> result;
    result.swap(m_tntIgnitions);
    return result;
}

void World::updateFluidCell(const glm::ivec3& p, uint64_t tick) {
    BlockId current = getBlock(p.x, p.y, p.z);
    if (!isFluid(current)) return;
    const bool lava = isLava(current);
    auto same = [&](BlockId block) { return lava ? isLava(block) : isWater(block); };
    auto opposite = [&](BlockId block) { return lava ? isWater(block) : isLava(block); };

    // Contact solidification happens before spreading so update order cannot
    // allow one fluid to overwrite the other.
    if (lava) {
        for (const glm::ivec3& offset : FACE_OFFSETS) {
            if (!isWater(getBlock(p.x + offset.x, p.y + offset.y, p.z + offset.z)))
                continue;
            const BlockId product = offset.y > 0 ? BlockId::STONE :
                (fluidLevel(current) == 0 ? BlockId::OBSIDIAN : BlockId::COBBLESTONE);
            setBlock(p.x, p.y, p.z, product);
            return;
        }
    }

    if (fluidLevel(current) != 0) {
        uint8_t desired = 8;
        const BlockId above = getBlock(p.x, p.y + 1, p.z);
        if (same(above)) {
            desired = 1;
        } else {
            int sourceNeighbors = 0;
            for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS) {
                const BlockId neighbor = getBlock(
                    p.x + offset.x, p.y, p.z + offset.z);
                if (!same(neighbor)) continue;
                if (fluidLevel(neighbor) == 0) ++sourceNeighbors;
                desired = std::min<uint8_t>(desired,
                    nextFluidLevel(lava,fluidLevel(neighbor)));
            }
            const BlockId below = getBlock(p.x,p.y-1,p.z);
            if (!lava && sourceNeighbors >= 2 &&
                (isSolid(below) || (isWater(below) && fluidLevel(below)==0))) desired=0;
        }
        if (desired > 7) {
            setDerivedBlock(p,BlockId::AIR);
            return;
        }
        const BlockId recomputed = fluidBlock(lava, desired);
        if (recomputed != current) {
            // A two-neighbor water source is permanent Minecraft state; unlike
            // ordinary flow depth it must survive removal of its parent sources.
            if(desired==0)setBlock(p.x,p.y,p.z,recomputed);
            else setDerivedBlock(p,recomputed);
            current = recomputed;
        }
    }

    for (const glm::ivec3& offset : FACE_OFFSETS) {
        const glm::ivec3 q = p + offset;
        if (getBlock(q.x, q.y, q.z) != BlockId::TNT) continue;
        if (lava) {
            setBlock(q.x, q.y, q.z, BlockId::AIR);
            m_tntIgnitions.push_back(q);
        }
    }

    const glm::ivec3 below = p + glm::ivec3(0, -1, 0);
    if (Config::isValidWorldY(below.y) && generatedAt(below.x, below.z)) {
        const BlockId target = getBlock(below.x, below.y, below.z);
        if (opposite(target)) {
            setBlock(below.x, below.y, below.z, BlockId::STONE);
            return;
        }
        if (same(target)) {
            if (fluidLevel(target)>1)setDerivedBlock(below,fluidBlock(lava,1));
            return;
        }
        if (isReplaceableByFluid(target)) {
            setDerivedBlock(below,fluidBlock(lava,1));
            return;
        }
    }

    const bool falling=same(getBlock(p.x,p.y+1,p.z));
    const uint8_t spreadLevel=falling?0:fluidLevel(current);
    const uint8_t nextLevel = nextFluidLevel(lava,spreadLevel);
    if (nextLevel > 7) return;
    const FluidSample sample=[this](const glm::ivec3& position){
        return getBlock(position.x,position.y,position.z);};
    const FluidAvailable available=[this](const glm::ivec3& position){
        return Config::isValidWorldY(position.y)&&generatedAt(position.x,position.z);};
    for (const glm::ivec3& offset : preferredFluidDirections(
             p,lava,spreadLevel,sample,available)) {
        const glm::ivec3 q = p + offset;
        const BlockId target = getBlock(q.x, q.y, q.z);
        if (opposite(target)) {
            if (lava) {
                setBlock(p.x,p.y,p.z,fluidLevel(current)==0
                    ? BlockId::OBSIDIAN : BlockId::COBBLESTONE);
            } else {
                setBlock(q.x,q.y,q.z,fluidLevel(target)==0
                    ? BlockId::OBSIDIAN : BlockId::COBBLESTONE);
            }
            continue;
        }
        if (fluidCanOccupy(target,lava,nextLevel))
            setDerivedBlock(q,fluidBlock(lava,nextLevel));
    }
    (void)tick;
}

void World::tickFluids(uint64_t tick) {
    m_currentWorldTick = tick;
    constexpr size_t MAX_UPDATES = 512;
    size_t processed = 0;
    while (!m_fluidTicks.empty() && m_fluidTicks.top().due <= tick &&
           processed < MAX_UPDATES) {
        const ScheduledFluidTick scheduled = m_fluidTicks.top();
        m_fluidTicks.pop();
        const auto current = m_scheduledFluidDue.find(scheduled.position);
        if (current == m_scheduledFluidDue.end() || current->second != scheduled.due)
            continue;
        m_scheduledFluidDue.erase(current);
        if (!generatedAt(scheduled.position.x, scheduled.position.z)) {
            scheduleFluidAround(scheduled.position, 20);
            continue;
        }
        updateFluidCell(scheduled.position, tick);
        ++processed;
    }
}

void World::markDirty(int cx, int cz) {
    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx, cz});
    if (it != m_chunks.end()) {
        it->second->markDirty();
    }
}

// ── Chunk access ──────────────────────────────────────────────────────

Chunk* World::getChunk(int cx, int cz) {
    std::pair<int,int> key{cx, cz};

    {
        std::shared_lock lock(m_chunkMutex);
        auto it = m_chunks.find(key);
        if (it != m_chunks.end()) {
            return it->second.get();
        }
    }

    std::unique_lock lock(m_chunkMutex);
    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) {
        return it->second.get();
    }

    auto chunk = std::make_unique<Chunk>(cx, cz);
    if (m_saveStore) {
        if (auto cached = m_saveStore->loadGeneratedChunk(
                cx, cz, WorldGenContext::CHUNK_CACHE_VERSION)) {
            chunk->loadRawBlocks(*cached);
            chunk->generated = true;
        }
    }
    // Generation is deferred to enqueueGeneration() — the chunk
    // starts as all-AIR and will be populated by a worker thread.
    Chunk* ptr = chunk.get();
    m_chunks[key] = std::move(chunk);
    return ptr;
}

World::GenerationProgress World::generationProgress() const {
    std::shared_lock lock(m_chunkMutex);
    GenerationProgress progress;
    progress.total = m_desiredChunks.size();
    for (const auto& key : m_desiredChunks) {
        const auto it = m_chunks.find(key);
        if (it != m_chunks.end() && it->second->generated.load())
            ++progress.completed;
    }
    return progress;
}

World::GenerationProgress World::loadingProgress() const {
    std::shared_lock lock(m_chunkMutex);
    GenerationProgress progress;
    progress.total = m_desiredChunks.size();
    for (const auto& key : m_desiredChunks) {
        const auto it = m_chunks.find(key);
        if (it == m_chunks.end()) continue;
        const Chunk& chunk = *it->second;
        if (chunk.generated.load() && !chunk.isDirty() &&
            !chunk.meshInProgress.load() && !chunk.meshReady.load()) {
            ++progress.completed;
        }
    }
    return progress;
}

void World::persistGeneratedChunks() {
    if (!m_saveStore) return;
    std::shared_lock lock(m_chunkMutex);
    for (const auto& [key, chunk] : m_chunks) {
        if (!chunk->generated.load()) continue;
        std::vector<uint8_t> blocks(
            chunk->rawBlocks(), chunk->rawBlocks() + Config::CHUNK_VOLUME);
        m_saveStore->saveGeneratedChunk(
            key.first, key.second, blocks, WorldGenContext::CHUNK_CACHE_VERSION);
    }
}

// ── Update (chunk loading/unloading) ──────────────────────────────────

void World::update(const glm::dvec3& playerPos, int loadBudgetOverride) {
    int pcx = worldToChunkX(playerPos.x);
    int pcz = worldToChunkZ(playerPos.z);
    m_centerChunkX = pcx;
    m_centerChunkZ = pcz;

    const bool targetChanged = pcx != m_streamCenterChunkX ||
        pcz != m_streamCenterChunkZ ||
        Config::RENDER_DISTANCE != m_streamRenderDistance;
    if (targetChanged) {
        m_streamCenterChunkX = pcx;
        m_streamCenterChunkZ = pcz;
        m_streamRenderDistance = Config::RENDER_DISTANCE;
        m_desiredChunks.clear();
        m_desiredChunkSet.clear();
        const int r2 = Config::RENDER_DISTANCE * Config::RENDER_DISTANCE;
        for (int dx = -Config::RENDER_DISTANCE; dx <= Config::RENDER_DISTANCE; ++dx) {
            for (int dz = -Config::RENDER_DISTANCE; dz <= Config::RENDER_DISTANCE; ++dz) {
                if (dx * dx + dz * dz > r2) continue;
                const int cx = pcx + dx;
                const int cz = pcz + dz;
                m_desiredChunks.emplace_back(cx, cz);
                m_desiredChunkSet.insert(packedChunkKey(cx, cz));
            }
        }
        std::sort(m_desiredChunks.begin(), m_desiredChunks.end(),
            [pcx, pcz](const auto& a, const auto& b) {
                const int64_t adx = static_cast<int64_t>(a.first) - pcx;
                const int64_t adz = static_cast<int64_t>(a.second) - pcz;
                const int64_t bdx = static_cast<int64_t>(b.first) - pcx;
                const int64_t bdz = static_cast<int64_t>(b.second) - pcz;
                return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
            });
        m_streamCursor = 0;
        m_streamCleanupPending = true;
    }

    bool activeChanged = false;
    if (m_streamCleanupPending) {
        std::unique_lock lock(m_chunkMutex);
        std::vector<std::pair<int,int>> toRemove;
        toRemove.reserve(Config::CHUNK_UNLOADS_PER_FRAME);
        bool cleanupRemaining = false;
        for (auto& [key, chunk] : m_chunks) {
            if (m_desiredChunkSet.count(packedChunkKey(key.first, key.second)) != 0)
                continue;
            if (chunk->meshInProgress.load() || chunk->generationInProgress.load())
                cleanupRemaining = true;
            else if (static_cast<int>(toRemove.size()) <
                     Config::CHUNK_UNLOADS_PER_FRAME)
                toRemove.push_back(key);
            else
                cleanupRemaining = true;
        }
        for (auto& key : toRemove) {
            auto it = m_chunks.find(key);
            if (it != m_chunks.end()) {
                // Retained chunks keep their derived light. Unloading an
                // out-of-range neighbor does not change world blocks, and
                // invalidating the boundary strip here caused a synchronous
                // relight whenever the player crossed a 16-block boundary.
                saveOverrides(key.first, key.second);
                saveBlockEntities(key.first, key.second);
                if (m_renderer)
                    m_renderer->releaseChunkMesh(it->second->getMesh());
                m_chunks.erase(it);
                m_overridesApplied.erase(key);
                m_blockEntities.erase(key);
                m_blockEntitiesApplied.erase(key);
                activeChanged = true;
            }
        }
        m_streamCleanupPending = cleanupRemaining;
    }

    const int loadBudget = loadBudgetOverride > 0 ? loadBudgetOverride :
        (m_firstUpdate ? Config::INITIAL_CHUNK_LOADS_PER_FRAME
                       : Config::CHUNK_LOADS_PER_FRAME);
    m_firstUpdate = false;
    m_chunksPerFrame = Config::CHUNK_LOADS_PER_FRAME;
    int loaded = 0;
    while (m_streamCursor < m_desiredChunks.size() && loaded < loadBudget) {
        const auto key = m_desiredChunks[m_streamCursor++];
        bool exists = false;
        {
            std::shared_lock lock(m_chunkMutex);
            exists = m_chunks.find(key) != m_chunks.end();
        }
        if (!exists) {
            getChunk(key.first, key.second);
            ++loaded;
            activeChanged = true;
        }
    }

    if (activeChanged) {
        std::shared_lock lock(m_chunkMutex);
        m_activeChunks.clear();
        m_activeChunks.reserve(m_chunks.size());
        for (auto& [key, chunk] : m_chunks) {
            m_activeChunks.push_back(chunk.get());
        }
        std::sort(m_activeChunks.begin(), m_activeChunks.end(), [pcx, pcz](
                      const Chunk* a, const Chunk* b) {
            const int64_t adx = static_cast<int64_t>(a->cx) - pcx;
            const int64_t adz = static_cast<int64_t>(a->cz) - pcz;
            const int64_t bdx = static_cast<int64_t>(b->cx) - pcx;
            const int64_t bdz = static_cast<int64_t>(b->cz) - pcz;
            return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
        });
        ++m_streamingRevision;
    }
}

// ── Async chunk generation (region-based) ─────────────────────────────

void World::enqueueGeneration() {
    if (!m_threadPool) return;

    const int taskSlots = Config::CHUNK_GEN_TASKS_IN_FLIGHT -
        m_generationTasksInFlight.load();
    if (taskSlots <= 0) return;

    std::shared_lock lock(m_chunkMutex);

    // Collect all ungenerated, not-in-progress chunk coords
    std::vector<std::pair<int,int>> ungenerated;
    for (auto& [key, chunk] : m_chunks) {
        if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
            ungenerated.push_back(key);
        }
    }
    if (ungenerated.empty()) return;
    std::sort(ungenerated.begin(), ungenerated.end(), [this](const auto& a, const auto& b) {
        const int64_t adx = static_cast<int64_t>(a.first) - m_centerChunkX;
        const int64_t adz = static_cast<int64_t>(a.second) - m_centerChunkZ;
        const int64_t bdx = static_cast<int64_t>(b.first) - m_centerChunkX;
        const int64_t bdz = static_cast<int64_t>(b.second) - m_centerChunkZ;
        return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
    });

    // Build a set for fast lookup
    std::unordered_set<int64_t> available;
    for (auto& [cx, cz] : ungenerated) {
        available.insert((static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz));
    }
    std::unordered_set<int64_t> visited;

    const int R = Config::REGION_SIZE_CHUNKS;  // 3
    const int PADDING = Config::REGION_PADDING;

    // Greedy: try to form R×R regions from ungenerated chunks
    struct RegionTask {
        int originCX, originCZ;
        std::vector<Chunk*> chunks;  // row-major: [lcz * R + lcx]
    };
    std::vector<RegionTask> regions;

    for (auto& [cx, cz] : ungenerated) {
        if (static_cast<int>(regions.size()) >= taskSlots) break;
        int64_t key = (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
        if (visited.count(key)) continue;

        // Check if a full R×R region is available starting at (cx, cz)
        RegionTask region;
        region.originCX = cx;
        region.originCZ = cz;
        bool complete = true;

        for (int dcz = 0; dcz < R && complete; ++dcz) {
            for (int dcx = 0; dcx < R && complete; ++dcx) {
                int64_t nkey = (static_cast<int64_t>(cx + dcx) << 32) | static_cast<uint32_t>(cz + dcz);
                if (!available.count(nkey) || visited.count(nkey)) {
                    complete = false;
                    break;
                }
                // Get the chunk pointer
                auto it = m_chunks.find({cx + dcx, cz + dcz});
                if (it == m_chunks.end()) { complete = false; break; }
                region.chunks.push_back(it->second.get());
            }
        }

        if (complete) {
            // Mark all chunks in region as visited and in-progress
            for (int dcz = 0; dcz < R; ++dcz) {
                for (int dcx = 0; dcx < R; ++dcx) {
                    int64_t nkey = (static_cast<int64_t>(cx + dcx) << 32) | static_cast<uint32_t>(cz + dcz);
                    visited.insert(nkey);
                    auto it = m_chunks.find({cx + dcx, cz + dcz});
                    it->second->generationInProgress = true;
                }
            }
            regions.push_back(std::move(region));
        }
    }

    // Enqueue region tasks
    for (auto& reg : regions) {
        World* worldPtr = this;
        WorldGenerator* genPtr = &m_generator;
        int regionDistance2 = std::numeric_limits<int>::max();
        for (const Chunk* chunk : reg.chunks) {
            const int dx = chunk->cx - m_centerChunkX;
            const int dz = chunk->cz - m_centerChunkZ;
            regionDistance2 = std::min(regionDistance2, dx * dx + dz * dz);
        }

        ++m_generationTasksInFlight;
        m_threadPool->enqueuePriority([worldPtr, genPtr, reg = std::move(reg), R, PADDING]() {
            struct Completion {
                std::atomic<int>& count;
                ~Completion() { --count; }
            } completion{worldPtr->m_generationTasksInFlight};
            // Build RegionGenerator from WorldGenerator's sub-generators
            RegionGenerator regionGen(
                genPtr->getHeightPipeline(),
                genPtr->getCaveGenerator(),
                genPtr->getTreeGenerator(),
                genPtr->getOreGenerator(),
                genPtr->getSeed()
            );

            // Clone chunk pointers (non-const because we need to mutate)
            auto chunks = reg.chunks;

            std::vector<RegionGenerationData::PendingBlock> pendingOut;
            regionGen.generateRegion(reg.originCX, reg.originCZ,
                                     R, PADDING,
                                     chunks, pendingOut);

            // Store pending blocks under the chunk mutex
            {
                std::unique_lock nLock(worldPtr->m_chunkMutex);
                for (auto& pb : pendingOut) {
                    int tcx = World::worldToChunkX(static_cast<double>(pb.worldX));
                    int tcz = World::worldToChunkZ(static_cast<double>(pb.worldZ));
                    worldPtr->m_pendingBlocks[{tcx, tcz}].push_back(pb);
                }
            }
        }, 1000000 - regionDistance2);
    }

    // Remaining ungenerated chunks (not part of any region) — legacy singleton path
    int scheduledTasks = static_cast<int>(regions.size());
    for (auto& [cx, cz] : ungenerated) {
        if (scheduledTasks >= taskSlots) break;
        int64_t key = (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
        if (visited.count(key)) continue;

        auto it = m_chunks.find({cx, cz});
        if (it == m_chunks.end()) continue;
        Chunk* chunkPtr = it->second.get();
        if (chunkPtr->generated.load() || chunkPtr->generationInProgress.load()) continue;

        chunkPtr->generationInProgress = true;
        visited.insert(key);

        WorldGenerator* genPtr = &m_generator;

        // Legacy neighborQuery for singleton chunks
        auto neighborQuery = [this, genPtr](int wx, int wz) -> std::optional<HeightBiome> {
            int ncx = World::worldToChunkX(static_cast<double>(wx));
            int ncz = World::worldToChunkZ(static_cast<double>(wz));
            {
                std::shared_lock nLock(m_chunkMutex);
                auto nit = m_chunks.find({ncx, ncz});
                if (nit == m_chunks.end() || !nit->second->generated.load()) {
                    return std::nullopt;
                }
            }
            return genPtr->queryHeightBiome(wx, wz);
        };

        auto blockSetter = [this](int wx, int wy, int wz, BlockId id) {
            if (!Config::isValidWorldY(wy)) return;
            int bsx = World::worldToChunkX(static_cast<double>(wx));
            int bsz = World::worldToChunkZ(static_cast<double>(wz));
            int lx = wx - bsx * Config::CHUNK_SIZE_X;
            int lz = wz - bsz * Config::CHUNK_SIZE_Z;
            if (lx < 0) { bsx -= 1; lx += Config::CHUNK_SIZE_X; }
            if (lz < 0) { bsz -= 1; lz += Config::CHUNK_SIZE_Z; }
            std::unique_lock nLock(m_chunkMutex);
            auto bit = m_chunks.find({bsx, bsz});
            if (bit != m_chunks.end() && bit->second->generated.load()) {
                bit->second->setBlock(lx, wy, lz, id);
            } else {
                m_pendingBlocks[{bsx, bsz}].push_back({wx, wy, wz, id});
            }
        };

        World* worldPtr = this;
        ++m_generationTasksInFlight;
        ++scheduledTasks;
        const int dx = cx - m_centerChunkX;
        const int dz = cz - m_centerChunkZ;
        const int distance2 = dx * dx + dz * dz;
        m_threadPool->enqueuePriority([worldPtr, chunkPtr, genPtr, neighborQuery, blockSetter]() {
            struct Completion {
                std::atomic<int>& count;
                ~Completion() { --count; }
            } completion{worldPtr->m_generationTasksInFlight};
            genPtr->generate(*chunkPtr, neighborQuery, blockSetter);
            chunkPtr->generated = true;
            chunkPtr->generationInProgress = false;
        }, 1000000 - distance2);
    }
}

void World::processCompletedGenerations(bool rebuildLightingNow) {
    // Apply pending tree leaves for chunks that have finished generating
    std::vector<glm::ivec3> fluidSeeds;
    bool generationStateChanged = false;
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (chunk->generated.load()) {
            const bool firstApply = m_overridesApplied.count(key) == 0;
            applyPendingBlocks(key.first, key.second);
            applySavedOverrides(key.first, key.second);
            loadBlockEntities(key.first, key.second);
            if (firstApply) {
                generationStateChanged = true;
                m_lightDirty = true;
                const auto overrides = m_blockOverrides.find(key);
                if (overrides != m_blockOverrides.end()) {
                    for (const auto& [index, block] : overrides->second) {
                        (void)block;
                        const int x = index % Config::CHUNK_SIZE_X;
                        const int z = (index / Config::CHUNK_SIZE_X) % Config::CHUNK_SIZE_Z;
                        const int y = Config::storageYToWorldY(index /
                            (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z));
                        fluidSeeds.push_back({key.first * Config::CHUNK_SIZE_X + x,
                                              y,
                                              key.second * Config::CHUNK_SIZE_Z + z});
                    }
                }
            }
        }
    }
    if (generationStateChanged) ++m_streamingRevision;
    lock.unlock();
    for (const glm::ivec3& position : fluidSeeds) scheduleFluidAround(position);
    if (rebuildLightingNow && m_lightDirty) rebuildLighting();
}

void World::rebuildLighting() {
    std::queue<BlockLightNode> blockQueue;
    std::queue<BlockLightNode> skyQueue;
    std::unique_lock lock(m_chunkMutex);
    if (!m_lightDirty) return;
    auto findCell=[&](int wx,int y,int wz) -> std::pair<Chunk*,glm::ivec3> {
        if(!Config::isValidWorldY(y)) return {nullptr,glm::ivec3(0)};
        const int cx=worldToChunkX(wx),cz=worldToChunkZ(wz);
        auto it=m_chunks.find({cx,cz});
        if(it==m_chunks.end()||!it->second->generated.load()) return {nullptr,glm::ivec3(0)};
        return {it->second.get(),{wx-cx*Config::CHUNK_SIZE_X,y,wz-cz*Config::CHUNK_SIZE_Z}};
    };

    bool hasSources = false;
    std::vector<Chunk*> initialized;
    std::unordered_set<Chunk*> lightChanged;
    for (auto& [key, chunk] : m_chunks) {
        (void)key;
        if (!chunk->generated.load()||chunk->lightingInitialized.load()) continue;
        chunk->clearLight();
        initialized.push_back(chunk.get());
        lightChanged.insert(chunk.get());
        for (int z=0;z<Config::CHUNK_SIZE_Z;++z) for(int x=0;x<Config::CHUNK_SIZE_X;++x) {
            uint8_t vertical=15;
            for(int y=Config::WORLD_MAX_Y-1;y>=Config::WORLD_MIN_Y;--y) {
                const BlockId block=chunk->getBlock(x,y,z);
                const uint8_t damping=getLightDampening(block);
                if(damping>=15) vertical=0;
                else if(vertical>0 && damping>0)
                    vertical=static_cast<uint8_t>(vertical>damping?vertical-damping:0);
                if(vertical>0) {
                    chunk->setSkyLight(x,y,z,vertical);
                }
                const uint8_t emission=getLightEmission(block);
                if(emission>0) {
                    chunk->setBlockLight(x,y,z,emission);
                    blockQueue.push({chunk->worldX()+x,y,chunk->worldZ()+z,emission});
                    hasSources=true;
                }
            }
        }
        chunk->lightingInitialized=true;
    }

    // Direct columns are already complete.  Seed flood fill only along an
    // exposed horizontal frontier instead of queueing every open-sky voxel.
    constexpr int horizontal[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(Chunk* chunk:initialized){
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
            for(int z=0;z<Config::CHUNK_SIZE_Z;++z)
                for(int x=0;x<Config::CHUNK_SIZE_X;++x){
                    const uint8_t value=chunk->getSkyLight(x,y,z);if(value==0)continue;
                    const int wx=chunk->worldX()+x,wz=chunk->worldZ()+z;
                    bool frontier=false;
                    for(const auto& d:horizontal){auto [neighbor,p]=findCell(wx+d[0],y,wz+d[1]);
                        if(neighbor&&neighbor->getSkyLight(p.x,p.y,p.z)<value){frontier=true;break;}}
                    if(frontier)skyQueue.push({wx,y,wz,value});
                }
    }
    // Existing neighbor borders are also sources for a newly initialized
    // chunk, and vice versa.
    for(Chunk* chunk:initialized)for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
        for(int edge=0;edge<4;++edge)for(int i=0;i<Config::CHUNK_SIZE_X;++i){
            const int x=edge==0?0:edge==1?Config::CHUNK_SIZE_X-1:i;
            const int z=edge==2?0:edge==3?Config::CHUNK_SIZE_Z-1:i;
            const int wx=chunk->worldX()+x,wz=chunk->worldZ()+z;
            const int nx=wx+(edge==0?-1:edge==1?1:0);
            const int nz=wz+(edge==2?-1:edge==3?1:0);
            auto [neighbor,p]=findCell(nx,y,nz);if(!neighbor)continue;
            for(const auto& seed:std::array<std::pair<int,int>,2>{{{wx,wz},{nx,nz}}}){
                auto [source,sp]=findCell(seed.first,y,seed.second);if(!source)continue;
                const uint8_t sky=source->getSkyLight(sp.x,sp.y,sp.z);
                const uint8_t block=source->getBlockLight(sp.x,sp.y,sp.z);
                if(sky)skyQueue.push({seed.first,y,seed.second,sky});
                if(block)blockQueue.push({seed.first,y,seed.second,block});
            }
        }

    constexpr int directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto spread=[&](std::queue<BlockLightNode>& queue,bool sky) {
        while(!queue.empty()) {
            const auto node=queue.front();queue.pop();
            auto [origin,op]=findCell(node.x,node.y,node.z);
            if(!origin)continue;
            const uint8_t current=sky?origin->getSkyLight(op.x,op.y,op.z):
                                      origin->getBlockLight(op.x,op.y,op.z);
            if(current!=node.light||current==0)continue;
            for(const auto& d:directions) {
                const int nx=node.x+d[0],ny=node.y+d[1],nz=node.z+d[2];
                auto [target,p]=findCell(nx,ny,nz);if(!target)continue;
                const uint8_t damping=getLightDampening(target->getBlock(p.x,p.y,p.z));
                if(damping>=15)continue;
                uint8_t loss=static_cast<uint8_t>(std::max<int>(1,damping));
                if(sky&&d[1]==-1&&current==15&&damping==0)loss=0;
                const uint8_t next=current>loss?static_cast<uint8_t>(current-loss):0;
                uint8_t old=sky?target->getSkyLight(p.x,p.y,p.z):target->getBlockLight(p.x,p.y,p.z);
                if(next<=old)continue;
                if(sky)target->setSkyLight(p.x,p.y,p.z,next);
                else target->setBlockLight(p.x,p.y,p.z,next);
                lightChanged.insert(target);
                queue.push({nx,ny,nz,next});
            }
        }
    };
    spread(skyQueue,true);
    spread(blockQueue,false);
    for(Chunk* chunk:lightChanged)chunk->markDirty();
    m_lightHasSources = hasSources;
    m_lightDirty=false;
}

void World::updateLightingAt(const glm::ivec3& position) {
    struct RemovalNode { int x=0,y=0,z=0;uint8_t light=0; };
    std::unique_lock lock(m_chunkMutex);
    if(m_lightDirty)return;
    auto findCell=[&](int wx,int y,int wz)->std::pair<Chunk*,glm::ivec3>{
        if(!Config::isValidWorldY(y))return {nullptr,glm::ivec3(0)};
        const int cx=worldToChunkX(wx),cz=worldToChunkZ(wz);
        auto it=m_chunks.find({cx,cz});
        if(it==m_chunks.end()||!it->second->generated.load())
            return {nullptr,glm::ivec3(0)};
        return {it->second.get(),{wx-cx*Config::CHUNK_SIZE_X,y,
                                  wz-cz*Config::CHUNK_SIZE_Z}};
    };
    std::unordered_set<Chunk*> changed;
    auto get=[&](int x,int y,int z,bool sky){auto [c,p]=findCell(x,y,z);
        return c?(sky?c->getSkyLight(p.x,p.y,p.z):c->getBlockLight(p.x,p.y,p.z)):uint8_t{0};};
    auto set=[&](int x,int y,int z,bool sky,uint8_t value){auto [c,p]=findCell(x,y,z);
        if(!c)return;
        const uint8_t old=sky?c->getSkyLight(p.x,p.y,p.z):c->getBlockLight(p.x,p.y,p.z);
        if(old==value)return;
        if(sky)c->setSkyLight(p.x,p.y,p.z,value);else c->setBlockLight(p.x,p.y,p.z,value);
        changed.insert(c);};
    constexpr int dirs[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto updateChannel=[&](bool sky,std::queue<RemovalNode>& removal,
                           std::queue<BlockLightNode>& addition){
        while(!removal.empty()){
            const auto node=removal.front();removal.pop();
            for(const auto& d:dirs){const int x=node.x+d[0],y=node.y+d[1],z=node.z+d[2];
                const uint8_t neighbor=get(x,y,z,sky);if(neighbor==0)continue;
                const bool descendingFullSky=sky&&d[1]==-1&&node.light==15&&neighbor==15;
                if(neighbor<node.light||descendingFullSky){set(x,y,z,sky,0);removal.push({x,y,z,neighbor});}
                else addition.push({x,y,z,neighbor});
            }
        }
        while(!addition.empty()){
            const auto node=addition.front();addition.pop();
            const uint8_t current=get(node.x,node.y,node.z,sky);
            if(current!=node.light||current==0)continue;
            for(const auto& d:dirs){const int x=node.x+d[0],y=node.y+d[1],z=node.z+d[2];
                auto [target,p]=findCell(x,y,z);if(!target)continue;
                const uint8_t damping=getLightDampening(target->getBlock(p.x,p.y,p.z));
                if(damping>=15)continue;
                uint8_t loss=static_cast<uint8_t>(std::max<int>(1,damping));
                if(sky&&d[1]==-1&&current==15&&damping==0)loss=0;
                const uint8_t next=current>loss?static_cast<uint8_t>(current-loss):0;
                if(next<=get(x,y,z,sky))continue;
                set(x,y,z,sky,next);addition.push({x,y,z,next});
            }
        }
    };

    std::queue<RemovalNode> blockRemoval,skyRemoval;
    std::queue<BlockLightNode> blockAddition,skyAddition;
    const uint8_t oldBlock=get(position.x,position.y,position.z,false);
    const auto [cell,local]=findCell(position.x,position.y,position.z);
    if(!cell)return;
    const uint8_t emission=getLightEmission(cell->getBlock(local.x,local.y,local.z));
    if(oldBlock>emission){set(position.x,position.y,position.z,false,emission);
        blockRemoval.push({position.x,position.y,position.z,oldBlock});}
    else if(emission>oldBlock)set(position.x,position.y,position.z,false,emission);
    if(emission>0)blockAddition.push({position.x,position.y,position.z,emission});
    for(const auto& d:dirs){const int x=position.x+d[0],y=position.y+d[1],z=position.z+d[2];
        const uint8_t value=get(x,y,z,false);if(value)blockAddition.push({x,y,z,value});}
    updateChannel(false,blockRemoval,blockAddition);

    uint8_t vertical=15;
    for(int y=Config::WORLD_MAX_Y-1;y>=Config::WORLD_MIN_Y;--y){
        auto [column,p]=findCell(position.x,y,position.z);if(!column)continue;
        const uint8_t damping=getLightDampening(column->getBlock(p.x,p.y,p.z));
        if(damping>=15)vertical=0;else if(vertical>0&&damping>0)
            vertical=static_cast<uint8_t>(vertical>damping?vertical-damping:0);
        const uint8_t old=get(position.x,y,position.z,true);
        if(old>vertical){set(position.x,y,position.z,true,vertical);
            skyRemoval.push({position.x,y,position.z,old});}
        else if(vertical>old)set(position.x,y,position.z,true,vertical);
        if(vertical>0)skyAddition.push({position.x,y,position.z,vertical});
    }
    for(const auto& d:dirs){const int x=position.x+d[0],y=position.y+d[1],z=position.z+d[2];
        const uint8_t value=get(x,y,z,true);if(value)skyAddition.push({x,y,z,value});}
    updateChannel(true,skyRemoval,skyAddition);
    for(Chunk* chunk:changed){chunk->markDirty();
        if(chunk->cx!=worldToChunkX(position.x)||chunk->cz!=worldToChunkZ(position.z))continue;
        if(local.x==0){auto it=m_chunks.find({chunk->cx-1,chunk->cz});if(it!=m_chunks.end())it->second->markDirty();}
        if(local.x==Config::CHUNK_SIZE_X-1){auto it=m_chunks.find({chunk->cx+1,chunk->cz});if(it!=m_chunks.end())it->second->markDirty();}
        if(local.z==0){auto it=m_chunks.find({chunk->cx,chunk->cz-1});if(it!=m_chunks.end())it->second->markDirty();}
        if(local.z==Config::CHUNK_SIZE_Z-1){auto it=m_chunks.find({chunk->cx,chunk->cz+1});if(it!=m_chunks.end())it->second->markDirty();}
    }
}

void World::applySavedOverrides(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_overridesApplied.count(key) != 0) return;
    auto chunkIt = m_chunks.find(key);
    if (chunkIt == m_chunks.end() || !chunkIt->second->generated.load()) return;

    auto& cached = m_blockOverrides[key];
    size_t pruned=0;
    for (auto it=cached.begin();it!=cached.end();) {
        if (isDerivedFluidState(it->second)){it=cached.erase(it);++pruned;}
        else ++it;
    }
    if (m_saveStore) {
        for (const auto& entry : m_saveStore->loadChunkOverrides(cx, cz)) {
            if (isDerivedFluidState(entry.block)) {++pruned;continue;}
            cached[entry.localIndex] = entry.block;
        }
    }
    if(pruned>0){m_dirtyOverrideChunks.insert(key);LOG_INFO(
        "Pruned "<<pruned<<" legacy derived fluid overrides from chunk "<<cx<<','<<cz);}
    Chunk* chunk = chunkIt->second.get();
    for (const auto& [index, block] : cached) {
        const int x = index % Config::CHUNK_SIZE_X;
        const int z = (index / Config::CHUNK_SIZE_X) % Config::CHUNK_SIZE_Z;
            const int y = Config::storageYToWorldY(
                index / (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z));
        chunk->setBlock(x, y, z, block);
    }
    m_overridesApplied.insert(key);
}

void World::saveOverrides(int cx, int cz) {
    if (!m_saveStore) return;
    const std::pair<int, int> key{cx, cz};
    if (m_dirtyOverrideChunks.count(key) == 0 &&
        m_pendingOverrideSaves.count(key) == 0) return;
    const auto it = m_blockOverrides.find(key);
    if (it == m_blockOverrides.end()) return;
    std::vector<BlockOverride> serialized;
    serialized.reserve(it->second.size());
    for (const auto& [localIndex, block] : it->second) {
        serialized.push_back({localIndex, block});
    }
    std::sort(serialized.begin(), serialized.end(),
              [](const BlockOverride& a, const BlockOverride& b) {
                  return a.localIndex < b.localIndex;
              });
    m_saveStore->saveChunkOverrides(cx, cz, serialized);
    m_dirtyOverrideChunks.erase(key);
    m_pendingOverrideSaves.erase(key);
}

void World::beginModifiedChunkAutosave() {
    std::unique_lock lock(m_chunkMutex);
    m_pendingOverrideSaves.insert(m_dirtyOverrideChunks.begin(),
                                  m_dirtyOverrideChunks.end());
    m_pendingBlockEntitySaves.insert(m_dirtyBlockEntityChunks.begin(),
                                     m_dirtyBlockEntityChunks.end());
    m_dirtyOverrideChunks.clear();
    m_dirtyBlockEntityChunks.clear();
}

bool World::flushModifiedChunks(size_t maxFiles) {
    std::unique_lock lock(m_chunkMutex);
    size_t saved = 0;
    std::vector<std::pair<int, int>> dirty(
        m_pendingOverrideSaves.begin(), m_pendingOverrideSaves.end());
    for (const auto& [cx, cz] : dirty) {
        if (saved >= maxFiles) break;
        saveOverrides(cx, cz);
        ++saved;
    }
    std::vector<std::pair<int, int>> dirtyEntities(
        m_pendingBlockEntitySaves.begin(), m_pendingBlockEntitySaves.end());
    for (const auto& [cx, cz] : dirtyEntities) {
        if (saved >= maxFiles) break;
        saveBlockEntities(cx, cz);
        ++saved;
    }
    return m_pendingOverrideSaves.empty() && m_pendingBlockEntitySaves.empty();
}

namespace {
uint32_t localIndexFor(const glm::ivec3& position, int cx, int cz) {
    const int lx = position.x - cx * Config::CHUNK_SIZE_X;
    const int lz = position.z - cz * Config::CHUNK_SIZE_Z;
    return static_cast<uint32_t>(lx + lz * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(position.y) *
            Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
}
}

BlockEntity* World::getBlockEntity(const glm::ivec3& position) {
    if (!Config::isValidWorldY(position.y)) return nullptr;
    const int cx = worldToChunkX(position.x);
    const int cz = worldToChunkZ(position.z);
    std::unique_lock lock(m_chunkMutex);
    auto chunkIt = m_blockEntities.find({cx, cz});
    if (chunkIt == m_blockEntities.end()) return nullptr;
    auto it = chunkIt->second.find(localIndexFor(position, cx, cz));
    if (it == chunkIt->second.end()) return nullptr;
    m_dirtyBlockEntityChunks.insert({cx, cz});
    return &it->second;
}

const BlockEntity* World::getBlockEntity(const glm::ivec3& position) const {
    if (!Config::isValidWorldY(position.y)) return nullptr;
    const int cx = worldToChunkX(position.x);
    const int cz = worldToChunkZ(position.z);
    std::shared_lock lock(m_chunkMutex);
    auto chunkIt = m_blockEntities.find({cx, cz});
    if (chunkIt == m_blockEntities.end()) return nullptr;
    auto it = chunkIt->second.find(localIndexFor(position, cx, cz));
    return it == chunkIt->second.end() ? nullptr : &it->second;
}

std::vector<ItemStack> World::takeBlockEntityContents(const glm::ivec3& position) {
    std::vector<ItemStack> contents;
    BlockEntity* entity = getBlockEntity(position);
    if (!entity) return contents;
    if (entity->type == BlockEntityType::Chest) {
        for (auto& stack : entity->chest) if (!stack.empty()) contents.push_back(stack);
    } else {
        for (auto* stack : {&entity->input, &entity->fuel, &entity->output})
            if (!stack->empty()) contents.push_back(*stack);
    }
    return contents;
}

void World::loadBlockEntities(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_blockEntitiesApplied.count(key)) return;
    if (m_saveStore) {
        auto& target = m_blockEntities[key];
        for (const auto& persisted : m_saveStore->loadBlockEntities(cx, cz))
            target[persisted.localIndex] = persisted.value;
    }
    m_blockEntitiesApplied.insert(key);
}

void World::saveBlockEntities(int cx, int cz) {
    const std::pair<int,int> key{cx,cz};
    if (!m_saveStore || (m_dirtyBlockEntityChunks.count(key) == 0 &&
                         m_pendingBlockEntitySaves.count(key) == 0)) return;
    std::vector<PersistedBlockEntity> persisted;
    auto it = m_blockEntities.find({cx, cz});
    if (it != m_blockEntities.end()) {
        persisted.reserve(it->second.size());
        for (const auto& [index, entity] : it->second)
            persisted.push_back({index, entity});
        std::sort(persisted.begin(), persisted.end(), [](const auto& a, const auto& b) {
            return a.localIndex < b.localIndex;
        });
    }
    m_saveStore->saveBlockEntities(cx, cz, persisted);
    m_dirtyBlockEntityChunks.erase({cx, cz});
    m_pendingBlockEntitySaves.erase({cx, cz});
}

void World::tickSurvival(const glm::dvec3& playerPosition, uint64_t tick,
                         bool raining) {
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
    {
        std::shared_lock lock(m_chunkMutex);
        for (const auto& [key, overrides] : m_blockOverrides) {
            auto chunkIt = m_chunks.find(key);
            if (chunkIt == m_chunks.end() || !chunkIt->second->generated.load()) continue;
            for (const auto& [index, block] : overrides) {
                if (!isFarmland(block) && !isSapling(block) &&
                    !(block >= BlockId::WHEAT_0 && block < BlockId::WHEAT_7)) continue;
                const int y = index / (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
                const int rem = index % (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
                const int z = rem / Config::CHUNK_SIZE_X;
                const int x = rem % Config::CHUNK_SIZE_X;
                candidates.push_back({{key.first * Config::CHUNK_SIZE_X + x, y,
                                       key.second * Config::CHUNK_SIZE_Z + z}, block});
            }
        }
    }
    for (const auto& candidate : candidates) {
        const glm::ivec3 p = candidate.position;
        if (getBlock(p.x, p.y, p.z) != candidate.block) continue;
        uint64_t random = static_cast<uint64_t>(static_cast<uint32_t>(p.x));
        random = hash(random ^ (static_cast<uint64_t>(static_cast<uint32_t>(p.z)) << 32) ^
                      static_cast<uint64_t>(p.y * 131 + tick * 37));
        if (isFarmland(candidate.block) && random % 20 == 0) {
            const BlockId next=nextFarmlandState(candidate.block,getBlock(p.x,p.y+1,p.z),
                                                  hasWaterForFarmland(p, raining),random);
            if(next!=candidate.block)setBlock(p.x,p.y,p.z,next);
        } else if (candidate.block >= BlockId::WHEAT_0 &&
                   candidate.block < BlockId::WHEAT_7) {
            const BlockId soil = getBlock(p.x, p.y - 1, p.z);
            const BlockId next=nextCropState(candidate.block,soil,random);
            if(next!=candidate.block)setBlock(p.x,p.y,p.z,next);
        } else if (isSapling(candidate.block) && random % 300 == 0) {
            growSapling(p, candidate.block);
        }
    }
}

void World::tickWeather(const WeatherSystem& weather, bool daytime, uint64_t tick) {
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
    for (const Chunk* chunk : m_activeChunks) {
        if (!chunk->generated.load()) continue;
        const uint64_t random = hash(
            Config::WORLD_SEED ^ tick * 131ULL ^
            static_cast<uint64_t>(static_cast<uint32_t>(chunk->cx)) ^
            (static_cast<uint64_t>(static_cast<uint32_t>(chunk->cz)) << 32));
        const int x = chunk->worldX() + static_cast<int>(random % 16);
        const int z = chunk->worldZ() + static_cast<int>((random >> 8) % 16);
        const int surface = getSurfaceY(x, z);
        if (!Config::isValidWorldY(surface)) continue;
        const BlockId top = getBlock(x, surface, z);
        if (weather.raining() &&
            precipitationAt(x, surface + 1, z) == PrecipitationType::Snow) {
            const int layerY = surface + 1;
            if (Config::isValidWorldY(layerY) && isSolid(top) &&
                !isFarmland(top) && top != BlockId::ICE &&
                getBlock(x, layerY, z) == BlockId::AIR &&
                getBlockLight(x, layerY, z) <= 9) {
                setBlock(x, layerY, z, BlockId::SNOW_LAYER);
            }
        } else if (weather.type() == WeatherType::Clear && daytime &&
                   top == BlockId::SNOW_LAYER && random % 2 == 0) {
            setBlock(x, surface, z, BlockId::AIR);
        }
    }

    struct FireCell { glm::ivec3 position; };
    std::vector<FireCell> fires;
    fires.reserve(256);
    {
        std::shared_lock lock(m_chunkMutex);
        for (const auto& [key, overrides] : m_blockOverrides) {
            auto chunkIt = m_chunks.find(key);
            if (chunkIt == m_chunks.end() || !chunkIt->second->generated.load()) continue;
            for (const auto& [index, block] : overrides) {
                if (block != BlockId::FIRE || fires.size() >= 256) continue;
                const int x = index % Config::CHUNK_SIZE_X;
                const int z = (index / Config::CHUNK_SIZE_X) % Config::CHUNK_SIZE_Z;
                const int y = Config::storageYToWorldY(
                    index / (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z));
                fires.push_back({{key.first * Config::CHUNK_SIZE_X + x, y,
                                  key.second * Config::CHUNK_SIZE_Z + z}});
            }
        }
    }

    for (const FireCell& cell : fires) {
        const glm::ivec3 p = cell.position;
        if (getBlock(p.x, p.y, p.z) != BlockId::FIRE) {
            m_fireAges.erase(p);
            continue;
        }
        uint8_t& age = m_fireAges[p];
        const uint64_t random = positionRandom(p, age);
        age = static_cast<uint8_t>(std::min<int>(15, age + random % 3));
        const bool exposedRain = weather.raining() &&
            precipitationAt(p.x, p.y, p.z) == PrecipitationType::Rain &&
            hasSkyAccess(p.x, p.y + 1, p.z);
        if ((exposedRain && random % 100 <
                static_cast<uint64_t>(20 + age * 3)) ||
            (age == 15 && (random >> 9) % 4 == 0)) {
            setBlock(p.x, p.y, p.z, BlockId::AIR);
            m_fireAges.erase(p);
            continue;
        }

        bool hasFuel = false;
        for (size_t direction = 0; direction < FACE_OFFSETS.size(); ++direction) {
            const glm::ivec3 q = p + FACE_OFFSETS[direction];
            const BlockId block = getBlock(q.x, q.y, q.z);
            if (block == BlockId::TNT) {
                setBlock(q.x, q.y, q.z, BlockId::AIR);
                m_tntIgnitions.push_back(q);
                hasFuel = true;
                continue;
            }
            if (!isFlammable(block)) continue;
            hasFuel = true;
            const uint64_t roll = positionRandom(q, direction + age * 17ULL);
            if (roll % 300 < burnOdds(block)) {
                setBlock(q.x, q.y, q.z,
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
                getBlock(q.x, q.y, q.z) != BlockId::AIR) continue;
            uint8_t encouragement = 0;
            for (const glm::ivec3& offset : FACE_OFFSETS) {
                encouragement = std::max(
                    encouragement, fireEncouragement(getBlock(
                        q.x + offset.x, q.y + offset.y, q.z + offset.z)));
            }
            if (encouragement > 0 &&
                (roll >> 24) % (300 + 20 * age) < encouragement)
                setBlock(q.x, q.y, q.z, BlockId::FIRE);
        }

        const bool supported = isSolid(getBlock(p.x, p.y - 1, p.z));
        if (age > 3 && !supported && !hasFuel) {
            setBlock(p.x, p.y, p.z, BlockId::AIR);
            m_fireAges.erase(p);
        }
    }
}

bool World::hasWaterForFarmland(const glm::ivec3& position, bool raining) const {
    if (raining && precipitationAt(position.x, position.y + 1, position.z) ==
                       PrecipitationType::Rain &&
        hasSkyAccess(position.x, position.y + 1, position.z)) return true;
    for (int y = position.y; y <= position.y + 1; ++y)
        for (int x = position.x - 4; x <= position.x + 4; ++x)
            for (int z = position.z - 4; z <= position.z + 4; ++z)
                if (isWater(getBlock(x, y, z))) return true;
    return false;
}

bool World::growSapling(const glm::ivec3& p, BlockId sapling) {
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
        const int cx = worldToChunkX(q.x), cz = worldToChunkZ(q.z);
        {
            std::shared_lock lock(m_chunkMutex);
            auto it = m_chunks.find({cx,cz});
            if (it == m_chunks.end() || !it->second->generated.load()) return false;
        }
        const BlockId current = getBlock(q.x,q.y,q.z);
        const bool replaceable = current == BlockId::AIR || isSapling(current) ||
            current == BlockId::LEAVES || current == BlockId::BIRCH_LEAVES ||
            current == BlockId::SPRUCE_LEAVES || current == BlockId::JUNGLE_LEAVES ||
            current == BlockId::ACACIA_LEAVES;
        if (!replaceable && !(q == p)) return false;
    }
    for (const auto& placement : placements) {
        const BlockId current = getBlock(placement.position.x, placement.position.y, placement.position.z);
        if (placement.trunk || current == BlockId::AIR || isSapling(current))
            setBlock(placement.position.x, placement.position.y, placement.position.z, placement.block);
    }
    return true;
}

void World::tickBlockEntities() {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, entities] : m_blockEntities) {
        bool changed = false;
        for (auto& [index, entity] : entities) {
            changed = tickFurnace(entity) || changed;
        }
        if (changed) m_dirtyBlockEntityChunks.insert(key);
    }
}

void World::applyPendingBlocks(int cx, int cz) {
    auto it = m_pendingBlocks.find({cx, cz});
    if (it == m_pendingBlocks.end()) return;

    auto chunkIt = m_chunks.find({cx, cz});
    if (chunkIt == m_chunks.end() || !chunkIt->second->generated.load()) return;
    Chunk* chunk = chunkIt->second.get();
    for (auto& pb : it->second) {
        int lx = pb.worldX - chunk->worldX();
        int lz = pb.worldZ - chunk->worldZ();
        if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 &&
            Config::isValidWorldY(pb.worldY)) {
            BlockId cur = chunk->getBlock(lx, pb.worldY, lz);
            bool leaf = cur == BlockId::LEAVES || cur == BlockId::BIRCH_LEAVES ||
                        cur == BlockId::SPRUCE_LEAVES || cur == BlockId::JUNGLE_LEAVES ||
                        cur == BlockId::ACACIA_LEAVES;
            if (cur == BlockId::AIR || leaf || cur == BlockId::SNOW) {
                chunk->setBlock(lx, pb.worldY, lz, pb.id);
            }
        }
    }
    m_pendingBlocks.erase(it);
}

void World::waitForInitialGeneration(int maxWaitMs) {
    RuntimeClock clock;const auto start=clock.now();

    while (true) {
        // Check if all chunks are generated
        bool allGenerated = true;
        {
            std::shared_lock lock(m_chunkMutex);
            for (auto& [key, chunk] : m_chunks) {
                if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
                    // Not generated and not being worked on — shouldn't happen
                    // if enqueue was called, but handle gracefully
                    allGenerated = false;
                    break;
                }
                if (chunk->generationInProgress.load()) {
                    allGenerated = false;
                    break;
                }
            }
        }

        if (allGenerated) break;

        const auto elapsed=RuntimeClock::milliseconds(
            RuntimeClock::elapsed(start,clock.now()));
        if (elapsed >= static_cast<uint64_t>(std::max(0,maxWaitMs))) break;

        // Yield to let worker threads run
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

// ── Async mesh building ───────────────────────────────────────────────

void World::enqueueMeshBuilds(int maxInFlight) {
    if (!m_threadPool) return;

    std::shared_lock lock(m_chunkMutex);

    int inFlight = 0;
    for (const auto& [key, chunk] : m_chunks) {
        (void)key;
        if (chunk->meshInProgress.load()) ++inFlight;
    }
    const int availableSlots = std::max(1, maxInFlight) - inFlight;
    if (availableSlots <= 0) return;

    std::vector<Chunk*> candidates;
    candidates.reserve(m_chunks.size());
    for (auto& [key, chunk] : m_chunks) {
        (void)key;
        if (!chunk->isDirty()) continue;
        if (chunk->meshInProgress.load()) continue;
        if (!chunk->generated.load()) continue;  // not generated yet
        candidates.push_back(chunk.get());
    }
    std::sort(candidates.begin(), candidates.end(), [this](const Chunk* a, const Chunk* b) {
        const int64_t adx = static_cast<int64_t>(a->cx) - m_centerChunkX;
        const int64_t adz = static_cast<int64_t>(a->cz) - m_centerChunkZ;
        const int64_t bdx = static_cast<int64_t>(b->cx) - m_centerChunkX;
        const int64_t bdz = static_cast<int64_t>(b->cz) - m_centerChunkZ;
        return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
    });

    const int enqueueCount = std::min({m_chunksPerFrame, availableSlots,
                                       static_cast<int>(candidates.size())});
    for (int i = 0; i < enqueueCount; ++i) {
        Chunk* chunkPtr = candidates[static_cast<size_t>(i)];

        chunkPtr->meshInProgress = true;
        chunkPtr->markClean();  // Mark clean NOW so we don't re-enqueue
        const uint64_t revision=chunkPtr->dataRevision();

        // Capture a raw pointer — the chunk is owned by m_chunks and
        // won't be destroyed while meshInProgress is true
        World* worldPtr = this;
        const int dx = chunkPtr->cx - m_centerChunkX;
        const int dz = chunkPtr->cz - m_centerChunkZ;
        const int distance2 = dx * dx + dz * dz;

        m_threadPool->enqueuePriority([chunkPtr, worldPtr, revision]() {
            // Build mesh into pending buffer
            auto neighborFunc = [worldPtr](int wx, int wy, int wz) -> BlockId {
                return worldPtr->getBlock(wx, wy, wz);
            };
            auto lightFunc = [worldPtr](int wx,int wy,int wz) -> LightSample {
                return worldPtr->getLight(wx,wy,wz);
            };

            chunkPtr->m_pendingMesh.build(
                chunkPtr->worldX(), chunkPtr->worldZ(),
                chunkPtr->rawBlocks(),
                chunkPtr->getColumnMaxYData(),
                neighborFunc, lightFunc
            );
            chunkPtr->pendingMeshRevision=revision;

            // Signal completion
            chunkPtr->meshReady = true;
        }, 500000 - distance2);
    }
}

void World::processCompletedMeshes(IGameRenderer* renderer, int maxUploads,
                                   size_t maxUploadBytes) {
    if (!renderer) return;
    m_renderer = renderer;

    std::shared_lock lock(m_chunkMutex);
    std::vector<Chunk*> ready;
    for (auto& [key, chunk] : m_chunks) {
        (void)key;
        if (chunk->meshReady.load()) ready.push_back(chunk.get());
    }
    std::sort(ready.begin(), ready.end(), [this](const Chunk* a, const Chunk* b) {
        const int64_t adx = static_cast<int64_t>(a->cx) - m_centerChunkX;
        const int64_t adz = static_cast<int64_t>(a->cz) - m_centerChunkZ;
        const int64_t bdx = static_cast<int64_t>(b->cx) - m_centerChunkX;
        const int64_t bdz = static_cast<int64_t>(b->cz) - m_centerChunkZ;
        return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
    });
    const int uploadCount = std::min(maxUploads, static_cast<int>(ready.size()));
    size_t uploadedBytes = 0;
    for (int i = 0; i < uploadCount; ++i) {
        Chunk* chunk = ready[static_cast<size_t>(i)];

        if(chunk->pendingMeshRevision.load()!=chunk->dataRevision()) {
            chunk->meshReady=false;
            chunk->meshInProgress=false;
            chunk->markDirty();
            continue;
        }

        const size_t bytes = chunk->m_pendingMesh.uploadBytes();
        if (uploadedBytes > 0 && uploadedBytes + bytes > maxUploadBytes) break;

        // Swap pending mesh into active
        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            chunk->getMesh().adoptCpuGeometry(chunk->m_pendingMesh);
        }

        // Upload on main thread (GL context)
        renderer->uploadChunkMesh(chunk->getMesh());
        uploadedBytes += bytes;

        chunk->meshReady = false;
        chunk->meshInProgress = false;
    }
}

// ── Synchronous mesh building ─────────────────────────────────────────

void World::buildMeshesSync(IGameRenderer* renderer, int maxCount) {
    if (!renderer) return;
    m_renderer = renderer;

    int built = 0;
    std::shared_lock lock(m_chunkMutex);

    for (auto& [key, chunk] : m_chunks) {
        if (built >= maxCount) break;
        if (!chunk->isDirty()) continue;

        auto neighborFunc = [this](int wx, int wy, int wz) -> BlockId {
            return this->getBlock(wx, wy, wz);
        };
        auto lightFunc = [this](int wx,int wy,int wz) -> LightSample {
            return this->getLight(wx,wy,wz);
        };

        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            ChunkMesh& mesh = chunk->getMesh();
            mesh.build(
                chunk->worldX(), chunk->worldZ(),
                chunk->rawBlocks(),
                chunk->getColumnMaxYData(),
                neighborFunc, lightFunc
            );
            renderer->uploadChunkMesh(mesh);
        }

        chunk->markClean();
        ++built;
    }
}

void World::invalidateGpuMeshes() {
    std::shared_lock lock(m_chunkMutex);
    for (auto& entry : m_chunks) {
        std::lock_guard meshLock(entry.second->getMeshMutex());
        if (m_renderer)
            m_renderer->releaseChunkMesh(entry.second->getMesh());
        else
            entry.second->getMesh().abandonGpuResources();
    }
}

void World::restoreGpuMeshes() {
    std::shared_lock lock(m_chunkMutex);
    for (Chunk* chunk : m_activeChunks) {
        std::lock_guard meshLock(chunk->getMeshMutex());
        if (!chunk->getMesh().empty() && m_renderer)
            m_renderer->uploadChunkMesh(chunk->getMesh());
    }
}

// ── Raycast ───────────────────────────────────────────────────────────

std::optional<World::RaycastHit> World::raycast(const glm::dvec3& origin,
                                                 const glm::vec3& direction,
                                                 float maxDistance) const {
    const glm::dvec3 pos = origin;
    glm::ivec3 blockPos(
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    );

    glm::ivec3 step(
        direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0),
        direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0),
        direction.z > 0.0f ? 1 : (direction.z < 0.0f ? -1 : 0)
    );

    glm::dvec3 absDir = glm::abs(glm::dvec3(direction));
    glm::dvec3 safeAbs = glm::max(absDir, glm::dvec3(1e-10));

    glm::dvec3 tDelta(
        step.x != 0 ? 1.0 / safeAbs.x : INFINITY,
        step.y != 0 ? 1.0 / safeAbs.y : INFINITY,
        step.z != 0 ? 1.0 / safeAbs.z : INFINITY
    );

    glm::dvec3 nextBoundary(
        step.x > 0 ? (blockPos.x + 1) - pos.x : pos.x - blockPos.x,
        step.y > 0 ? (blockPos.y + 1) - pos.y : pos.y - blockPos.y,
        step.z > 0 ? (blockPos.z + 1) - pos.z : pos.z - blockPos.z
    );
    glm::dvec3 tMax = nextBoundary / safeAbs;

    glm::ivec3 lastNormal(0);

    int maxSteps = static_cast<int>(maxDistance * 3);
    for (int i = 0; i < maxSteps; ++i) {
        BlockId id = getBlock(blockPos.x, blockPos.y, blockPos.z);
        if (id != BlockId::AIR) {
            const BlockProperties& props = getBlockProps(id);
            if (props.solid || props.shape == RenderShape::Cross) {
                return RaycastHit{blockPos, lastNormal};
            }
        }

        lastNormal = glm::ivec3(0);

        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            lastNormal.x = -step.x;
            blockPos.x += step.x;
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.x && tMax.y <= tMax.z) {
            lastNormal.y = -step.y;
            blockPos.y += step.y;
            tMax.y += tDelta.y;
        } else {
            lastNormal.z = -step.z;
            blockPos.z += step.z;
            tMax.z += tDelta.z;
        }

        if (!Config::isValidWorldY(blockPos.y)) {
            break;
        }
    }

    return std::nullopt;
}
