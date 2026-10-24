#pragma once

#include "world/Noise.h"
#include "world/NetherGenerator.h"
#ifdef MINECRAFTC_HEAVEN_TEST_QUERIES
#include "plugins/official_heaven/HeavenGenerator.h"
#endif
#include "plugins/Extensions.h"
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
    struct VillageSpawnRequest {
        glm::dvec3 position{0.0};
        uint32_t seed = 0;
    };
#ifdef MINECRAFTC_HEAVEN_TEST_QUERIES
    // Compatibility query types; implementation belongs to the optional provider.
    using HeavenBiome=OfficialHeaven::HeavenGenerator::HeavenBiome;
    using HeavenIslandColumn=OfficialHeaven::HeavenGenerator::HeavenIslandColumn;
    using HeavenLodFeature=OfficialHeaven::HeavenGenerator::HeavenLodFeature;
    using HeavenSkyway=OfficialHeaven::HeavenGenerator::HeavenSkyway;
    static constexpr int HEAVEN_BIOME_COUNT=8,HEAVEN_LAYER_COUNT=5;
#endif
    // Callback for setting blocks outside the current chunk (tree leaves at edges)
    using BlockSetter = std::function<void(int worldX, int worldY, int worldZ, BlockId id)>;
    // Callback for structure blocks outside the current chunk.  Unlike
    // leaves, these must overwrite whatever the target chunk already holds.
    using StructureSetter = std::function<void(
        int worldX, int worldY, int worldZ, BlockId id,
        StructureLootProfile lootProfile, uint64_t lootSeed)>;

    explicit WorldGenerator(uint64_t seed = 1234567890ULL,
                             WorldType worldType = WorldType::Normal,
                             DimensionId dimension = DimensionId::Overworld,
                             Plugins::GenerationConfig config = {});

    // ── Singleton chunk generation (fallback) ────────────────────────────
    void generate(Chunk& chunk,
                  const NeighborQuery& neighborQuery = {},
                  const BlockSetter& blockSetter = {},
                  const StructureSetter& structureSetter = {});

    void generateRegion(int originCX, int originCZ, int regionSizeChunks,
                        int padding, std::vector<Chunk*>& chunks,
                        std::vector<RegionGenerationData::PendingBlock>& pendingOut);

    std::vector<MC_DecorationBlock> pluginModifierBlocks(int x,int z,int width=1,int depth=1,bool includeStructures=true) const;

    std::vector<RegionGenerationData::PendingBlock> pluginStructureBlocks(int,int,int=16,int=16,bool coarse=false) const;
    std::optional<MC_LocatedStructure> locatePluginStructure(const std::string&,int,int,uint32_t=8192) const;
    uint64_t pluginStructureFingerprint() const;
    std::optional<glm::dvec3> sampleDimensionSpawn() const;
    std::vector<MC_GenerationLayer> sampleGenerationLayers(int x,int z) const;
    uint64_t lodSamplingFingerprint() const;
    std::vector<MC_LodSpan> samplePluginLodColumn(int x,int z,int cellSize,
        const std::vector<MC_LodSpan>& base={}) const;
    void generatePluginChunks(const std::vector<Chunk*>& chunks) const;
    // ── Queries ──────────────────────────────────────────────────────────
    int getTerrainHeight(int worldX, int worldZ) const;
    HeightBiome queryHeightBiome(int worldX, int worldZ) const;
    CaveBiome caveBiomeAt(int worldX, int worldY, int worldZ) const {
        return isNether() || customGenerator() || m_worldType == WorldType::Superflat
            ? CaveBiome::Neutral
            : m_caveGenerator.biomeAt(worldX, worldY, worldZ);
    }
    SurfaceColumn sampleTerrainColumn(int worldX, int worldZ) const;
    std::vector<TreeGenerator::TreePlacement> sampleLodTrees(
        int worldOriginX, int worldOriginZ, int width, int depth) const;
    std::vector<VillageSpawnRequest> villageSpawnsForChunk(
        int chunkX, int chunkZ) const;
    bool isNether() const { return m_dimension == DimensionId::Nether; }
    const NetherGenerator& nether() const { return m_nether; }
    WorldType worldType() const { return m_worldType; }
    DimensionId dimension() const { return m_dimension; }
#ifdef MINECRAFTC_HEAVEN_TEST_QUERIES
    bool isHeaven() const { return m_dimension == DimensionId::Heaven; }
    static BlockId heavenSurfaceBlock(HeavenBiome biome);
    std::vector<HeavenLodFeature> sampleHeavenEcologyFeatures(int worldX, int worldZ) const;
    HeavenBiome heavenBiomeAt(int worldX, int worldZ) const;
    std::array<HeavenIslandColumn, HEAVEN_LAYER_COUNT> sampleHeavenLayers(
        int worldX, int worldZ) const;
    std::vector<HeavenLodFeature> sampleHeavenLodFeatures(
        int worldX, int worldZ) const;
    std::optional<LocatedStructure> locateNearestHeavenStructure(
        StructureType type, int worldX, int worldZ,
        int maximumDistance = 8192) const;
    glm::ivec3 heavenSpawnBlock() const;
    HeavenSkyway originHeavenSkyway() const;
    std::optional<HeavenSkyway> heavenSkywayForCell(
        int cellX, int cellZ) const;
    std::optional<glm::dvec3> heavenSkywayDestination(
        const glm::ivec3& core, bool upward) const;
#endif
    uint32_t generationVersion() const {
        return isNether() ? NetherGenerator::VERSION : m_dimension==DimensionId::Heaven ? HEAVEN_GENERATION_VERSION :
                            WorldGenContext::GENERATION_VERSION;
    }
    const Plugins::GenerationConfig& generationConfig() const {return m_pluginConfig;}
    bool customGenerator() const {return !m_pluginConfig.generator.empty();}
    BlockId samplePluginBlock(int x,int y,int z) const;
    std::array<uint16_t,Config::CHUNK_SIZE_Y> samplePluginColumnBlocks(int,int) const;
    uint32_t chunkCacheVersion() const {
        if(isNether())return (NetherGenerator::VERSION<<16)|1u;
        const auto structures=pluginStructureFingerprint();
        if(structures||customGenerator()||!m_pluginConfig.modifiers.empty()) {
            const auto hash=Plugins::generationFingerprint(m_pluginConfig)^structures;
            return static_cast<uint32_t>(hash^(hash>>32));
        }
        return isNether() ? (NetherGenerator::VERSION << 16) | 1u : m_dimension==DimensionId::Heaven ? HEAVEN_CHUNK_CACHE_VERSION :
                            WorldGenContext::CHUNK_CACHE_VERSION;
    }

    static constexpr uint32_t HEAVEN_GENERATION_VERSION = 9;
    static constexpr uint32_t HEAVEN_CHUNK_CACHE_VERSION =
        (HEAVEN_GENERATION_VERSION << 16) | 1u;

    // ── Sub-generator access (for RegionGenerator construction) ──────────
    HeightPipeline& getHeightPipeline() { return m_heightPipeline; }
    CaveGenerator&  getCaveGenerator()  { return m_caveGenerator; }
    TreeGenerator&  getTreeGenerator()  { return m_treeGenerator; }
    OreGenerator&   getOreGenerator()   { return m_oreGenerator; }
    StructureGenerator& getStructureGenerator() { return m_structureGenerator; }
    const StructureGenerator& getStructureGenerator() const {
        return m_structureGenerator;
    }
    uint64_t        getSeed() const     { return m_seed; }

private:
    SurfaceColumn pluginColumn(int,int) const;
    std::optional<NetherStructure> overworldPortalForCell(int,int) const;
    std::vector<RegionGenerationData::PendingBlock> overworldPortalBlocks(int,int,int,int) const;
    std::optional<NetherStructure> locateOverworldPortal(int,int,int) const;
    void generatePluginChunk(Chunk&) const;
    void applyPluginModifiers(Chunk&) const;
    Plugins::GenerationConfig m_pluginConfig;
    uint64_t m_seed;
    WorldType m_worldType = WorldType::Normal;
    DimensionId m_dimension = DimensionId::Overworld;
    NetherGenerator m_nether;
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

#ifdef MINECRAFTC_HEAVEN_TEST_QUERIES
    HeavenIslandColumn sampleHeavenIsland(int worldX, int worldZ) const;
    HeavenIslandColumn sampleHeavenLayer(
        int worldX, int worldZ, int layer) const;
    std::optional<LocatedStructure> heavenStructureForCell(
        StructureType type, int cellX, int cellZ) const;
    glm::ivec3 findHeavenSpawnBlock(int maximumRadius = 512) const;
    HeavenSkyway findOriginSkyway() const;
#endif
};
