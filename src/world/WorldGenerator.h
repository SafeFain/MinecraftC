#pragma once

#include "world/Noise.h"
#include "world/BiomeMap.h"
#include "world/HeightPipeline.h"
#include "world/CaveGenerator.h"
#include "world/TreeGenerator.h"
#include "world/OreGenerator.h"
#include "world/StructureGenerator.h"
#include "world/Chunk.h"
#include "world/RegionGenerationData.h"
#include "game/GameRules.h"
#include "Config.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// ── World Generator ─────────────────────────────────────────────────────
// Orchestrates the terrain generation pipeline.
//
// Two generation paths:
//   Region-based (primary):  this class creates a RegionGenerator (which
//     takes references to the sub-generators owned here), and region
//     generation handles N×N chunks atomically with perfect continuity.
//   Singleton fallback:      generate() for individual chunks that can't
//     form a full region. Uses NeighborQuery/BlockSetter for cross-chunk
//     access (legacy behavior).
//
class WorldGenerator {
public:
    enum class HeavenBiome : uint8_t {
        DawnMeadow,
        SkyrootGrove,
        SunstoneHeights,
        StarCrystalGarden,
        CloudbloomFields,
        SkystoneBarrens,
        GlimmerFen,
        MoonpearlTerrace
    };
    static constexpr int HEAVEN_BIOME_COUNT = 8;
    static constexpr int HEAVEN_LAYER_COUNT = 5;

    struct HeavenIslandColumn {
        bool present = false;
        HeavenBiome biome = HeavenBiome::DawnMeadow;
        int top = Config::WORLD_MIN_Y - 1;
        int bottom = Config::WORLD_MIN_Y;
    };
    // Callback for setting blocks outside the current chunk (tree leaves at edges)
    using BlockSetter = std::function<void(int worldX, int worldY, int worldZ, BlockId id)>;
    // Callback for structure blocks outside the current chunk.  Unlike
    // leaves, these must overwrite whatever the target chunk already holds.
    using StructureSetter = std::function<void(int worldX, int worldY, int worldZ, BlockId id)>;

    explicit WorldGenerator(uint64_t seed = 1234567890ULL,
                             WorldType worldType = WorldType::Normal,
                             DimensionId dimension = DimensionId::Overworld);

    // ── Singleton chunk generation (fallback) ────────────────────────────
    void generate(Chunk& chunk,
                  const NeighborQuery& neighborQuery = {},
                  const BlockSetter& blockSetter = {},
                  const StructureSetter& structureSetter = {});

    void generateRegion(int originCX, int originCZ, int regionSizeChunks,
                        int padding, std::vector<Chunk*>& chunks,
                        std::vector<RegionGenerationData::PendingBlock>& pendingOut);

    // ── Queries ──────────────────────────────────────────────────────────
    int getTerrainHeight(int worldX, int worldZ) const;
    HeightBiome queryHeightBiome(int worldX, int worldZ) const;
    SurfaceColumn sampleTerrainColumn(int worldX, int worldZ) const;
    WorldType worldType() const { return m_worldType; }
    DimensionId dimension() const { return m_dimension; }
    bool isHeaven() const { return m_dimension == DimensionId::Heaven; }
    HeavenBiome heavenBiomeAt(int worldX, int worldZ) const;
    std::array<HeavenIslandColumn, HEAVEN_LAYER_COUNT> sampleHeavenLayers(
        int worldX, int worldZ) const;
    uint32_t generationVersion() const {
        return isHeaven() ? HEAVEN_GENERATION_VERSION :
                            WorldGenContext::GENERATION_VERSION;
    }
    uint32_t chunkCacheVersion() const {
        return isHeaven() ? HEAVEN_CHUNK_CACHE_VERSION :
                            WorldGenContext::CHUNK_CACHE_VERSION;
    }

    static constexpr uint32_t HEAVEN_GENERATION_VERSION = 6;
    static constexpr uint32_t HEAVEN_CHUNK_CACHE_VERSION =
        (HEAVEN_GENERATION_VERSION << 16) | 1u;

    // ── Sub-generator access (for RegionGenerator construction) ──────────
    HeightPipeline& getHeightPipeline() { return m_heightPipeline; }
    CaveGenerator&  getCaveGenerator()  { return m_caveGenerator; }
    TreeGenerator&  getTreeGenerator()  { return m_treeGenerator; }
    OreGenerator&   getOreGenerator()   { return m_oreGenerator; }
    StructureGenerator& getStructureGenerator() { return m_structureGenerator; }
    uint64_t        getSeed() const     { return m_seed; }

private:
    uint64_t m_seed;
    WorldType m_worldType = WorldType::Normal;
    DimensionId m_dimension = DimensionId::Overworld;
    Noise          m_noise;
    HeightPipeline m_heightPipeline;
    CaveGenerator  m_caveGenerator;
    TreeGenerator  m_treeGenerator;
    OreGenerator   m_oreGenerator;
    StructureGenerator m_structureGenerator;

    // Tree placement helper — handles all tree types
    void placeTree(Chunk& chunk, int localX, int baseY, int localZ,
                   TreeType type, int trunkHeight,
                   const BlockSetter& blockSetter = {},
                   int chunkWorldX = 0, int chunkWorldZ = 0);

    // Place a simple trunk column (used by all tree types)
    static void placeTrunk(Chunk& chunk, int x, int baseY, int z,
                           int trunkHeight, TreeType type);

    static SurfaceColumn superflatColumn();
    static void populateSuperflat(Chunk& chunk);

    HeavenIslandColumn sampleHeavenIsland(int worldX, int worldZ) const;
    HeavenIslandColumn sampleHeavenLayer(
        int worldX, int worldZ, int layer) const;
    static void populateHeaven(Chunk& chunk, WorldGenerator& generator);
};
