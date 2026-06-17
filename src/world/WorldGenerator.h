#pragma once

#include "world/Noise.h"
#include "world/BiomeMap.h"
#include "world/HeightPipeline.h"
#include "world/CaveGenerator.h"
#include "world/TreeGenerator.h"
#include "world/OreGenerator.h"
#include "world/Chunk.h"
#include "world/RegionGenerationData.h"
#include <cstdint>
#include <functional>
#include <vector>

// ── World Generator ─────────────────────────────────────────────────────
// Orchestrates the terrain generation pipeline.
//
// Two generation paths:
//   Region-based (primary):  World creates a RegionGenerator (which takes
//     references to the sub-generators owned here), and region generation
//     handles N×N chunks atomically with perfect cross-chunk continuity.
//   Singleton fallback:      generate() for individual chunks that can't
//     form a full region. Uses NeighborQuery/BlockSetter for cross-chunk
//     access (legacy behavior).
//
class WorldGenerator {
public:
    // Callback for setting blocks outside the current chunk (tree leaves at edges)
    using BlockSetter = std::function<void(int worldX, int worldY, int worldZ, BlockId id)>;

    explicit WorldGenerator(uint64_t seed = 1234567890ULL);

    // ── Singleton chunk generation (fallback) ────────────────────────────
    void generate(Chunk& chunk,
                  const NeighborQuery& neighborQuery = {},
                  const BlockSetter& blockSetter = {});

    // ── Queries ──────────────────────────────────────────────────────────
    int getTerrainHeight(int worldX, int worldZ) const;
    HeightBiome queryHeightBiome(int worldX, int worldZ) const;

    // ── Sub-generator access (for RegionGenerator construction) ──────────
    HeightPipeline& getHeightPipeline() { return m_heightPipeline; }
    CaveGenerator&  getCaveGenerator()  { return m_caveGenerator; }
    TreeGenerator&  getTreeGenerator()  { return m_treeGenerator; }
    OreGenerator&   getOreGenerator()   { return m_oreGenerator; }
    uint64_t        getSeed() const     { return m_seed; }

private:
    uint64_t m_seed;
    Noise          m_noise;
    HeightPipeline m_heightPipeline;
    CaveGenerator  m_caveGenerator;
    TreeGenerator  m_treeGenerator;
    OreGenerator   m_oreGenerator;

    // Tree placement helper — handles all tree types
    void placeTree(Chunk& chunk, int localX, int baseY, int localZ,
                   TreeType type, int trunkHeight,
                   const BlockSetter& blockSetter = {},
                   int chunkWorldX = 0, int chunkWorldZ = 0);

    // Place a simple trunk column (used by all tree types)
    static void placeTrunk(Chunk& chunk, int x, int baseY, int z, int trunkHeight);
};
