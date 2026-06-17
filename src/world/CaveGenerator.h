#pragma once

#include <cstdint>
#include <vector>

class Noise;

// ── Worm-Based Cave Generator ───────────────────────────────────────────────
//
// Replaces Perlin noise thresholding with deterministic 3D random-walk
// "worms" that carve continuous, meandering tunnel networks.
//
// Architecture:
//   1. The world is divided into a coarse 3D grid of "worm cells".
//   2. Each cell deterministically spawns 0–N worm paths (hash-based).
//   3. Each worm walks through space with smooth directional evolution.
//   4. A block is a cave if it lies within the radius of any worm segment
//      or within a deterministic junction room.
//
// Usage (region path):
//   auto tunnels = cg.generateTunnels(minX,minY,minZ, maxX,maxY,maxZ);
//   if (cg.isInTunnel(wx, wy, wz, tunnels) || cg.isInRoom(wx, wy, wz, sy))
//       → carve AIR
//
// Usage (legacy singleton path): same pattern, generate tunnels for the
// chunk + padding before the per-column cave carving loop.
//
// ── Tunnel Segment ──────────────────────────────────────────────────────

struct TunnelSegment {
    float startX, startY, startZ;
    float endX,   endY,   endZ;
    float radius;
};

// ── Cave Generator ──────────────────────────────────────────────────────

// Spatial index for O(1) tunnel segment lookup per block.
// Partitions tunnel segments into a coarse 3D grid so isInTunnel()
// only checks segments in the relevant cell instead of all ~1776.
struct CaveSpatialIndex {
    static constexpr float kCellSize = 8.0f;

    float worldMinX = 0, worldMinY = 0, worldMinZ = 0;
    int sizeX = 0, sizeY = 0, sizeZ = 0;

    // Flat 3D grid: cells[cz * sizeX * sizeY + cy * sizeX + cx]
    // Each cell stores indices into the original TunnelSegment vector.
    std::vector<std::vector<size_t>> cells;

    void build(const std::vector<TunnelSegment>& tunnels,
               float minX, float minY, float minZ,
               float maxX, float maxY, float maxZ);

    // Returns the cell containing (wx, wy, wz), or nullptr if out of bounds.
    const std::vector<size_t>* getCell(float wx, float wy, float wz) const;
};

class CaveGenerator {
public:
    CaveGenerator(const Noise& noise, uint64_t seed);

    // ── Tunnel generation ────────────────────────────────────────────────
    // Generate all tunnel segments whose bounding boxes intersect the given
    // world-coordinate AABB. Call once per region (or per chunk+padding for
    // the legacy path) before carving.
    std::vector<TunnelSegment> generateTunnels(
        int minX, int minY, int minZ,
        int maxX, int maxY, int maxZ) const;

    // ── Point-in-tunnel query ────────────────────────────────────────────
    // Returns true if (wx, wy, wz) is within any precomputed tunnel segment.
    // Uses bounding-box early-out → point-to-segment distance.

    // Spatial-index variant (region path): only checks segments in the
    // relevant grid cell. Use CaveSpatialIndex::build() first.
    bool isInTunnel(float wx, float wy, float wz,
                    const CaveSpatialIndex& index,
                    const std::vector<TunnelSegment>& tunnels) const;

    // Linear-scan variant (legacy singleton fallback)
    bool isInTunnel(float wx, float wy, float wz,
                    const std::vector<TunnelSegment>& tunnels) const;

    // ── Point-in-room query ──────────────────────────────────────────────
    // Returns true if (wx, wy, wz) is inside a deterministic junction room.
    // Rooms are placed on a coarse grid; this is a pure function of world
    // coords + seed (no precomputation needed).
    bool isInRoom(float wx, float wy, float wz, int surfaceY) const;

private:
    const Noise& m_noise;
    uint64_t     m_seed;

    // ── Internal helpers ─────────────────────────────────────────────────
    // Deterministically generate all worms that start in a given worm cell.
    void generateWormsForCell(int cellX, int cellY, int cellZ,
                              int caveMinY, int caveMaxY,
                              std::vector<TunnelSegment>& out) const;

    // Compute a deterministic 64-bit hash for a worm cell.
    static uint64_t cellHash(int cellX, int cellY, int cellZ, uint64_t seed);

    // Simple splitmix64 PRNG (deterministic, no state needed across calls).
    static uint64_t splitmix64(uint64_t& state);
    static float    splitmixFloat(uint64_t& state);   // [0, 1)
    static float    splitmixRange(uint64_t& state, float minVal, float maxVal);
    static int      splitmixInt(uint64_t& state, int minVal, int maxVal);  // inclusive
};
