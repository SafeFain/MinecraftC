#pragma once

#include "entity/EntityRecord.h"
#include "world/Block.h"
#include "world/BlockEntity.h"

#include <cstdint>
#include <optional>
#include <vector>

struct BlockOverride {
    uint32_t localIndex = 0;
    BlockId block = BlockId::AIR;
};

struct ChunkEntityLoadData {
    std::vector<EntityRecord> entities;
    uint32_t populationVersion = 0;
};

// One asynchronous read unit for a resident chunk. The generated terrain is
// optional because a cache miss must fall back to deterministic generation;
// the remaining vectors may be empty when the corresponding partition has
// never been written.
struct ChunkLoadBundle {
    std::optional<std::vector<uint16_t>> generated;
    std::vector<BlockOverride> overrides;
    std::vector<PersistedBlockEntity> blockEntities;
    std::vector<EntityRecord> entities;
    uint32_t entityPopulationVersion = 0;
};
