#include "world/World.h"
#include "world/ChunkMesh.h"
#include "world/RegionGenerator.h"
#include "renderer/Renderer.h"
#include "threading/ThreadPool.h"
#include "debug/Log.h"
#include "Config.h"
#include "game/SaveStore.h"
#include "game/SurvivalRules.h"
#include "world/BlockLightLogic.h"

#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>
#include <unordered_set>

World::World() : m_generator(Config::WORLD_SEED) {}

void World::resetForNewSeed(uint64_t newSeed) {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        chunk->getMesh().destroy();
    }
    m_chunks.clear();
    m_activeChunks.clear();
    m_pendingBlocks.clear();
    m_blockOverrides.clear();
    m_dirtyOverrideChunks.clear();
    m_overridesApplied.clear();
    m_blockEntities.clear();
    m_dirtyBlockEntityChunks.clear();
    m_blockEntitiesApplied.clear();
    m_lightDirty = true;
    m_firstUpdate = true;
    m_chunksPerFrame = 16;
    // Placement-new: WorldGenerator contains reference members (Noise&),
    // so move assignment is deleted. Reconstruct in-place.
    m_generator.~WorldGenerator();
    new (&m_generator) WorldGenerator(newSeed);
    Config::WORLD_SEED = newSeed;
}

World::~World() {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        chunk->getMesh().destroy();
    }
}

// ── Block queries ─────────────────────────────────────────────────────

BlockId World::getBlock(int worldX, int worldY, int worldZ) const {
    if (worldY < 0 || worldY >= Config::CHUNK_SIZE_Y) {
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
    if (worldY < 0 || worldY >= Config::CHUNK_SIZE_Y) return 0;
    const int cx = worldToChunkX(worldX), cz = worldToChunkZ(worldZ);
    const int lx = worldX - cx * Config::CHUNK_SIZE_X;
    const int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    std::shared_lock lock(m_chunkMutex);
    auto it = m_chunks.find({cx,cz});
    return it == m_chunks.end() ? 0 : it->second->getBlockLight(lx,worldY,lz);
}

void World::setBlock(int worldX, int worldY, int worldZ, BlockId id) {
    if (worldY < 0 || worldY >= Config::CHUNK_SIZE_Y) return;

    int cx = worldToChunkX(static_cast<double>(worldX));
    int cz = worldToChunkZ(static_cast<double>(worldZ));

    int lx = worldX - cx * Config::CHUNK_SIZE_X;
    int lz = worldZ - cz * Config::CHUNK_SIZE_Z;
    if (lx < 0) { cx -= 1; lx += Config::CHUNK_SIZE_X; }
    if (lz < 0) { cz -= 1; lz += Config::CHUNK_SIZE_Z; }

    Chunk* chunk = getChunk(cx, cz);
    chunk->setBlock(lx, worldY, lz, id);
    const uint16_t localIndex = static_cast<uint16_t>(
        lx + lz * Config::CHUNK_SIZE_X +
        worldY * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
    {
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
    m_lightDirty = true;

    if (lx == 0)                   markDirty(cx - 1, cz);
    if (lx == Config::CHUNK_SIZE_X - 1) markDirty(cx + 1, cz);
    if (lz == 0)                   markDirty(cx, cz - 1);
    if (lz == Config::CHUNK_SIZE_Z - 1) markDirty(cx, cz + 1);
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
    // Generation is deferred to enqueueGeneration() — the chunk
    // starts as all-AIR and will be populated by a worker thread.
    Chunk* ptr = chunk.get();
    m_chunks[key] = std::move(chunk);
    return ptr;
}

// ── Update (chunk loading/unloading) ──────────────────────────────────

void World::update(const glm::dvec3& playerPos) {
    int pcx = worldToChunkX(playerPos.x);
    int pcz = worldToChunkZ(playerPos.z);

    // First frame: load more chunks
    if (m_firstUpdate) {
        m_chunksPerFrame = 16;
        m_firstUpdate = false;
    } else {
        m_chunksPerFrame = 4;
    }

    // Compute needed chunks
    std::vector<std::pair<int,int>> needed;
    int r2 = Config::RENDER_DISTANCE * Config::RENDER_DISTANCE;
    for (int dx = -Config::RENDER_DISTANCE; dx <= Config::RENDER_DISTANCE; ++dx) {
        for (int dz = -Config::RENDER_DISTANCE; dz <= Config::RENDER_DISTANCE; ++dz) {
            if (dx * dx + dz * dz <= r2) {
                needed.emplace_back(pcx + dx, pcz + dz);
            }
        }
    }

    // Remove out-of-range chunks — O(1) lookup via hash set
    {
        std::unique_lock lock(m_chunkMutex);

        // Build lookup set from needed coords for O(1) membership test
        std::unordered_set<int64_t> neededSet;
        neededSet.reserve(needed.size());
        for (auto& [cx, cz] : needed) {
            neededSet.insert((static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz));
        }

        std::vector<std::pair<int,int>> toRemove;
        for (auto& [key, chunk] : m_chunks) {
            int64_t k64 = (static_cast<int64_t>(key.first) << 32) | static_cast<uint32_t>(key.second);
            if (neededSet.find(k64) == neededSet.end() && !chunk->meshInProgress.load()
                && !chunk->generationInProgress.load()) {
                toRemove.push_back(key);
            }
        }
        for (auto& key : toRemove) {
            auto it = m_chunks.find(key);
            if (it != m_chunks.end()) {
                saveOverrides(key.first, key.second);
                saveBlockEntities(key.first, key.second);
                it->second->getMesh().destroy();
                m_chunks.erase(it);
                m_overridesApplied.erase(key);
                m_blockEntities.erase(key);
                m_blockEntitiesApplied.erase(key);
            }
        }
    }

    // Load/generate new chunks
    for (auto& key : needed) {
        bool exists = false;
        {
            std::shared_lock lock(m_chunkMutex);
            exists = m_chunks.find(key) != m_chunks.end();
        }
        if (!exists) {
            getChunk(key.first, key.second);
        }
    }

    // Rebuild active list
    {
        std::shared_lock lock(m_chunkMutex);
        m_activeChunks.clear();
        m_activeChunks.reserve(m_chunks.size());
        for (auto& [key, chunk] : m_chunks) {
            m_activeChunks.push_back(chunk.get());
        }
    }
}

// ── Async chunk generation (region-based) ─────────────────────────────

void World::enqueueGeneration() {
    if (!m_threadPool) return;

    std::shared_lock lock(m_chunkMutex);

    // Collect all ungenerated, not-in-progress chunk coords
    std::vector<std::pair<int,int>> ungenerated;
    for (auto& [key, chunk] : m_chunks) {
        if (!chunk->generated.load() && !chunk->generationInProgress.load()) {
            ungenerated.push_back(key);
        }
    }
    if (ungenerated.empty()) return;

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

        m_threadPool->enqueuePriority([worldPtr, genPtr, reg = std::move(reg), R, PADDING]() {
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
        }, 1);
    }

    // Remaining ungenerated chunks (not part of any region) — legacy singleton path
    for (auto& [cx, cz] : ungenerated) {
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
            if (wy < 0 || wy >= Config::CHUNK_SIZE_Y) return;
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

        m_threadPool->enqueuePriority([chunkPtr, genPtr, neighborQuery, blockSetter]() {
            genPtr->generate(*chunkPtr, neighborQuery, blockSetter);
            chunkPtr->generated = true;
            chunkPtr->generationInProgress = false;
        }, 1);
    }
}

void World::processCompletedGenerations() {
    // Apply pending tree leaves for chunks that have finished generating
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (chunk->generated.load()) {
            const bool firstApply = m_overridesApplied.count(key) == 0;
            applyPendingBlocks(key.first, key.second);
            applySavedOverrides(key.first, key.second);
            loadBlockEntities(key.first, key.second);
            if (firstApply) m_lightDirty = true;
        }
    }
    lock.unlock();
    if (m_lightDirty) rebuildBlockLight();
}

void World::rebuildBlockLight() {
    std::queue<BlockLightNode> queue;
    std::unique_lock lock(m_chunkMutex);
    if (!m_lightDirty) return;
    for (auto& [key, chunk] : m_chunks) {
        if (!chunk->generated.load()) continue;
        chunk->clearBlockLight();
        for (int y=0;y<Config::CHUNK_SIZE_Y;++y)
            for (int z=0;z<Config::CHUNK_SIZE_Z;++z)
                for (int x=0;x<Config::CHUNK_SIZE_X;++x)
                    if (chunk->getBlock(x,y,z)==BlockId::TORCH) {
                        chunk->setBlockLight(x,y,z,14);
                        queue.push({chunk->worldX()+x,y,chunk->worldZ()+z,14});
                    }
    }
    auto findCell=[&](int wx,int y,int wz) -> std::pair<Chunk*,glm::ivec3> {
        if(y<0||y>=Config::CHUNK_SIZE_Y) return {nullptr,glm::ivec3(0)};
        const int cx=worldToChunkX(wx),cz=worldToChunkZ(wz);
        auto it=m_chunks.find({cx,cz});
        if(it==m_chunks.end()||!it->second->generated.load()) return {nullptr,glm::ivec3(0)};
        return {it->second.get(),{wx-cx*Config::CHUNK_SIZE_X,y,wz-cz*Config::CHUNK_SIZE_Z}};
    };
    propagateBlockLight(queue,
        [&](int x,int y,int z){ auto [chunk,p]=findCell(x,y,z);if(!chunk)return false;
            const BlockId block=chunk->getBlock(p.x,p.y,p.z);
            return block==BlockId::AIR||getBlockProps(block).transparent; },
        [&](int x,int y,int z){ auto [chunk,p]=findCell(x,y,z);
            return chunk?chunk->getBlockLight(p.x,p.y,p.z):uint8_t{0}; },
        [&](int x,int y,int z,uint8_t value){ auto [chunk,p]=findCell(x,y,z);
            if(chunk)chunk->setBlockLight(p.x,p.y,p.z,value); });
    for (auto& [key,chunk]:m_chunks) if (chunk->generated.load()) chunk->markDirty();
    m_lightDirty=false;
}

void World::applySavedOverrides(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_overridesApplied.count(key) != 0) return;
    auto chunkIt = m_chunks.find(key);
    if (chunkIt == m_chunks.end() || !chunkIt->second->generated.load()) return;

    auto& cached = m_blockOverrides[key];
    if (m_saveStore) {
        for (const auto& entry : m_saveStore->loadChunkOverrides(cx, cz)) {
            cached[entry.localIndex] = entry.block;
        }
    }
    Chunk* chunk = chunkIt->second.get();
    for (const auto& [index, block] : cached) {
        const int x = index % Config::CHUNK_SIZE_X;
        const int z = (index / Config::CHUNK_SIZE_X) % Config::CHUNK_SIZE_Z;
        const int y = index / (Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
        chunk->setBlock(x, y, z, block);
    }
    m_overridesApplied.insert(key);
}

void World::saveOverrides(int cx, int cz) {
    if (!m_saveStore) return;
    const std::pair<int, int> key{cx, cz};
    if (m_dirtyOverrideChunks.count(key) == 0) return;
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
}

void World::flushModifiedChunks() {
    std::unique_lock lock(m_chunkMutex);
    std::vector<std::pair<int, int>> dirty(
        m_dirtyOverrideChunks.begin(), m_dirtyOverrideChunks.end());
    for (const auto& [cx, cz] : dirty) saveOverrides(cx, cz);
    std::vector<std::pair<int, int>> dirtyEntities(
        m_dirtyBlockEntityChunks.begin(), m_dirtyBlockEntityChunks.end());
    for (const auto& [cx, cz] : dirtyEntities) saveBlockEntities(cx, cz);
}

namespace {
uint16_t localIndexFor(const glm::ivec3& position, int cx, int cz) {
    const int lx = position.x - cx * Config::CHUNK_SIZE_X;
    const int lz = position.z - cz * Config::CHUNK_SIZE_Z;
    return static_cast<uint16_t>(lx + lz * Config::CHUNK_SIZE_X +
        position.y * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
}
}

BlockEntity* World::getBlockEntity(const glm::ivec3& position) {
    if (position.y < 0 || position.y >= Config::CHUNK_SIZE_Y) return nullptr;
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
    if (position.y < 0 || position.y >= Config::CHUNK_SIZE_Y) return nullptr;
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
    if (!m_saveStore || m_dirtyBlockEntityChunks.count({cx, cz}) == 0) return;
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
}

void World::tickSurvival(const glm::dvec3& playerPosition, uint64_t tick) {
    auto hash = [](uint64_t value) {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    };
    for (uint64_t sample = 0; sample < 32; ++sample) {
        const uint64_t random = hash(tick * 37 + sample);
        const int x = static_cast<int>(std::floor(playerPosition.x)) +
                      static_cast<int>(random % 33) - 16;
        const int z = static_cast<int>(std::floor(playerPosition.z)) +
                      static_cast<int>((random >> 16) % 33) - 16;
        const int y = static_cast<int>((random >> 32) % Config::CHUNK_SIZE_Y);
        const BlockId block = getBlock(x, y, z);
        if (block >= BlockId::WHEAT_0 && block < BlockId::WHEAT_7 &&
            getBlock(x, y - 1, z) == BlockId::FARMLAND) {
            setBlock(x, y, z, static_cast<BlockId>(
                static_cast<uint8_t>(block) + 1));
        }
    }
}

void World::tickBlockEntities() {
    std::unique_lock lock(m_chunkMutex);
    for (auto& [key, entities] : m_blockEntities) {
        bool changed = false;
        for (auto& [index, entity] : entities) {
            if (entity.type != BlockEntityType::Furnace) continue;
            const SmeltingRecipe* recipe = entity.input.empty()
                ? nullptr : findSmeltingRecipe(entity.input.id);
            const bool outputFits = recipe && (entity.output.empty() ||
                (entity.output.id == recipe->output.id &&
                 entity.output.count + recipe->output.count <=
                     getItemProps(entity.output.id).maxStack));
            if (entity.burnRemaining == 0 && outputFits && !entity.fuel.empty()) {
                const uint16_t burn = fuelTicks(entity.fuel.id);
                if (burn != 0) {
                    entity.burnRemaining = entity.burnTotal = burn;
                    if (--entity.fuel.count == 0) entity.fuel.clear();
                    changed = true;
                }
            }
            if (entity.burnRemaining > 0) { --entity.burnRemaining; changed = true; }
            if (entity.burnRemaining > 0 && outputFits) {
                entity.cookTotal = recipe->cookTicks;
                if (++entity.cookProgress >= entity.cookTotal) {
                    entity.cookProgress = 0;
                    if (--entity.input.count == 0) entity.input.clear();
                    if (entity.output.empty()) entity.output = recipe->output;
                    else entity.output.count += recipe->output.count;
                }
                changed = true;
            } else if (entity.cookProgress != 0) {
                entity.cookProgress = 0;
                changed = true;
            }
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
            pb.worldY >= 0 && pb.worldY < Config::CHUNK_SIZE_Y) {
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
    using namespace std::chrono;
    auto start = high_resolution_clock::now();

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

        auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
        if (elapsed >= maxWaitMs) break;

        // Yield to let worker threads run
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

// ── Async mesh building ───────────────────────────────────────────────

void World::enqueueMeshBuilds() {
    if (!m_threadPool) return;

    int enqueued = 0;
    std::shared_lock lock(m_chunkMutex);

    for (auto& [key, chunk] : m_chunks) {
        if (enqueued >= m_chunksPerFrame) break;
        if (!chunk->isDirty()) continue;
        if (chunk->meshInProgress.load()) continue;
        if (!chunk->generated.load()) continue;  // not generated yet

        chunk->meshInProgress = true;
        chunk->markClean();  // Mark clean NOW so we don't re-enqueue

        // Capture a raw pointer — the chunk is owned by m_chunks and
        // won't be destroyed while meshInProgress is true
        Chunk* chunkPtr = chunk.get();
        World* worldPtr = this;

        m_threadPool->enqueuePriority([chunkPtr, worldPtr]() {
            // Build mesh into pending buffer
            auto neighborFunc = [worldPtr](int wx, int wy, int wz) -> BlockId {
                return worldPtr->getBlock(wx, wy, wz);
            };
            auto lightFunc = [worldPtr](int wx,int wy,int wz) -> uint8_t {
                return worldPtr->getBlockLight(wx,wy,wz);
            };

            chunkPtr->m_pendingMesh.build(
                chunkPtr->worldX(), chunkPtr->worldZ(),
                &chunkPtr->blockAt(0, 0, 0),
                chunkPtr->getColumnMaxYData(),
                neighborFunc, lightFunc
            );

            // Signal completion
            chunkPtr->meshReady = true;
        }, 0);

        ++enqueued;
    }
}

void World::processCompletedMeshes(Renderer* renderer, int maxUploads) {
    if (!renderer) return;

    int uploaded = 0;
    std::shared_lock lock(m_chunkMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (uploaded >= maxUploads) break;
        if (!chunk->meshReady.load()) continue;

        // Swap pending mesh into active
        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            chunk->getMesh().adoptCpuGeometry(chunk->m_pendingMesh);
        }

        // Upload on main thread (GL context)
        chunk->getMesh().upload();

        chunk->meshReady = false;
        chunk->meshInProgress = false;
        ++uploaded;
    }
}

// ── Synchronous mesh building ─────────────────────────────────────────

void World::buildMeshesSync(Renderer* renderer, int maxCount) {
    if (!renderer) return;

    int built = 0;
    std::shared_lock lock(m_chunkMutex);

    for (auto& [key, chunk] : m_chunks) {
        if (built >= maxCount) break;
        if (!chunk->isDirty()) continue;

        auto neighborFunc = [this](int wx, int wy, int wz) -> BlockId {
            return this->getBlock(wx, wy, wz);
        };
        auto lightFunc = [this](int wx,int wy,int wz) -> uint8_t {
            return this->getBlockLight(wx,wy,wz);
        };

        {
            std::lock_guard meshLock(chunk->getMeshMutex());
            ChunkMesh& mesh = chunk->getMesh();
            mesh.build(
                chunk->worldX(), chunk->worldZ(),
                &chunk->blockAt(0, 0, 0),
                chunk->getColumnMaxYData(),
                neighborFunc, lightFunc
            );
            mesh.upload();
        }

        chunk->markClean();
        ++built;
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

        if (blockPos.y < 0 || blockPos.y >= Config::CHUNK_SIZE_Y) {
            break;
        }
    }

    return std::nullopt;
}
