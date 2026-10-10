#include "world/World.h"
#include <algorithm>

void World::setReplicaMode(bool enabled) {
    m_replicaMode = enabled;
    m_streamer.setExternalSnapshots(enabled);
    m_streamer.setRenderDistanceLimit(enabled ? 16 : std::numeric_limits<int>::max());
}

bool World::installReplicaSnapshot(int cx, int cz, const std::vector<uint16_t>& blocks) {
    if (!m_replicaMode || blocks.size() != Config::CHUNK_VOLUME) return false;
    for (auto id : blocks)
        if (!isValidBlockId(static_cast<BlockId>(id))) return false;
    Chunk* chunk = m_chunks.find(cx, cz);
    if (!chunk || chunk->lifecycle.load() == Chunk::LifecycleState::Warm) return false;
    std::vector<glm::ivec3> changed;
    const bool existing = chunk->generated.load();
    if (existing) {
        m_lighting.rebuild();
        std::vector<uint16_t> previous;
        chunk->copyRawBlocks(previous);
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (previous[i] == blocks[i]) continue;
            const int x = static_cast<int>(i % 16), z = static_cast<int>((i / 16) % 16);
            const int y = static_cast<int>(i / 256) + Config::WORLD_MIN_Y;
            changed.push_back({cx * 16 + x, y, cz * 16 + z});
        }
    }
    chunk->loadRawBlocks(blocks);
    chunk->generated = true;
    chunk->cacheChecked = true;
    chunk->lifecycle = Chunk::LifecycleState::WaitingForMesh;
    if (!existing) {
        chunk->lightingInitialized = false;
        m_lighting.markDirty();
    } else m_lighting.updateLightingBatch(changed);
    for (const auto& offset : ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS) {
        if (auto* neighbor = m_chunks.find(cx + offset[0], cz + offset[1])) neighbor->markDirty();
    }
    return true;
}

bool World::installReplicaEdits(int cx, int cz, const std::vector<std::pair<uint32_t, BlockId>>& edits) {
    if (!m_replicaMode || edits.empty() || edits.size() > 4096) return false;
    Chunk* chunk = m_chunks.find(cx, cz);
    if (!chunk || !chunk->generated.load() || chunk->lifecycle.load() == Chunk::LifecycleState::Warm) return false;
    std::set<uint32_t> indices;
    for (const auto& edit : edits)
        if (edit.first >= Config::CHUNK_VOLUME || !isValidBlockId(edit.second) ||
            !indices.insert(edit.first).second) return false;
    m_lighting.rebuild();
    std::vector<glm::ivec3> changed;
    for (const auto& edit : edits) {
        const int x = static_cast<int>(edit.first % 16), z = static_cast<int>((edit.first / 16) % 16);
        const int y = static_cast<int>(edit.first / 256) + Config::WORLD_MIN_Y;
        chunk->setBlock(x, y, z, edit.second);
        changed.push_back({cx * 16 + x, y, cz * 16 + z});
        for (const auto& offset : ChunkMesh::NEIGHBOR_DEPENDENCY_OFFSETS)
            if (auto* neighbor = m_chunks.find(cx + offset[0], cz + offset[1])) neighbor->markDirty();
    }
    m_lighting.updateLightingBatch(changed);
    return true;
}
