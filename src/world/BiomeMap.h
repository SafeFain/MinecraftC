#pragma once

#include <cstdint>

#include "world/Biome.h"

// ── Biome enum ──────────────────────────────────────────────────────────

// ── Tree type (for vegetation generation) ───────────────────────────────

enum class TreeType : uint8_t {
    NONE,
    OAK,
    BIRCH,
    SPRUCE,
    JUNGLE,
    ACACIA,
    SWAMP_OAK,
    CACTUS
};

// ── Biome properties ────────────────────────────────────────────────────

#include "world/Block.h"

struct BiomeProperties {
    BlockId   surfaceBlock  = BlockId::GRASS;
    BlockId   subsoilBlock  = BlockId::DIRT;
    // Reserved: these three fields are not wired into generation. Terrain
    // height comes from HeightPipeline and tree density from TreeGenerator.
    float     heightMul     = 0.5f;
    float     heightOffset  = 5.0f;
    float     treeDensity   = 0.05f;
    TreeType  treeType1     = TreeType::OAK;
    TreeType  treeType2     = TreeType::NONE;
    int       waterLevel    = 63;      // sea-level override per biome (for swamps etc.)
    int       snowLine       = 999;    // height above which surface becomes SNOW (999=never)
};

// Global biome property table
extern const BiomeProperties BIOME_TABLE[BIOME_COUNT];

inline const BiomeProperties& getBiomeProps(Biome b) {
    return BIOME_TABLE[static_cast<uint8_t>(b)];
}
