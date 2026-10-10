#pragma once

#include "network/Protocol.h"
#include "entity/EntitySnapshot.h"

namespace Lan {
constexpr size_t MAX_VISIBLE_ENTITIES = 512;
constexpr size_t MAX_VISIBLE_DEATHS = 128;
struct EntityBatch {
    DimensionId dimension = DimensionId::Overworld;
    uint64_t epoch = 1, tick = 0;
    std::vector<EntitySnapshot> entities;
    std::vector<EntityDeathSnapshot> deaths;
};
Bytes encodeEntityBatch(const EntityBatch& batch);
EntityBatch decodeEntityBatch(const Bytes& bytes);
}
