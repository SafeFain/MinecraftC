#pragma once

#include "world/BiomeMap.h"
#include "world/Block.h"
#include "world/CaveGenerator.h"  // for TunnelSegment
#include <vector>
#include <cstdint>

// ── Region Generation Data ────────────────────────────────────────────────
// Shared pre-computed column data for a generation region (N×N chunks).
// All height/biome/river/cave operations use this padded grid so cross-chunk
// smoothing and connectivity are perfectly continuous within the region.
//
// Coordinate system:
//   worldOriginX = regionOriginCX * 16
//   worldOriginZ = regionOriginCZ * 16
//   localX = 0 is worldOriginX - padding  (leftmost padded column)
//   localZ = 0 is worldOriginZ - padding  (topmost padded column)
//   core region: localX in [padding, padding + regionSizeBlocks)
//
struct RegionGenerationData {

    // ── Region identity ──────────────────────────────────────────────────
    int regionOriginCX = 0;
    int regionOriginCZ = 0;
    int worldOriginX   = 0;
    int worldOriginZ   = 0;
    int regionSizeChunks = 3;
    int regionSizeBlocks = 48;   // regionSizeChunks * 16
    int padding         = 6;
    int paddedWidth     = 0;     // regionSizeBlocks + 2 * padding
    int paddedDepth     = 0;

    // ── Per-column data ──────────────────────────────────────────────────
    struct ColumnInfo {
        int   height  = 0;     // final surface Y (after river carving, biome modulation, smoothing)
        Biome biome   = Biome::OCEAN;
        bool  isRiver = false;
    };

    // Row-major: columns[lz * paddedWidth + lx]
    // Covers the full padded grid (paddedWidth × paddedDepth)
    std::vector<ColumnInfo> columns;

    // Helper: access column by local padded coordinates
    ColumnInfo& col(int lx, int lz) {
        return columns[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)];
    }
    const ColumnInfo& col(int lx, int lz) const {
        return columns[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)];
    }

    // ── Tree placements (local to worldOriginX / worldOriginZ) ───────────
    struct TreePlacement {
        int      localX      = 0;
        int      localZ      = 0;
        int      baseY       = 0;
        int      trunkHeight = 4;
        TreeType type        = TreeType::OAK;
    };
    std::vector<TreePlacement> trees;

    // ── Worm tunnel segments (precomputed for this region) ─────────────
    std::vector<TunnelSegment> tunnels;

    // ── Spatial index for fast tunnel lookup (built after tunnels) ──────
    CaveSpatialIndex caveIndex;

    // ── Pending blocks (tree leaves that fall outside this region) ───────
    struct PendingBlock {
        int     worldX = 0;
        int     worldY = 0;
        int     worldZ = 0;
        BlockId id     = BlockId::AIR;
    };
    std::vector<PendingBlock> pendingBlocks;
};
