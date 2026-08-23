#pragma once

#include "world/RegionGenerationData.h"
#include "world/StructureGenerator.h"
#include "world/Chunk.h"
#include <vector>
#include <cstdint>

class HeightPipeline;
class CaveGenerator;
class TreeGenerator;
class OreGenerator;

// ── Region Generator ─────────────────────────────────────────────────────
// Generates an N×N chunk region as an atomic unit. Within the region, all
// cross-chunk operations (biome smoothing, river flow, cave generation,
// tree spacing) are perfectly continuous — no more chunk-boundary seams.
//
// Pipeline: precomputeColumns → precomputeCaves → placeStructures →
//            placeTrees → populateChunks → placeTreeBlocks →
//            placeStructureBlocks → finalizeChunks
//
class RegionGenerator {
public:
    RegionGenerator(HeightPipeline& hp, CaveGenerator& cg,
                    TreeGenerator& tg, OreGenerator& og,
                    uint64_t seed);

    // Generate an entire region. chunks must be regionSizeChunks^2 pointers
    // in row-major order: chunks[localCZ * regionSizeChunks + localCX].
    // pendingOut receives leaf and structure blocks that belong outside this
    // region.
    void generateRegion(int originCX, int originCZ,
                        int regionSizeChunks, int padding,
                        std::vector<Chunk*>& chunks,
                        std::vector<RegionGenerationData::PendingBlock>& pendingOut);

    // Access the pre-computed region data (for boundary queries)
    const RegionGenerationData& getRegionData() const { return m_regionData; }

private:
    HeightPipeline& m_heightPipeline;
    CaveGenerator&  m_caveGenerator;
    TreeGenerator&  m_treeGenerator;
    OreGenerator&   m_oreGenerator;
    uint64_t        m_seed;
    StructureGenerator m_structureGenerator;

    RegionGenerationData m_regionData;

    // Number of chunks along one edge of the region
    int m_regionSizeChunks = 3;

    std::vector<StructurePlacement> m_structures;

    // ── Pipeline phases ─────────────────────────────────────────────────
    void precomputeColumns();
    void precomputeCaves();
    void placeStructuresRegion();
    void placeTreesRegion();
    void populateChunk(Chunk& chunk, int localCX, int localCZ);
    void placeStructures(std::vector<Chunk*>& chunks,
                         std::vector<RegionGenerationData::PendingBlock>& pendingOut);
    void finalizeChunks(std::vector<Chunk*>& chunks);
};
