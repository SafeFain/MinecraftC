#include "world/HeightPipeline.h"
#include "world/Noise.h"
#include "Config.h"

#include <cmath>
#include <algorithm>
#include <random>

// ── Constructor ─────────────────────────────────────────────────────────

HeightPipeline::HeightPipeline(const Noise& noise, uint64_t seed)
    : m_noise(noise)
    , m_seed(seed)
    , m_continentalScale(Config::CONTINENTAL_SCALE)
    , m_terrainScale(Config::TERRAIN_BASE_SCALE)
    , m_detailScale(Config::TERRAIN_DETAIL_SCALE)
    , m_warpScale(Config::TERRAIN_WARP_SCALE)
    , m_warpStrength(Config::TERRAIN_WARP_STRENGTH)
    , m_tempScale(Config::BIOME_TEMP_SCALE)
    , m_humidScale(Config::BIOME_HUMID_SCALE)
    , m_riverScale(Config::RIVER_SCALE)
{
    // Derive decorrelated offsets from seed
    std::mt19937_64 rng(seed);
    m_contOffset  = static_cast<float>(rng() % 10000) * 0.1f;
    m_terrOffset  = static_cast<float>(rng() % 10000) * 0.1f;
    m_tempOffset  = static_cast<float>(rng() % 10000) * 0.1f;
    m_humidOffset = static_cast<float>(rng() % 10000) * 0.1f;
    m_riverOffset = static_cast<float>(rng() % 10000) * 0.1f;
}

// ── Per-column height + biome (shared by compute and queryHeight) ──────

HeightPipeline::ColumnResult
HeightPipeline::computeColumn(float wx, float wz) const {
    ColumnResult result;

    // baseHeightRaw already computes continentalness — capture it to avoid recomputation
    result.rawHeight = baseHeightRaw(wx, wz, result.continentalness);
    result.rawHeight = applyErosion(result.rawHeight, wx, wz);

    // Compute temperature and humidity for biome classification
    float temp = m_noise.octave2D(
        wx * m_tempScale + m_tempOffset,
        wz * m_tempScale + m_tempOffset,
        Config::BIOME_TEMP_OCTAVES, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY);
    float humid = m_noise.octave2D(
        wx * m_humidScale + m_humidOffset,
        wz * m_humidScale + m_humidOffset,
        Config::BIOME_HUMID_OCTAVES, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY);

    result.biome = classifyBiome(temp, humid, result.rawHeight, result.continentalness);
    return result;
}

// ── Main compute ────────────────────────────────────────────────────────

void HeightPipeline::compute(int chunkWorldX, int chunkWorldZ,
                             int   heightOut[16][16],
                             Biome biomeOut[16][16],
                             bool  riverOut[16][16],
                             const NeighborQuery& neighborQuery) {
    // Phase 1: per-column raw height + biome
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            float wx = static_cast<float>(chunkWorldX + lx);
            float wz = static_cast<float>(chunkWorldZ + lz);

            ColumnResult col = computeColumn(wx, wz);

            // Apply biome height modulation
            float finalH = col.rawHeight * biomeHeightMul(col.biome)
                         + biomeHeightOffset(col.biome);

            // Clamp to valid range
            finalH = std::max(1.0f, std::min(static_cast<float>(Config::TERRAIN_MAX_HEIGHT), finalH));

            heightOut[lx][lz] = static_cast<int>(std::round(finalH));
            biomeOut[lx][lz]  = col.biome;
        }
    }

    // Phase 1b: smooth biome boundaries to prevent sharp cliffs
    smoothBiomeBoundaries(heightOut, biomeOut, chunkWorldX, chunkWorldZ, neighborQuery);

    // Phase 2: river detection (post-process, needs full height+biome map)
    computeRivers(chunkWorldX, chunkWorldZ, heightOut, biomeOut, riverOut, neighborQuery);

    // Phase 2b: smooth river banks so terrain slopes gently down to water
    smoothRiverBanks(heightOut, riverOut, chunkWorldX, chunkWorldZ, neighborQuery);
}

// ── Single-point height query ─────────────────────────────────────────────

float HeightPipeline::queryHeight(float wx, float wz) const {
    ColumnResult col = computeColumn(wx, wz);
    float finalH = col.rawHeight * biomeHeightMul(col.biome)
                 + biomeHeightOffset(col.biome);
    finalH = std::max(1.0f, std::min(static_cast<float>(Config::CHUNK_SIZE_Y - 20), finalH));
    return finalH;
}

HeightBiome HeightPipeline::queryHeightBiome(float wx, float wz) const {
    ColumnResult col = computeColumn(wx, wz);
    float finalH = col.rawHeight * biomeHeightMul(col.biome)
                 + biomeHeightOffset(col.biome);
    finalH = std::max(1.0f, std::min(static_cast<float>(Config::CHUNK_SIZE_Y - 20), finalH));
    return { static_cast<int>(std::round(finalH)), col.biome };
}

// ── Base height (three-layer domain-warped synthesis) ───────────────────

float HeightPipeline::baseHeightRaw(float wx, float wz, float& outContinentalness) const {
    // Layer 1: Continental-scale noise (very low frequency)
    float continentalNoise = m_noise.octave2D(
        wx * m_continentalScale + m_contOffset,
        wz * m_continentalScale + m_contOffset,
        Config::CONTINENTAL_OCTAVES, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY);
    float continentalness = (continentalNoise + 1.0f) * 0.5f;  // map [-1,1] → [0,1]
    outContinentalness = continentalness;

    // Layer 2: Domain-warped terrain (medium frequency, main shape)
    float terrain = m_noise.warped2D(
        wx * m_terrainScale + m_terrOffset,
        wz * m_terrainScale + m_terrOffset,
        Config::TERRAIN_OCTAVES,
        m_warpStrength * m_terrainScale,
        m_warpScale);

    // Layer 3: Detail perturbation (high frequency, small amplitude)
    float detail = m_noise.octave2D(
        wx * m_detailScale + m_terrOffset + 500.0f,
        wz * m_detailScale + m_terrOffset + 500.0f,
        Config::DETAIL_OCTAVES, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY) * Config::DETAIL_AMP;

    // Continentalness strictly determines the height ZONE.
    // Terrain only adds variation WITHIN each zone, so ocean never
    // becomes land and vice versa.
    float seaLevel = static_cast<float>(Config::SEA_LEVEL);
    float baseH, terrainAmp;

    if (continentalness < Config::OCEAN_THRESHOLD) {
        // OCEAN: deep sea floor, gentle variation
        float t = continentalness / Config::OCEAN_THRESHOLD;    // [0, 1]
        baseH = seaLevel + Config::HEIGHT_OCEAN_BASE + t * Config::HEIGHT_OCEAN_RANGE;
        terrainAmp = Config::HEIGHT_OCEAN_AMP;
    } else if (continentalness < Config::BEACH_THRESHOLD) {
        // BEACH: transition from shallow water to coastal land
        float t = (continentalness - Config::OCEAN_THRESHOLD)
                / (Config::BEACH_THRESHOLD - Config::OCEAN_THRESHOLD);  // [0, 1]
        baseH = seaLevel + Config::HEIGHT_BEACH_BASE + t * Config::HEIGHT_BEACH_RANGE;
        terrainAmp = Config::HEIGHT_BEACH_AMP;
    } else {
        // LAND: rolling hills, mountains at high continentalness
        float t = (continentalness - Config::BEACH_THRESHOLD)
                / (1.0f - Config::BEACH_THRESHOLD);             // [0, 1]
        baseH = seaLevel + Config::HEIGHT_LAND_BASE + t * Config::HEIGHT_LAND_RANGE;
        terrainAmp = Config::HEIGHT_LAND_AMP;
    }

    return baseH + terrain * terrainAmp + detail;
}

// ── Simplified thermal erosion ──────────────────────────────────────────

float HeightPipeline::applyErosion(float h, float wx, float wz) const {
    // Sample neighbors at 1-block spacing
    float dummyCont;
    float hL = baseHeightRaw(wx - 1.0f, wz, dummyCont);
    float hR = baseHeightRaw(wx + 1.0f, wz, dummyCont);
    float hD = baseHeightRaw(wx, wz - 1.0f, dummyCont);
    float hU = baseHeightRaw(wx, wz + 1.0f, dummyCont);

    float slopeX = (hR - hL) * 0.5f;
    float slopeZ = (hU - hD) * 0.5f;
    float slope = std::sqrt(slopeX * slopeX + slopeZ * slopeZ);

    // Steep slopes: erode downward
    if (slope > Config::EROSION_SLOPE_THRESHOLD) {
        float erosion = (slope - Config::EROSION_SLOPE_THRESHOLD) * Config::EROSION_RATE;
        h -= erosion;
    }

    // Low depressions: mild fill
    float avgNeighbor = (hL + hR + hD + hU) * 0.25f;
    if (h < avgNeighbor - Config::EROSION_FILL_THRESHOLD) {
        h += (avgNeighbor - h - Config::EROSION_FILL_THRESHOLD) * Config::EROSION_FILL_RATE;
    }

    return h;
}

// ── Biome classification ────────────────────────────────────────────────

Biome HeightPipeline::classifyBiome(float temperature, float humidity,
                                    float rawHeight, float continentalness) const {
    // ── Ocean/Beach: use continentalness as single source of truth ───
    // This is the same threshold used by baseHeightRaw to determine
    // ocean/beach/land zones — no more split-brain disagreement.
    if (continentalness < Config::OCEAN_THRESHOLD) {
        return Biome::OCEAN;
    }
    if (continentalness < Config::BEACH_THRESHOLD) {
        return Biome::BEACH;
    }

    // ── Elevation-based biome overrides ─────────────────────────────
    if (rawHeight > Config::ELEVATION_MOUNTAIN_MIN) return Biome::MOUNTAINS;
    if (rawHeight > Config::ELEVATION_HILL_MIN)     return Biome::HILLS;

    // ── Temperature-humidity decision table ─────────────────────────
    if (temperature > Config::TEMP_HOT_THRESHOLD) {
        // Hot
        if (humidity < Config::HUMID_DRY_THRESHOLD)      return Biome::DESERT;
        if (humidity < Config::HUMID_WET_THRESHOLD)      return Biome::SAVANNA;
        return Biome::JUNGLE;
    }
    if (temperature > Config::TEMP_WARM_THRESHOLD) {
        // Warm
        if (humidity < Config::HUMID_DRY_THRESHOLD)      return Biome::PLAINS;
        if (humidity < Config::HUMID_WET_THRESHOLD)      return Biome::FOREST;
        return Biome::SWAMP;
    }
    if (temperature > Config::TEMP_COOL_THRESHOLD) {
        // Cool
        if (humidity < Config::HUMID_DRY_THRESHOLD)      return Biome::TAIGA;
        if (humidity < Config::HUMID_WET_THRESHOLD)      return Biome::FOREST;
        return Biome::TAIGA;
    }
    // Cold
    return Biome::SNOW_TUNDRA;
}

float HeightPipeline::biomeHeightMul(Biome b) const {
    return getBiomeProps(b).heightMul;
}

float HeightPipeline::biomeHeightOffset(Biome b) const {
    return getBiomeProps(b).heightOffset;
}

// ── Biome boundary smoothing ────────────────────────────────────────────
// Blends heights at biome transitions to prevent abrupt cliffs where two
// biomes with different height modulation meet.

void HeightPipeline::smoothBiomeBoundaries(int heightMap[16][16],
                                           const Biome biomeMap[16][16],
                                           int chunkWorldX, int chunkWorldZ,
                                           const NeighborQuery& neighborQuery) {
    constexpr float blendWeight = Config::BIOME_BLEND_WEIGHT;
    constexpr int passes = Config::BIOME_SMOOTH_PASSES;

    for (int p = 0; p < passes; ++p) {
        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                Biome centerBiome = biomeMap[lx][lz];
                float h = static_cast<float>(heightMap[lx][lz]);
                float blendedH = h;
                int neighborCount = 0;

                // Check 4 cardinal neighbors
                const int dirs[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}};
                for (int d = 0; d < 4; ++d) {
                    int nx = lx + dirs[d][0];
                    int nz = lz + dirs[d][1];

                    // Cross-chunk neighbor
                    if (nx < 0 || nx >= 16 || nz < 0 || nz >= 16) {
                        if (neighborQuery) {
                            int worldNX = chunkWorldX + nx;
                            int worldNZ = chunkWorldZ + nz;
                            auto opt = neighborQuery(worldNX, worldNZ);
                            if (opt.has_value()) {
                                if (opt->biome != centerBiome) {
                                    blendedH += static_cast<float>(opt->height);
                                    ++neighborCount;
                                }
                            }
                        }
                        continue;
                    }

                    // Only blend if neighbor has different biome (transition zone)
                    if (biomeMap[nx][nz] != centerBiome) {
                        blendedH += static_cast<float>(heightMap[nx][nz]);
                        ++neighborCount;
                    }
                }

                if (neighborCount > 0) {
                    float avg = blendedH / static_cast<float>(neighborCount + 1);
                    heightMap[lx][lz] = static_cast<int>(std::round(
                        h * (1.0f - blendWeight) + avg * blendWeight));
                }
            }
        }
    }
}

// ── River bank smoothing ────────────────────────────────────────────────
// Gradually slopes terrain down toward river level so rivers don't have
// vertical cliff banks. Non-river cells near rivers get their height
// reduced toward the river water level over a few iterations.

void HeightPipeline::smoothRiverBanks(int heightMap[16][16],
                                      const bool riverMap[16][16],
                                      int chunkWorldX, int chunkWorldZ,
                                      const NeighborQuery& neighborQuery) {
    constexpr int blendRadius = Config::RIVER_BANK_BLEND_RADIUS;
    constexpr int passes = Config::RIVER_BANK_PASSES;
    const int seaLevel = Config::SEA_LEVEL;

    for (int p = 0; p < passes; ++p) {
        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                // Skip river cells themselves — they're already carved
                if (riverMap[lx][lz]) continue;

                int h = heightMap[lx][lz];
                // Only smooth terrain above river level
                if (h <= seaLevel + 1) continue;

                // Find minimum distance to any river cell in neighborhood
                int minDist = blendRadius + 1;
                for (int dx = -blendRadius; dx <= blendRadius; ++dx) {
                    for (int dz = -blendRadius; dz <= blendRadius; ++dz) {
                        int nx = lx + dx;
                        int nz = lz + dz;
                        bool isRiver = false;

                        // Cross-chunk: query neighbor
                        if (nx < 0 || nx >= 16 || nz < 0 || nz >= 16) {
                            if (neighborQuery) {
                                int worldNX = chunkWorldX + nx;
                                int worldNZ = chunkWorldZ + nz;
                                auto opt = neighborQuery(worldNX, worldNZ);
                                // Approximate: if neighbor height is near sea level,
                                // it's likely a river cell in the adjacent chunk
                                if (opt.has_value() && opt->height <= seaLevel + 1) {
                                    isRiver = true;
                                }
                            }
                        } else {
                            isRiver = riverMap[nx][nz];
                        }

                        if (isRiver) {
                            int dist = std::max(std::abs(dx), std::abs(dz));
                            if (dist < minDist) minDist = dist;
                        }
                    }
                }

                if (minDist <= blendRadius) {
                    float t = static_cast<float>(minDist) / static_cast<float>(blendRadius + 1);
                    float pullDown = (1.0f - t) * static_cast<float>(h - seaLevel) * 0.5f;
                    int newH = static_cast<int>(std::round(static_cast<float>(h) - pullDown));
                    heightMap[lx][lz] = std::max(seaLevel, newH);
                }
            }
        }
    }
}

// ── River detection (flow-field skeleton method) ────────────────────────

bool HeightPipeline::computeRivers(int chunkWorldX, int chunkWorldZ,
                                   int heightMap[16][16],
                                   const Biome biomeMap[16][16],
                                   bool riverOut[16][16],
                                   const NeighborQuery& neighborQuery) {
    // Initialize: no rivers
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            riverOut[lx][lz] = false;
        }
    }

    // Extended region: pad by RIVER_EDGE_EXTEND to handle river widths
    // at chunk boundaries. A 4-block wide river needs 4-block padding
    // so the flow constraint can see across the boundary.
    constexpr int EXT = Config::RIVER_EDGE_EXTEND;
    constexpr int SIZE = 16 + EXT * 2;
    float riverPotential[SIZE][SIZE] = {};
    bool  isRiver[SIZE][SIZE] = {};

    // Compute river potential for the extended region
    for (int ix = 0; ix < SIZE; ++ix) {
        for (int iz = 0; iz < SIZE; ++iz) {
            float wx = static_cast<float>(chunkWorldX + ix - EXT);
            float wz = static_cast<float>(chunkWorldZ + iz - EXT);

            // Ridge noise for river paths
            float ridge = 1.0f - 2.0f * std::abs(
                m_noise.noise2D(wx * m_riverScale + m_riverOffset,
                                wz * m_riverScale + m_riverOffset));

            // Only consider for ridge above threshold AND on land
            if (ridge > Config::RIVER_THRESHOLD) {
                // Get local height (use neighborQuery for cross-chunk positions)
                int lx = ix - EXT;
                int lz = iz - EXT;
                float h;
                if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) {
                    h = static_cast<float>(heightMap[lx][lz]);
                } else if (neighborQuery) {
                    auto opt = neighborQuery(static_cast<int>(wx), static_cast<int>(wz));
                    if (opt.has_value()) {
                        h = static_cast<float>(opt->height);
                    } else {
                        // No neighbor available — fall back to recomputing
                        ColumnResult col = computeColumn(wx, wz);
                        h = col.rawHeight * biomeHeightMul(col.biome)
                          + biomeHeightOffset(col.biome);
                        h = std::max(1.0f, std::min(static_cast<float>(Config::CHUNK_SIZE_Y - 20), h));
                    }
                } else {
                    // No neighborQuery provided — use self-consistent recomputation
                    ColumnResult col = computeColumn(wx, wz);
                    h = col.rawHeight * biomeHeightMul(col.biome)
                      + biomeHeightOffset(col.biome);
                    h = std::max(1.0f, std::min(static_cast<float>(Config::CHUNK_SIZE_Y - 20), h));
                }

                riverPotential[ix][iz] = h + (ridge - Config::RIVER_THRESHOLD) * Config::RIVER_RIDGE_HEIGHT_BOOST;
            } else {
                riverPotential[ix][iz] = 999.0f;  // not a river cell
            }
        }
    }

    // Determine river cells: a cell is a river if its potential is valid
    // AND it has at least one lower-potential neighbor (flow constraint)
    for (int ix = EXT; ix < SIZE - EXT; ++ix) {
        for (int iz = EXT; iz < SIZE - EXT; ++iz) {
            float pot = riverPotential[ix][iz];
            if (pot >= 900.0f) continue;  // not a candidate

            // Check 8-neighborhood for flow target
            bool hasLower = false;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue;
                    if (riverPotential[ix + dx][iz + dz] < pot) {
                        hasLower = true;
                        break;
                    }
                }
                if (hasLower) break;
            }

            // Also accept if it flows to ocean/beach (within chunk or via neighbor)
            int lx = ix - EXT;
            int lz = iz - EXT;
            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) {
                Biome b = biomeMap[lx][lz];
                if (b == Biome::OCEAN || b == Biome::BEACH) {
                    hasLower = true;
                }
            } else if (neighborQuery) {
                int worldX = chunkWorldX + lx;
                int worldZ = chunkWorldZ + lz;
                auto opt = neighborQuery(worldX, worldZ);
                if (opt.has_value() &&
                    (opt->biome == Biome::OCEAN || opt->biome == Biome::BEACH)) {
                    hasLower = true;
                }
            }

            if (hasLower) {
                isRiver[ix][iz] = true;
            }
        }
    }

    // Copy results back to output
    bool anyRiver = false;
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            riverOut[lx][lz] = isRiver[lx + EXT][lz + EXT];
            if (riverOut[lx][lz]) anyRiver = true;
        }
    }

    // Apply river carving to height map
    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            if (!riverOut[lx][lz]) continue;

            int seaLevel = Config::SEA_LEVEL;
            int oldH = heightMap[lx][lz];

            // River carves down (never raises terrain)
            int carveTarget = seaLevel - Config::RIVER_CARVE_DEPTH;
            int newH = oldH - Config::RIVER_CARVE_STEP;
            // Never carve below the river floor — carveTarget is a minimum bound
            if (newH < carveTarget) newH = carveTarget;
            // Only apply if carving actually lowers the terrain
            if (newH < oldH) heightMap[lx][lz] = newH;
        }
    }

    return anyRiver;
}

// ═══════════════════════════════════════════════════════════════════════════
// Padded Region Methods (no NeighborQuery needed — all data is self-contained)
// ═══════════════════════════════════════════════════════════════════════════

void HeightPipeline::computePaddedRegion(
    int worldOriginX, int worldOriginZ,
    int regionSizeX, int regionSizeZ, int padding,
    RegionGenerationData::ColumnInfo* columnsOut)
{
    const int paddedW = regionSizeX + 2 * padding;
    const int paddedD = regionSizeZ + 2 * padding;
    const size_t totalCells = static_cast<size_t>(paddedW) * static_cast<size_t>(paddedD);

    // ── Phase A: raw heights + biome (no erosion yet) ─────────────────────
    // We compute all raw heights first so erosion can read neighbor values
    // from the already-computed grid instead of recomputing baseHeightRaw().
    std::vector<float> rawHeights(totalCells);

    for (int lz = 0; lz < paddedD; ++lz) {
        for (int lx = 0; lx < paddedW; ++lx) {
            float wx = static_cast<float>(worldOriginX - padding + lx);
            float wz = static_cast<float>(worldOriginZ - padding + lz);
            size_t idx = static_cast<size_t>(lz) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx);

            float continentalness;
            float rawH = baseHeightRaw(wx, wz, continentalness);

            // Compute temperature and humidity for biome classification
            float temp = m_noise.octave2D(
                wx * m_tempScale + m_tempOffset,
                wz * m_tempScale + m_tempOffset,
                Config::BIOME_TEMP_OCTAVES, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY);
            float humid = m_noise.octave2D(
                wx * m_humidScale + m_humidOffset,
                wz * m_humidScale + m_humidOffset,
                Config::BIOME_HUMID_OCTAVES, Config::NOISE_PERSISTENCE, Config::NOISE_LACUNARITY);

            Biome biome = classifyBiome(temp, humid, rawH, continentalness);
            rawHeights[idx] = rawH;
            columnsOut[idx].biome   = biome;
            columnsOut[idx].isRiver = false;
        }
    }

    // ── Phase B: erosion pass (reads precomputed neighbor raw heights) ────
    for (int lz = 1; lz < paddedD - 1; ++lz) {
        for (int lx = 1; lx < paddedW - 1; ++lx) {
            size_t idx = static_cast<size_t>(lz) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx);
            float h = rawHeights[idx];

            // Read neighbors from precomputed grid (no noise recomputation)
            float hL = rawHeights[static_cast<size_t>(lz) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx - 1)];
            float hR = rawHeights[static_cast<size_t>(lz) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx + 1)];
            float hD = rawHeights[static_cast<size_t>(lz - 1) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx)];
            float hU = rawHeights[static_cast<size_t>(lz + 1) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx)];

            // Same erosion math as applyErosion(), but inlined
            float slopeX = (hR - hL) * 0.5f;
            float slopeZ = (hU - hD) * 0.5f;
            float slope = std::sqrt(slopeX * slopeX + slopeZ * slopeZ);

            if (slope > Config::EROSION_SLOPE_THRESHOLD) {
                float erosion = (slope - Config::EROSION_SLOPE_THRESHOLD) * Config::EROSION_RATE;
                h -= erosion;
            }

            float avgNeighbor = (hL + hR + hD + hU) * 0.25f;
            if (h < avgNeighbor - Config::EROSION_FILL_THRESHOLD) {
                h += (avgNeighbor - h - Config::EROSION_FILL_THRESHOLD) * Config::EROSION_FILL_RATE;
            }

            rawHeights[idx] = h;
        }
    }

    // ── Phase C: biome height modulation + clamp ──────────────────────────
    for (int lz = 0; lz < paddedD; ++lz) {
        for (int lx = 0; lx < paddedW; ++lx) {
            size_t idx = static_cast<size_t>(lz) * static_cast<size_t>(paddedW) + static_cast<size_t>(lx);
            Biome biome = columnsOut[idx].biome;
            float finalH = rawHeights[idx] * biomeHeightMul(biome)
                         + biomeHeightOffset(biome);
            finalH = std::max(1.0f, std::min(static_cast<float>(Config::TERRAIN_MAX_HEIGHT), finalH));
            columnsOut[idx].height = static_cast<int>(std::round(finalH));
        }
    }

    // Phase 1b: biome boundary smoothing (padded, self-contained)
    smoothBiomeBoundariesPadded(columnsOut, paddedW, paddedD, padding);

    // Phase 2: river detection
    computeRiversPadded(worldOriginX, worldOriginZ, columnsOut, paddedW, paddedD, padding);

    // Phase 2b: river bank smoothing
    smoothRiverBanksPadded(columnsOut, paddedW, paddedD, padding);
}

void HeightPipeline::computeChunkFromRegion(
    const RegionGenerationData& region,
    int chunkWorldX, int chunkWorldZ,
    int heightOut[16][16],
    Biome biomeOut[16][16],
    bool  riverOut[16][16])
{
    // Compute local offset of this chunk within the region's padded grid
    int baseLX = chunkWorldX - region.worldOriginX + region.padding;
    int baseLZ = chunkWorldZ - region.worldOriginZ + region.padding;

    for (int lx = 0; lx < 16; ++lx) {
        for (int lz = 0; lz < 16; ++lz) {
            const auto& info = region.col(baseLX + lx, baseLZ + lz);
            heightOut[lx][lz] = info.height;
            biomeOut[lx][lz]  = info.biome;
            riverOut[lx][lz]  = info.isRiver;
        }
    }
}

// ── Padded biome boundary smoothing ──────────────────────────────────────

void HeightPipeline::smoothBiomeBoundariesPadded(
    RegionGenerationData::ColumnInfo* columns,
    int paddedWidth, int paddedDepth, int padding)
{
    constexpr float blendWeight = Config::BIOME_BLEND_WEIGHT;
    constexpr int passes = Config::BIOME_SMOOTH_PASSES;

    // Work on a copy of heights so that pass N doesn't see pass N's own
    // modifications when checking neighbors
    std::vector<int> tempHeights(static_cast<size_t>(paddedWidth) * static_cast<size_t>(paddedDepth));
    for (int i = 0; i < paddedWidth * paddedDepth; ++i) {
        tempHeights[static_cast<size_t>(i)] = columns[i].height;
    }

    for (int p = 0; p < passes; ++p) {
        // Copy current heights into temp for this pass
        for (int i = 0; i < paddedWidth * paddedDepth; ++i) {
            tempHeights[static_cast<size_t>(i)] = columns[i].height;
        }

        for (int lz = padding; lz < paddedDepth - padding; ++lz) {
            for (int lx = padding; lx < paddedWidth - padding; ++lx) {
                auto& center = columns[lz * paddedWidth + lx];
                float h = static_cast<float>(center.height);
                float blendedH = h;
                int neighborCount = 0;

                const int dirs[4][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}};
                for (int d = 0; d < 4; ++d) {
                    int nx = lx + dirs[d][0];
                    int nz = lz + dirs[d][1];

                    // All neighbors within padded grid are valid
                    auto& neighbor = columns[nz * paddedWidth + nx];
                    if (neighbor.biome != center.biome) {
                        blendedH += static_cast<float>(tempHeights[nz * paddedWidth + nx]);
                        ++neighborCount;
                    }
                }

                if (neighborCount > 0) {
                    float avg = blendedH / static_cast<float>(neighborCount + 1);
                    center.height = static_cast<int>(std::round(
                        h * (1.0f - blendWeight) + avg * blendWeight));
                }
            }
        }
    }
}

// ── Padded river detection ───────────────────────────────────────────────

bool HeightPipeline::computeRiversPadded(
    int worldOriginX, int worldOriginZ,
    RegionGenerationData::ColumnInfo* columns,
    int paddedWidth, int paddedDepth, int padding)
{
    // The entire padded grid serves as the "extended region" for flow detection.
    // Cells within [padding, padded-padding) form the region core.

    // Compute river potential for all cells
    std::vector<float> riverPotential(static_cast<size_t>(paddedWidth) * static_cast<size_t>(paddedDepth));
    std::vector<bool>  isRiver(static_cast<size_t>(paddedWidth) * static_cast<size_t>(paddedDepth), false);

    for (int lz = 0; lz < paddedDepth; ++lz) {
        for (int lx = 0; lx < paddedWidth; ++lx) {
            float wx = static_cast<float>(worldOriginX - padding + lx);
            float wz = static_cast<float>(worldOriginZ - padding + lz);

            float ridge = 1.0f - 2.0f * std::abs(
                m_noise.noise2D(wx * m_riverScale + m_riverOffset,
                                wz * m_riverScale + m_riverOffset));

            if (ridge > Config::RIVER_THRESHOLD) {
                // Use the pre-computed height from the padded grid
                float h = static_cast<float>(columns[lz * paddedWidth + lx].height);
                float potential = h + (ridge - Config::RIVER_THRESHOLD) * Config::RIVER_RIDGE_HEIGHT_BOOST;
                riverPotential[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)] = potential;
            } else {
                riverPotential[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)] = 999.0f;
            }
        }
    }

    // Determine river cells: must have at least one lower-potential neighbor
    // (flow constraint). Only process the core region + padding — outermost
    // cells don't have full neighbor data for the flow check.
    for (int lz = 1; lz < paddedDepth - 1; ++lz) {
        for (int lx = 1; lx < paddedWidth - 1; ++lx) {
            float pot = riverPotential[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)];
            if (pot >= 900.0f) continue;

            bool hasLower = false;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    float nPot = riverPotential[static_cast<size_t>(lz + dz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx + dx)];
                    if (nPot < pot) { hasLower = true; break; }
                }
                if (hasLower) break;
            }

            // Also accept if adjacent to ocean/beach
            if (!hasLower) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        auto& ncol = columns[(lz + dz) * paddedWidth + (lx + dx)];
                        if (ncol.biome == Biome::OCEAN || ncol.biome == Biome::BEACH) {
                            hasLower = true;
                            break;
                        }
                    }
                    if (hasLower) break;
                }
            }

            if (hasLower) {
                isRiver[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)] = true;
            }
        }
    }

    // Apply river carving to height map and copy results back
    bool anyRiver = false;
    for (int lz = 0; lz < paddedDepth; ++lz) {
        for (int lx = 0; lx < paddedWidth; ++lx) {
            bool river = isRiver[static_cast<size_t>(lz) * static_cast<size_t>(paddedWidth) + static_cast<size_t>(lx)];
            auto& col = columns[lz * paddedWidth + lx];
            col.isRiver = river;

            if (river) {
                anyRiver = true;
                int seaLevel = Config::SEA_LEVEL;
                int oldH = col.height;
                int carveTarget = seaLevel - Config::RIVER_CARVE_DEPTH;
                int newH = oldH - Config::RIVER_CARVE_STEP;
                if (newH < carveTarget) newH = carveTarget;
                if (newH < oldH) col.height = newH;
            }
        }
    }

    return anyRiver;
}

// ── Padded river bank smoothing ──────────────────────────────────────────

void HeightPipeline::smoothRiverBanksPadded(
    RegionGenerationData::ColumnInfo* columns,
    int paddedWidth, int paddedDepth, int padding)
{
    constexpr int blendRadius = Config::RIVER_BANK_BLEND_RADIUS;
    constexpr int passes = Config::RIVER_BANK_PASSES;
    const int seaLevel = Config::SEA_LEVEL;

    // Work on a copy for pass-to-pass consistency
    std::vector<int> tempHeights(static_cast<size_t>(paddedWidth) * static_cast<size_t>(paddedDepth));

    for (int p = 0; p < passes; ++p) {
        for (int i = 0; i < paddedWidth * paddedDepth; ++i) {
            tempHeights[static_cast<size_t>(i)] = columns[i].height;
        }

        for (int lz = padding; lz < paddedDepth - padding; ++lz) {
            for (int lx = padding; lx < paddedWidth - padding; ++lx) {
                auto& col = columns[lz * paddedWidth + lx];
                if (col.isRiver) continue;

                int h = col.height;
                if (h <= seaLevel + 1) continue;

                // Find minimum distance to any river cell in neighborhood
                int minDist = blendRadius + 1;
                for (int dz = -blendRadius; dz <= blendRadius; ++dz) {
                    for (int dx = -blendRadius; dx <= blendRadius; ++dx) {
                        int nx = lx + dx;
                        int nz = lz + dz;
                        // All neighbors within padded grid are available
                        auto& neighbor = columns[nz * paddedWidth + nx];
                        if (neighbor.isRiver) {
                            int dist = std::max(std::abs(dx), std::abs(dz));
                            if (dist < minDist) minDist = dist;
                        }
                    }
                }

                if (minDist <= blendRadius) {
                    float t = static_cast<float>(minDist) / static_cast<float>(blendRadius + 1);
                    float pullDown = (1.0f - t) * static_cast<float>(h - seaLevel) * 0.5f;
                    int newH = static_cast<int>(std::round(static_cast<float>(h) - pullDown));
                    col.height = std::max(seaLevel, newH);
                }
            }
        }
    }
}
