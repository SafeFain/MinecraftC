#include "world/WorldPersistence.h"

#include "Config.h"
#include "debug/Log.h"
#include "game/SaveStore.h"
#include "world/BlockEntityLogic.h"
#include "world/Chunk.h"
#include "world/ChunkStore.h"
#include "world/FluidLogic.h"

#include <algorithm>

namespace {
uint32_t localIndexFor(const glm::ivec3& position, int cx, int cz) {
    const int lx = position.x - cx * Config::CHUNK_SIZE_X;
    const int lz = position.z - cz * Config::CHUNK_SIZE_Z;
    return static_cast<uint32_t>(lx + lz * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(position.y) *
            Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
}
int worldToChunkX(double wx) {
    return static_cast<int>(std::floor(wx / Config::CHUNK_SIZE_X));
}
int worldToChunkZ(double wz) {
    return static_cast<int>(std::floor(wz / Config::CHUNK_SIZE_Z));
}
}  // namespace

void WorldPersistence::forEachOverride(const OverrideVisitor& fn) const {
    m_chunks.withShared([&](ChunkStore& store) {
        for (const auto& [key, overrides] : m_blockOverrides) {
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr || !chunk->generated.load()) continue;
            for (const auto& [index, block] : overrides) {
                fn(key, index, block);
            }
        }
    });
}

void WorldPersistence::registerGeneratedBlockEntityUnlocked(
    int cx, int cz, uint32_t localIndex, BlockId id) {
    if (id != BlockId::CHEST && id != BlockId::FURNACE) return;
    auto& entities = m_blockEntities[{cx, cz}];
    if (entities.count(localIndex) != 0) return;
    BlockEntity entity;
    entity.type = id == BlockId::CHEST ? BlockEntityType::Chest
                                       : BlockEntityType::Furnace;
    entities.emplace(localIndex, entity);
    m_dirtyBlockEntityChunks.insert({cx, cz});
}

void WorldPersistence::recordOverride(int cx, int cz, uint32_t localIndex,
                                      BlockId id) {
    m_chunks.withUnique([&](ChunkStore&) {
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
    });
}

void WorldPersistence::installLoadedChunkDataUnlocked(
    int cx, int cz, const std::vector<BlockOverride>& overrides,
    const std::vector<PersistedBlockEntity>& entities) {
    const std::pair<int, int> key{cx, cz};
    auto& cached = m_blockOverrides[key];
    for (const auto& entry : overrides) {
        if (entry.localIndex >= static_cast<uint32_t>(Config::CHUNK_VOLUME) ||
            static_cast<uint8_t>(entry.block) >= static_cast<uint8_t>(BlockId::COUNT) ||
            isDerivedFluidState(entry.block)) continue;
        cached[entry.localIndex] = entry.block;
    }
    auto& target = m_blockEntities[key];
    for (const auto& entity : entities) target[entity.localIndex] = entity.value;
    m_prefetchedChunks.insert(key);
}

void WorldPersistence::applySavedOverridesUnlocked(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_overridesApplied.count(key) != 0) return;
    Chunk* chunk = m_chunks.findUnlocked(cx, cz);
    if (chunk == nullptr || !chunk->generated.load()) return;

    auto& cached = m_blockOverrides[key];
    size_t pruned=0;
    for (auto it=cached.begin();it!=cached.end();) {
        if (isDerivedFluidState(it->second)){it=cached.erase(it);++pruned;}
        else ++it;
    }
    if (m_saveStore && m_prefetchedChunks.count(key) == 0) {
        for (const auto& entry : m_saveStore->loadChunkOverrides(cx, cz)) {
            if (isDerivedFluidState(entry.block)) {++pruned;continue;}
            cached[entry.localIndex] = entry.block;
        }
    }
    if(pruned>0){m_dirtyOverrideChunks.insert(key);LOG_INFO(
        "Pruned "<<pruned<<" legacy derived fluid overrides from chunk "<<cx<<','<<cz);}
    for (const auto& [index, block] : cached) {
        int x = 0, z = 0, y = 0;
        decodeChunkIndex(index, x, z, y);
        chunk->setBlock(x, y, z, block);
    }
    m_overridesApplied.insert(key);
}

void WorldPersistence::saveOverrides(int cx, int cz) {
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

void WorldPersistence::saveBlockEntities(int cx, int cz) {
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

void WorldPersistence::loadBlockEntities(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_blockEntitiesApplied.count(key)) return;
    if (m_saveStore && m_prefetchedChunks.count(key) == 0) {
        auto& target = m_blockEntities[key];
        for (const auto& persisted : m_saveStore->loadBlockEntities(cx, cz))
            target[persisted.localIndex] = persisted.value;
    }
    m_blockEntitiesApplied.insert(key);
}

void WorldPersistence::beginModifiedChunkAutosave() {
    m_chunks.withUnique([&](ChunkStore&) {
        m_pendingOverrideSaves.insert(m_dirtyOverrideChunks.begin(),
                                      m_dirtyOverrideChunks.end());
        m_pendingBlockEntitySaves.insert(m_dirtyBlockEntityChunks.begin(),
                                         m_dirtyBlockEntityChunks.end());
        m_dirtyOverrideChunks.clear();
        m_dirtyBlockEntityChunks.clear();
    });
}

bool WorldPersistence::flushModifiedChunks(size_t maxFiles) {
    bool done = false;
    m_chunks.withUnique([&](ChunkStore&) {
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
        done = m_pendingOverrideSaves.empty() &&
               m_pendingBlockEntitySaves.empty();
    });
    return done;
}

BlockEntity* WorldPersistence::getBlockEntity(const glm::ivec3& position) {
    if (!Config::isValidWorldY(position.y)) return nullptr;
    const int cx = worldToChunkX(position.x);
    const int cz = worldToChunkZ(position.z);
    BlockEntity* result = nullptr;
    m_chunks.withUnique([&](ChunkStore&) {
        auto chunkIt = m_blockEntities.find({cx, cz});
        if (chunkIt == m_blockEntities.end()) return;
        auto it = chunkIt->second.find(localIndexFor(position, cx, cz));
        if (it == chunkIt->second.end()) return;
        m_dirtyBlockEntityChunks.insert({cx, cz});
        result = &it->second;
    });
    return result;
}

const BlockEntity* WorldPersistence::getBlockEntity(
    const glm::ivec3& position) const {
    if (!Config::isValidWorldY(position.y)) return nullptr;
    const int cx = worldToChunkX(position.x);
    const int cz = worldToChunkZ(position.z);
    const BlockEntity* result = nullptr;
    m_chunks.withShared([&](ChunkStore&) {
        auto chunkIt = m_blockEntities.find({cx, cz});
        if (chunkIt == m_blockEntities.end()) return;
        auto it = chunkIt->second.find(localIndexFor(position, cx, cz));
        if (it == chunkIt->second.end()) return;
        result = &it->second;
    });
    return result;
}

std::vector<ItemStack> WorldPersistence::takeBlockEntityContents(
    const glm::ivec3& position) {
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

void WorldPersistence::tickBlockEntities() {
    m_chunks.withUnique([&](ChunkStore&) {
        for (auto& [key, entities] : m_blockEntities) {
            bool changed = false;
            for (auto& [index, entity] : entities) {
                changed = tickFurnace(entity) || changed;
            }
            if (changed) m_dirtyBlockEntityChunks.insert(key);
        }
    });
}
