#pragma once

#include "world/BiomeMap.h"
#include "world/RegionGenerationData.h"
#include <cstdint>
#include <functional>
#include <optional>

class Noise;

// ── Height-Biome query result (cross-chunk neighbor lookups) ──────────

struct HeightBiome {
    int height;
    Biome biome;
};

// A neighbor query returns nullopt if the chunk at that position
// doesn't exist or hasn't been generated yet. Used for cross-chunk
// smoothing and river detection.
using NeighborQuery = std::function<std::optional<HeightBiome>(int worldX, int worldZ)>;

// ── Height Pipeline ─────────────────────────────────────────────────────
// Computes height, biome, and river maps for terrain generation.
//
// Two modes:
//   Padded region mode (new):  computePaddedRegion() pre-computes a full
//     padded grid so all smoothing/flow operations are self-contained.
//     computeChunkFromRegion() then trivially slices 16×16 chunks from it.
//   Legacy per-chunk mode:     compute() fills a single chunk's 16×16 maps,
//     using NeighborQuery for cross-chunk access. Retained as fallback
//     for singleton chunks that can't form a full region.
//
class HeightPipeline {
public:
    HeightPipeline(const Noise& noise, uint64_t seed);

    // ── Padded region mode (primary path) ───────────────────────────────
    // Fills a RegionGenerationData's columns array with height/biome/river
    // for the full padded grid. All smoothing, river carving, and bank
    // blending operate on this self-contained array — no neighborQuery needed.
    void computePaddedRegion(int worldOriginX, int worldOriginZ,
                             int regionSizeX, int regionSizeZ, int padding,
                             RegionGenerationData::ColumnInfo* columnsOut);

    // Trivially copies a 16×16 slice from pre-computed region data into
    // the per-chunk output arrays. No noise computation.
    void computeChunkFromRegion(const RegionGenerationData& region,
                                int chunkWorldX, int chunkWorldZ,
                                int heightOut[16][16],
                                Biome biomeOut[16][16],
                                bool  riverOut[16][16]);

    // ── Legacy per-chunk mode (singleton fallback) ──────────────────────
    // Fills heightOut[16][16], biomeOut[16][16], riverOut[16][16]
    // neighborQuery allows cross-chunk access for boundary smoothing (may be empty)
    void compute(int chunkWorldX, int chunkWorldZ,
                 int   heightOut[16][16],
                 Biome biomeOut[16][16],
                 bool  riverOut[16][16],
                 const NeighborQuery& neighborQuery = {});

    // ── Single-point queries ────────────────────────────────────────────
    float queryHeight(float worldX, float worldZ) const;
    HeightBiome queryHeightBiome(float worldX, float worldZ) const;

private:
    const Noise& m_noise;
    uint64_t m_seed;

    // ── Scale constants (initialized from Config.h in constructor) ──
    float m_continentalScale;
    float m_terrainScale;
    float m_detailScale;
    float m_warpScale;
    float m_warpStrength;
    float m_tempScale;
    float m_humidScale;
    float m_riverScale;

    // ── Seed-derived offsets (decorrelate noise layers) ────────────
    float m_contOffset;
    float m_terrOffset;
    float m_tempOffset;
    float m_humidOffset;
    float m_riverOffset;

    // ── Per-column computation ─────────────────────────────────────
    struct ColumnResult {
        float rawHeight;
        float continentalness;
        Biome biome;
    };
    ColumnResult computeColumn(float wx, float wz) const;

    // ── Per-column helpers ─────────────────────────────────────────
    float baseHeightRaw(float wx, float wz, float& outContinentalness) const;
    float applyErosion(float h, float wx, float wz) const;
    Biome classifyBiome(float temperature, float humidity,
                        float rawHeight, float continentalness) const;
    float biomeHeightMul(Biome b) const;
    float biomeHeightOffset(Biome b) const;

    // ── Padded-grid smoothing / river (no NeighborQuery) ────────────
    void smoothBiomeBoundariesPadded(RegionGenerationData::ColumnInfo* columns,
                                     int paddedWidth, int paddedDepth, int padding);
    bool computeRiversPadded(int worldOriginX, int worldOriginZ,
                             RegionGenerationData::ColumnInfo* columns,
                             int paddedWidth, int paddedDepth, int padding);
    void smoothRiverBanksPadded(RegionGenerationData::ColumnInfo* columns,
                                int paddedWidth, int paddedDepth, int padding);

    // ── Legacy smoothing passes (with NeighborQuery) ────────────────
    void smoothBiomeBoundaries(int heightMap[16][16],
                               const Biome biomeMap[16][16],
                               int chunkWorldX, int chunkWorldZ,
                               const NeighborQuery& neighborQuery);
    void smoothRiverBanks(int heightMap[16][16],
                          const bool riverMap[16][16],
                          int chunkWorldX, int chunkWorldZ,
                          const NeighborQuery& neighborQuery);

    // ── Legacy river detection ─────────────────────────────────────
    bool computeRivers(int chunkWorldX, int chunkWorldZ,
                       int heightMap[16][16],
                       const Biome biomeMap[16][16],
                       bool riverOut[16][16],
                       const NeighborQuery& neighborQuery);
};
