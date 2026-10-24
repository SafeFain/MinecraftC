#pragma once

#include "world/Block.h"
#include "world/Structure.h"
#include <cstdint>

enum class NetherStructureKind : uint8_t {
    Fortress, BastionBridge, BastionStable, BastionHousing, BastionTreasure,
    Fossil, RuinedPortal
};

struct NetherStructure {
    NetherStructureKind kind=NetherStructureKind::Fortress;
    glm::ivec3 anchor{0};
    uint64_t seed=0;
    std::optional<BlockId> block(int x,int y,int z) const;
    StructureLootProfile loot() const;
    const char* key() const;
};
