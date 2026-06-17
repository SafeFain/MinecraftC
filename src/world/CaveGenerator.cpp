#include "world/CaveGenerator.h"
#include "world/Noise.h"
#include "Config.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cfloat>

CaveGenerator::CaveGenerator(const Noise& noise, uint64_t seed)
    : m_noise(noise)
    , m_seed(seed)
{}

// ═══════════════════════════════════════════════════════════════════════════
// Deterministic PRNG (splitmix64)
// ═══════════════════════════════════════════════════════════════════════════

uint64_t CaveGenerator::cellHash(int cellX, int cellY, int cellZ, uint64_t seed) {
    // SplitMix64-style hash mixing
    uint64_t h = static_cast<uint64_t>(cellX) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<uint64_t>(cellY) * 0xBF58476D1CE4E5B9ULL;
    h ^= static_cast<uint64_t>(cellZ) * 0x94D049BB133111EBULL;
    h ^= seed;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return h;
}

uint64_t CaveGenerator::splitmix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

float CaveGenerator::splitmixFloat(uint64_t& state) {
    return static_cast<float>((splitmix64(state) >> 11) & 0xFFFFFFFFFFFFFULL) / 4503599627370496.0f;
}

float CaveGenerator::splitmixRange(uint64_t& state, float minVal, float maxVal) {
    return minVal + splitmixFloat(state) * (maxVal - minVal);
}

int CaveGenerator::splitmixInt(uint64_t& state, int minVal, int maxVal) {
    return minVal + static_cast<int>(splitmix64(state) % static_cast<uint64_t>(maxVal - minVal + 1));
}

// ═══════════════════════════════════════════════════════════════════════════
// Worm generation for a single cell
// ═══════════════════════════════════════════════════════════════════════════

void CaveGenerator::generateWormsForCell(int cellX, int cellY, int cellZ,
                                          int caveMinY, int caveMaxY,
                                          std::vector<TunnelSegment>& out) const {
    uint64_t state = cellHash(cellX, cellY, cellZ, m_seed);

    // Number of worms: 0..CAVE_WORMS_PER_CELL_MAX
    int numWorms = static_cast<int>(splitmix64(state) % (Config::CAVE_WORMS_PER_CELL_MAX + 1));

    int cellOriginX = cellX * Config::CAVE_WORM_CELL_SIZE_XZ;
    int cellOriginY = cellY * Config::CAVE_WORM_CELL_SIZE_Y;
    int cellOriginZ = cellZ * Config::CAVE_WORM_CELL_SIZE_XZ;

    for (int w = 0; w < numWorms; ++w) {
        // Worm type: 0-4 = main, 5-7 = branch, 8-9 = shaft
        int typeVal = static_cast<int>(splitmix64(state) % 10);

        float radius, stepSize, verticalBias;
        int   steps;
        if (typeVal < 5) {
            // Main tunnel (50%)
            radius       = splitmixRange(state, Config::CAVE_MAIN_RADIUS_MIN, Config::CAVE_MAIN_RADIUS_MAX);
            steps        = splitmixInt(state, Config::CAVE_MAIN_STEPS_MIN, Config::CAVE_MAIN_STEPS_MAX);
            stepSize     = Config::CAVE_MAIN_STEP_SIZE;
            verticalBias = Config::CAVE_MAIN_VERTICAL_BIAS;
        } else if (typeVal < 8) {
            // Branch passage (30%)
            radius       = splitmixRange(state, Config::CAVE_BRANCH_RADIUS_MIN, Config::CAVE_BRANCH_RADIUS_MAX);
            steps        = splitmixInt(state, Config::CAVE_BRANCH_STEPS_MIN, Config::CAVE_BRANCH_STEPS_MAX);
            stepSize     = Config::CAVE_BRANCH_STEP_SIZE;
            verticalBias = Config::CAVE_BRANCH_VERTICAL_BIAS;
        } else {
            // Vertical shaft (20%)
            radius       = splitmixRange(state, Config::CAVE_SHAFT_RADIUS_MIN, Config::CAVE_SHAFT_RADIUS_MAX);
            steps        = splitmixInt(state, Config::CAVE_SHAFT_STEPS_MIN, Config::CAVE_SHAFT_STEPS_MAX);
            stepSize     = Config::CAVE_SHAFT_STEP_SIZE;
            verticalBias = Config::CAVE_SHAFT_VERTICAL_BIAS;
        }

        // Start position within the cell, clamped to cave band
        float sx = static_cast<float>(cellOriginX) + splitmixFloat(state) * Config::CAVE_WORM_CELL_SIZE_XZ;
        float sy = static_cast<float>(cellOriginY) + splitmixFloat(state) * Config::CAVE_WORM_CELL_SIZE_Y;
        float sz = static_cast<float>(cellOriginZ) + splitmixFloat(state) * Config::CAVE_WORM_CELL_SIZE_XZ;
        sy = std::clamp(sy, static_cast<float>(caveMinY) + 1.0f, static_cast<float>(caveMaxY) - 1.0f);

        // Initial direction: random 3D vector
        float dx = splitmixFloat(state) * 2.0f - 1.0f;
        float dy = (splitmixFloat(state) * 2.0f - 1.0f) * 0.5f + verticalBias;
        float dz = splitmixFloat(state) * 2.0f - 1.0f;
        float invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
        dx *= invLen;
        dy *= invLen;
        dz *= invLen;

        float cx = sx, cy = sy, cz = sz;

        for (int step = 0; step < steps; ++step) {
            // Perturb direction using hash (deterministic, smooth turning)
            uint64_t turnHash = splitmix64(state);
            float tx = ((static_cast<float>(turnHash & 0xFFFF) / 65535.0f) - 0.5f) * 2.0f * Config::CAVE_WORM_TURN_RATE;
            float ty = ((static_cast<float>((turnHash >> 16) & 0xFFFF) / 65535.0f) - 0.5f) * 2.0f * Config::CAVE_WORM_TURN_RATE;
            float tz = ((static_cast<float>((turnHash >> 32) & 0xFFFF) / 65535.0f) - 0.5f) * 2.0f * Config::CAVE_WORM_TURN_RATE;

            dx += tx;
            dy += ty;
            dz += tz;

            // Re-normalize
            invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
            dx *= invLen;
            dy *= invLen;
            dz *= invLen;

            float nx = cx + dx * stepSize;
            float ny = cy + dy * stepSize;
            float nz = cz + dz * stepSize;

            // Y-clamp to cave band (bounce off boundaries)
            if (ny < static_cast<float>(caveMinY)) {
                ny = static_cast<float>(caveMinY);
                dy = std::abs(dy);
            }
            if (ny > static_cast<float>(caveMaxY)) {
                ny = static_cast<float>(caveMaxY);
                dy = -std::abs(dy);
            }

            out.push_back({cx, cy, cz, nx, ny, nz, radius});

            cx = nx;
            cy = ny;
            cz = nz;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Generate all tunnels intersecting an AABB
// ═══════════════════════════════════════════════════════════════════════════

std::vector<TunnelSegment> CaveGenerator::generateTunnels(
    int minX, int minY, int minZ,
    int maxX, int maxY, int maxZ) const
{
    // kMaxRadiusXZ must account for maximum worm travel distance in X/Z.
    // Main worms: maxSteps=60 × stepSize=2.0 = 120 blocks of travel.
    // Plus max worm radius (4.5) + wall noise amplitude (0.8) = 125.3 total.
    constexpr float kMaxRadiusXZ = Config::CAVE_MAIN_STEPS_MAX * Config::CAVE_MAIN_STEP_SIZE
                                  + Config::CAVE_MAIN_RADIUS_MAX + Config::CAVE_WALL_NOISE_AMP;

    // kMaxRadiusY only needs radius expansion, NOT worm travel distance.
    // Worms are Y-clamped into [caveMinY, caveMaxY] on every step, so a worm
    // from ANY Y-cell starts at an effective Y within the cave band.
    // Expanding Y by full worm travel would pull in worms from ~6 extra cells
    // above and below, all bunched at the band edges — drowning out natural
    // cave formation in the middle of the band.
    constexpr float kMaxRadiusY = Config::CAVE_MAIN_RADIUS_MAX + Config::CAVE_WALL_NOISE_AMP;

    int cellMinX = static_cast<int>(std::floor((minX - kMaxRadiusXZ) / Config::CAVE_WORM_CELL_SIZE_XZ));
    int cellMaxX = static_cast<int>(std::floor((maxX + kMaxRadiusXZ) / Config::CAVE_WORM_CELL_SIZE_XZ));
    int cellMinY = static_cast<int>(std::floor((minY - kMaxRadiusY) / Config::CAVE_WORM_CELL_SIZE_Y));
    int cellMaxY = static_cast<int>(std::floor((maxY + kMaxRadiusY) / Config::CAVE_WORM_CELL_SIZE_Y));
    int cellMinZ = static_cast<int>(std::floor((minZ - kMaxRadiusXZ) / Config::CAVE_WORM_CELL_SIZE_XZ));
    int cellMaxZ = static_cast<int>(std::floor((maxZ + kMaxRadiusXZ) / Config::CAVE_WORM_CELL_SIZE_XZ));

    // Cap to prevent runaway generation
    int cellCount = (cellMaxX - cellMinX + 1) * (cellMaxY - cellMinY + 1) * (cellMaxZ - cellMinZ + 1);
    constexpr int kMaxCells = 4096;
    if (cellCount > kMaxCells) {
        // Clamp to a safe range; rare edge case with tiny cell size or huge AABB
        cellMinX = std::max(cellMinX, cellMaxX - 8);
        cellMinY = std::max(cellMinY, cellMaxY - 4);
        cellMinZ = std::max(cellMinZ, cellMaxZ - 8);
    }

    std::vector<TunnelSegment> tunnels;
    // Reserve roughly 2× the expected segment count per cell
    // (avg 2 worms × avg 30 steps = 60, vs old 4×30 = 120 per cell)
    tunnels.reserve(static_cast<size_t>(cellCount) * Config::CAVE_WORMS_PER_CELL_MAX * 15);

    for (int cy = cellMinY; cy <= cellMaxY; ++cy) {
        for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
            for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
                generateWormsForCell(cx, cy, cz, minY, maxY, tunnels);
            }
        }
    }

    return tunnels;
}

// ═══════════════════════════════════════════════════════════════════════════
// CaveSpatialIndex implementation
// ═══════════════════════════════════════════════════════════════════════════

void CaveSpatialIndex::build(const std::vector<TunnelSegment>& tunnels,
                              float minX, float minY, float minZ,
                              float maxX, float maxY, float maxZ) {
    worldMinX = minX;
    worldMinY = minY;
    worldMinZ = minZ;

    sizeX = std::max(1, static_cast<int>(std::ceil((maxX - minX) / kCellSize)));
    sizeY = std::max(1, static_cast<int>(std::ceil((maxY - minY) / kCellSize)));
    sizeZ = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / kCellSize)));

    cells.clear();
    cells.resize(static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY) * static_cast<size_t>(sizeZ));

    for (size_t segIdx = 0; segIdx < tunnels.size(); ++segIdx) {
        const auto& seg = tunnels[segIdx];

        // Compute segment AABB expanded by radius + typical wall noise
        float r = seg.radius + Config::CAVE_WALL_NOISE_AMP;
        float segMinX = std::min(seg.startX, seg.endX) - r;
        float segMaxX = std::max(seg.startX, seg.endX) + r;
        float segMinY = std::min(seg.startY, seg.endY) - r;
        float segMaxY = std::max(seg.startY, seg.endY) + r;
        float segMinZ = std::min(seg.startZ, seg.endZ) - r;
        float segMaxZ = std::max(seg.startZ, seg.endZ) + r;

        // Clamp to indexed bounds
        segMinX = std::max(segMinX, worldMinX);
        segMaxX = std::min(segMaxX, maxX);
        segMinY = std::max(segMinY, worldMinY);
        segMaxY = std::min(segMaxY, maxY);
        segMinZ = std::max(segMinZ, worldMinZ);
        segMaxZ = std::min(segMaxZ, maxZ);

        int cellMinX = static_cast<int>((segMinX - worldMinX) / kCellSize);
        int cellMaxX = static_cast<int>((segMaxX - worldMinX) / kCellSize);
        int cellMinY = static_cast<int>((segMinY - worldMinY) / kCellSize);
        int cellMaxY = static_cast<int>((segMaxY - worldMinY) / kCellSize);
        int cellMinZ = static_cast<int>((segMinZ - worldMinZ) / kCellSize);
        int cellMaxZ = static_cast<int>((segMaxZ - worldMinZ) / kCellSize);

        cellMinX = std::max(0, cellMinX); cellMaxX = std::min(sizeX - 1, cellMaxX);
        cellMinY = std::max(0, cellMinY); cellMaxY = std::min(sizeY - 1, cellMaxY);
        cellMinZ = std::max(0, cellMinZ); cellMaxZ = std::min(sizeZ - 1, cellMaxZ);

        for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
            for (int cy = cellMinY; cy <= cellMaxY; ++cy) {
                for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
                    size_t cellIdx = (static_cast<size_t>(cz) * static_cast<size_t>(sizeY) + static_cast<size_t>(cy))
                                   * static_cast<size_t>(sizeX) + static_cast<size_t>(cx);
                    cells[cellIdx].push_back(segIdx);
                }
            }
        }
    }
}

const std::vector<size_t>* CaveSpatialIndex::getCell(float wx, float wy, float wz) const {
    int cx = static_cast<int>((wx - worldMinX) / kCellSize);
    int cy = static_cast<int>((wy - worldMinY) / kCellSize);
    int cz = static_cast<int>((wz - worldMinZ) / kCellSize);

    if (cx < 0 || cx >= sizeX || cy < 0 || cy >= sizeY || cz < 0 || cz >= sizeZ) {
        return nullptr;
    }

    size_t idx = (static_cast<size_t>(cz) * static_cast<size_t>(sizeY) + static_cast<size_t>(cy))
               * static_cast<size_t>(sizeX) + static_cast<size_t>(cx);
    return &cells[idx];
}

// ═══════════════════════════════════════════════════════════════════════════
// Point-in-tunnel check (spatial-index variant — region path)
// ═══════════════════════════════════════════════════════════════════════════

bool CaveGenerator::isInTunnel(float wx, float wy, float wz,
                                const CaveSpatialIndex& index,
                                const std::vector<TunnelSegment>& tunnels) const {
    // Wall noise sample (same for all tunnels at this point, for consistency)
    float wallNoise = m_noise.noise3D(
        wx * Config::CAVE_WALL_NOISE_SCALE,
        wy * Config::CAVE_WALL_NOISE_SCALE,
        wz * Config::CAVE_WALL_NOISE_SCALE) * Config::CAVE_WALL_NOISE_AMP;

    const std::vector<size_t>* cell = index.getCell(wx, wy, wz);
    if (!cell) return false;

    for (size_t segIdx : *cell) {
        const auto& seg = tunnels[segIdx];

        // AABB early-out with radius + wall noise expansion
        float r = seg.radius + wallNoise;
        if (r <= 0.0f) continue;

        float segMinX = std::min(seg.startX, seg.endX) - r;
        float segMaxX = std::max(seg.startX, seg.endX) + r;
        if (wx < segMinX || wx > segMaxX) continue;

        float segMinY = std::min(seg.startY, seg.endY) - r;
        float segMaxY = std::max(seg.startY, seg.endY) + r;
        if (wy < segMinY || wy > segMaxY) continue;

        float segMinZ = std::min(seg.startZ, seg.endZ) - r;
        float segMaxZ = std::max(seg.startZ, seg.endZ) + r;
        if (wz < segMinZ || wz > segMaxZ) continue;

        // Point-to-segment distance (squared)
        float dx = seg.endX - seg.startX;
        float dy = seg.endY - seg.startY;
        float dz = seg.endZ - seg.startZ;

        float px = wx - seg.startX;
        float py = wy - seg.startY;
        float pz = wz - seg.startZ;

        float segLen2 = dx * dx + dy * dy + dz * dz;
        float t = (segLen2 > 0.0f) ? std::clamp((px * dx + py * dy + pz * dz) / segLen2, 0.0f, 1.0f) : 0.0f;

        float closestX = seg.startX + t * dx;
        float closestY = seg.startY + t * dy;
        float closestZ = seg.startZ + t * dz;

        float dist2 = (wx - closestX) * (wx - closestX)
                    + (wy - closestY) * (wy - closestY)
                    + (wz - closestZ) * (wz - closestZ);

        float r2 = r * r;
        if (dist2 < r2) return true;
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Point-in-tunnel check (linear-scan variant — legacy singleton fallback)
// ═══════════════════════════════════════════════════════════════════════════

bool CaveGenerator::isInTunnel(float wx, float wy, float wz,
                                const std::vector<TunnelSegment>& tunnels) const {
    // Wall noise sample (same for all tunnels at this point, for consistency)
    float wallNoise = m_noise.noise3D(
        wx * Config::CAVE_WALL_NOISE_SCALE,
        wy * Config::CAVE_WALL_NOISE_SCALE,
        wz * Config::CAVE_WALL_NOISE_SCALE) * Config::CAVE_WALL_NOISE_AMP;

    for (const auto& seg : tunnels) {
        // AABB early-out with radius + wall noise expansion
        float r = seg.radius + wallNoise;
        if (r <= 0.0f) continue;

        float segMinX = std::min(seg.startX, seg.endX) - r;
        float segMaxX = std::max(seg.startX, seg.endX) + r;
        if (wx < segMinX || wx > segMaxX) continue;

        float segMinY = std::min(seg.startY, seg.endY) - r;
        float segMaxY = std::max(seg.startY, seg.endY) + r;
        if (wy < segMinY || wy > segMaxY) continue;

        float segMinZ = std::min(seg.startZ, seg.endZ) - r;
        float segMaxZ = std::max(seg.startZ, seg.endZ) + r;
        if (wz < segMinZ || wz > segMaxZ) continue;

        // Point-to-segment distance (squared)
        float dx = seg.endX - seg.startX;
        float dy = seg.endY - seg.startY;
        float dz = seg.endZ - seg.startZ;

        float px = wx - seg.startX;
        float py = wy - seg.startY;
        float pz = wz - seg.startZ;

        float segLen2 = dx * dx + dy * dy + dz * dz;
        float t = (segLen2 > 0.0f) ? std::clamp((px * dx + py * dy + pz * dz) / segLen2, 0.0f, 1.0f) : 0.0f;

        float closestX = seg.startX + t * dx;
        float closestY = seg.startY + t * dy;
        float closestZ = seg.startZ + t * dz;

        float dist2 = (wx - closestX) * (wx - closestX)
                    + (wy - closestY) * (wy - closestY)
                    + (wz - closestZ) * (wz - closestZ);

        float r2 = r * r;
        if (dist2 < r2) return true;
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Point-in-room check
// ═══════════════════════════════════════════════════════════════════════════

bool CaveGenerator::isInRoom(float wx, float wy, float wz, int surfaceY) const {
    // Only generate rooms within the cave Y band
    int caveMinY = Config::CAVE_MIN_Y;
    int caveMaxY = std::min(surfaceY - Config::CAVE_CEILING_MARGIN,
                            Config::CHUNK_SIZE_Y - Config::CAVE_TOP_MARGIN);
    if (wy < static_cast<float>(caveMinY) || wy > static_cast<float>(caveMaxY)) return false;

    int cellX = static_cast<int>(std::floor(wx / Config::CAVE_ROOM_GRID_XZ));
    int cellY = static_cast<int>(std::floor(wy / Config::CAVE_ROOM_GRID_Y));
    int cellZ = static_cast<int>(std::floor(wz / Config::CAVE_ROOM_GRID_XZ));

    uint64_t state = cellHash(cellX, cellY, cellZ, m_seed ^ 0xDEADBEEFCAFE0001ULL);

    // Chance check
    float chance = splitmixFloat(state);
    if (chance >= Config::CAVE_ROOM_CHANCE) return false;

    // Room center = cell center + random offset (within cell)
    float cx = (static_cast<float>(cellX) + 0.5f) * Config::CAVE_ROOM_GRID_XZ
             + splitmixRange(state, -Config::CAVE_ROOM_GRID_XZ * 0.3f, Config::CAVE_ROOM_GRID_XZ * 0.3f);
    float cy = (static_cast<float>(cellY) + 0.5f) * Config::CAVE_ROOM_GRID_Y
             + splitmixRange(state, -Config::CAVE_ROOM_GRID_Y * 0.3f, Config::CAVE_ROOM_GRID_Y * 0.3f);
    float cz = (static_cast<float>(cellZ) + 0.5f) * Config::CAVE_ROOM_GRID_XZ
             + splitmixRange(state, -Config::CAVE_ROOM_GRID_XZ * 0.3f, Config::CAVE_ROOM_GRID_XZ * 0.3f);

    float radius = splitmixRange(state, Config::CAVE_ROOM_RADIUS_MIN, Config::CAVE_ROOM_RADIUS_MAX);

    // Y-clamp room center to cave band
    cy = std::clamp(cy, static_cast<float>(caveMinY) + radius, static_cast<float>(caveMaxY) - radius);

    float dx = wx - cx;
    float dy = wy - cy;
    float dz = wz - cz;
    float dist2 = dx * dx + dy * dy + dz * dz;

    // Optionally add wall noise for organic edge
    float wallNoise = m_noise.noise3D(
        wx * Config::CAVE_WALL_NOISE_SCALE + 500.0f,
        wy * Config::CAVE_WALL_NOISE_SCALE + 500.0f,
        wz * Config::CAVE_WALL_NOISE_SCALE + 500.0f) * Config::CAVE_WALL_NOISE_AMP;

    float effectiveR = radius + wallNoise * 0.5f;
    return dist2 < (effectiveR * effectiveR);
}
