#include "world/RedstoneBlocks.h"
#include "world/OreGenerator.h"
#include "world/Noise.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_set>

namespace {
constexpr uint64_t COAL_DOMAIN = 0x4F52455F434F414CULL;
constexpr uint64_t IRON_DOMAIN = 0x4F52455F49524F4EULL;
constexpr uint64_t GOLD_DOMAIN = 0x4F52455F474F4C44ULL;
constexpr uint64_t DIAM_DOMAIN = 0x4F52455F4449414DULL;
constexpr uint64_t VEIN_DOMAIN = 0x4F52455F5645494EULL;
constexpr uint64_t EMERALD_DOMAIN = 0x4F52455F454D4552ULL;

int floorDiv16(int value) {
    int quotient = value / 16;
    if (value % 16 < 0) --quotient;
    return quotient;
}

uint32_t localOreIndex(int x, int y, int z) {
    return static_cast<uint32_t>((y + 64) * 256 + z * 16 + x);
}

struct EmeraldCacheEntry {
    uint64_t seed = 0;
    int chunkX = 0;
    int chunkZ = 0;
    std::unordered_set<uint32_t> blocks;
};

const std::unordered_set<uint32_t>& emeraldCandidates(
    uint64_t seed, int targetChunkX, int targetChunkZ) {
    thread_local std::deque<EmeraldCacheEntry> cache;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->seed == seed && it->chunkX == targetChunkX &&
            it->chunkZ == targetChunkZ) {
            if (it != cache.begin()) {
                EmeraldCacheEntry hit = std::move(*it);
                cache.erase(it);
                cache.push_front(std::move(hit));
            }
            return cache.front().blocks;
        }
    }
    EmeraldCacheEntry entry;
    entry.seed = seed;
    entry.chunkX = targetChunkX;
    entry.chunkZ = targetChunkZ;
    const int targetMinX = targetChunkX * 16;
    const int targetMinZ = targetChunkZ * 16;
    for (int sourceChunkZ = targetChunkZ - 1;
         sourceChunkZ <= targetChunkZ + 1; ++sourceChunkZ) {
        for (int sourceChunkX = targetChunkX - 1;
             sourceChunkX <= targetChunkX + 1; ++sourceChunkX) {
            for (int attempt = 0; attempt < 100; ++attempt) {
                uint64_t random = WorldGenContext::hashPosition(
                    seed ^ EMERALD_DOMAIN, sourceChunkX, attempt, sourceChunkZ);
                int x = sourceChunkX * 16 + static_cast<int>(random & 15u);
                int z = sourceChunkZ * 16 + static_cast<int>((random >> 4) & 15u);
                const double u = static_cast<double>((random >> 8) & 0xffffffu) /
                    static_cast<double>(0x1000000u);
                constexpr double minimum = -16.0;
                constexpr double maximum = 320.0;
                constexpr double mode = 232.0;
                constexpr double split = (mode - minimum) / (maximum - minimum);
                const double sampledY = u < split
                    ? minimum + std::sqrt(u * (maximum - minimum) *
                                          (mode - minimum))
                    : maximum - std::sqrt((1.0 - u) * (maximum - minimum) *
                                           (maximum - mode));
                int y = std::clamp(static_cast<int>(std::floor(sampledY)),
                                   -16, 319);
                const int count = static_cast<int>((random >> 32) % 5u);
                for (int block = 0; block < count; ++block) {
                    if (floorDiv16(x) == targetChunkX && floorDiv16(z) == targetChunkZ &&
                        y >= -64 && y <= 319) {
                        entry.blocks.insert(localOreIndex(
                            x - targetMinX, y, z - targetMinZ));
                    }
                    random = WorldGenContext::mix(random +
                        static_cast<uint64_t>(block) + 0x9E3779B97F4A7C15ULL);
                    switch (random % 6u) {
                        case 0: ++x; break; case 1: --x; break;
                        case 2: ++z; break; case 3: --z; break;
                        case 4: ++y; break; default: --y; break;
                    }
                }
            }
        }
    }
    cache.push_front(std::move(entry));
    if (cache.size() > 24) cache.pop_back();
    return cache.front().blocks;
}
}


// Java placement parameters: two size-seven veins with triangular -32..32
// anchors, and four size-seven buried veins with uniform -64..64 anchors.
struct LapisCacheEntry {
    uint64_t seed; int x, z;
    std::unordered_set<uint32_t> ordinary, buried;
};
static const LapisCacheEntry& lapisCandidates(uint64_t seed, int chunkX, int chunkZ) {
    thread_local std::deque<LapisCacheEntry> cache;
    for (const auto& entry : cache) if (entry.seed == seed && entry.x == chunkX && entry.z == chunkZ) return entry;
    LapisCacheEntry entry{seed, chunkX, chunkZ, {}, {}};
    for (int sourceZ = chunkZ - 1; sourceZ <= chunkZ + 1; ++sourceZ)
        for (int sourceX = chunkX - 1; sourceX <= chunkX + 1; ++sourceX)
            for (int attempt = 0; attempt < 6; ++attempt) {
                uint64_t random = WorldGenContext::hashPosition(seed ^ 0x4c415049535f3232ULL, sourceX, attempt, sourceZ);
                const auto draw = [&random](unsigned bound) { random = WorldGenContext::mix(random); return static_cast<unsigned>(random % bound); };
                const double originX = double(sourceX * 16) + draw(16), originZ = double(sourceZ * 16) + draw(16);
                const double originY = attempt < 2 ? -32. + draw(33) + draw(33) : -64. + draw(129);
                const double angle = double(draw(1u << 24)) / (1u << 24) * 3.141592653589793;
                const double dx = std::sin(angle) * 7 / 8, dz = std::cos(angle) * 7 / 8;
                const double y0 = originY + draw(3) - 2, y1 = originY + draw(3) - 2;
                for (int segment = 0; segment < 7; ++segment) {
                    const double t = segment / 7.;
                    const double x = originX + dx * (1 - 2 * t), z = originZ + dz * (1 - 2 * t), y = y0 + (y1 - y0) * t;
                    const double radius = ((std::sin(3.141592653589793 * t) + 1) * (double(draw(1u << 24)) / (1u << 24)) * 7 / 16 + 1) / 2;
                    for (int bz = int(std::floor(z-radius)); bz <= int(std::floor(z+radius)); ++bz)
                        for (int bx = int(std::floor(x-radius)); bx <= int(std::floor(x+radius)); ++bx)
                            for (int by = std::max(-64, int(std::floor(y-radius))); by <= std::min(319, int(std::floor(y+radius))); ++by) {
                                if (floorDiv16(bx) != chunkX || floorDiv16(bz) != chunkZ) continue;
                                const double nx = (bx + .5 - x) / radius, ny = (by + .5 - y) / radius, nz = (bz + .5 - z) / radius;
                                if (nx*nx + ny*ny + nz*nz < 1) (attempt < 2 ? entry.ordinary : entry.buried).insert(localOreIndex(bx-chunkX*16, by, bz-chunkZ*16));
                            }
                }
            }
    if (cache.size() >= 24) cache.pop_back();
    cache.push_front(std::move(entry)); return cache.front();
}

// Java-style size-eight veins: four uniform -64..15 attempts and eight
// triangular -96..-32 attempts (peak -64). Independent from prior ore domains.
struct RedstoneCacheEntry {
    uint64_t seed; int x, z;
    std::unordered_set<uint32_t> blocks;
};
static const RedstoneCacheEntry& redstoneCandidates(uint64_t seed, int chunkX, int chunkZ) {
    thread_local std::deque<RedstoneCacheEntry> cache;
    for (const auto& entry : cache) if (entry.seed == seed && entry.x == chunkX && entry.z == chunkZ) return entry;
    RedstoneCacheEntry entry{seed, chunkX, chunkZ, {}};
    for (int sourceZ = chunkZ - 1; sourceZ <= chunkZ + 1; ++sourceZ)
        for (int sourceX = chunkX - 1; sourceX <= chunkX + 1; ++sourceX)
            for (int attempt = 0; attempt < 12; ++attempt) {
                uint64_t random = WorldGenContext::hashPosition(seed ^ 0x52454453544f4e45ULL, sourceX, attempt, sourceZ);
                const auto draw = [&random](unsigned bound) { random = WorldGenContext::mix(random); return static_cast<unsigned>(random % bound); };
                const double originX = double(sourceX * 16) + draw(16), originZ = double(sourceZ * 16) + draw(16);
                const double originY = attempt < 4 ? -64. + draw(80) : -96. + draw(33) + draw(33);
                const double angle = double(draw(1u << 24)) / (1u << 24) * 3.141592653589793;
                const double dx = std::sin(angle) * 8 / 8, dz = std::cos(angle) * 8 / 8;
                const double y0 = originY + draw(3) - 2, y1 = originY + draw(3) - 2;
                for (int segment = 0; segment < 8; ++segment) {
                    const double t = segment / 8.;
                    const double x = originX + dx * (1 - 2 * t), z = originZ + dz * (1 - 2 * t), y = y0 + (y1 - y0) * t;
                    const double radius = ((std::sin(3.141592653589793 * t) + 1) * (double(draw(1u << 24)) / (1u << 24)) * 8 / 16 + 1) / 2;
                    for (int bz = int(std::floor(z-radius)); bz <= int(std::floor(z+radius)); ++bz)
                        for (int bx = int(std::floor(x-radius)); bx <= int(std::floor(x+radius)); ++bx)
                            for (int by = std::max(-64, int(std::floor(y-radius))); by <= std::min(319, int(std::floor(y+radius))); ++by) {
                                if (floorDiv16(bx) != chunkX || floorDiv16(bz) != chunkZ) continue;
                                const double nx = (bx + .5 - x) / radius, ny = (by + .5 - y) / radius, nz = (bz + .5 - z) / radius;
                                if (nx*nx + ny*ny + nz*nz < 1) entry.blocks.insert(localOreIndex(bx-chunkX*16, by, bz-chunkZ*16));
                            }
                }
            }
    if (cache.size() >= 24) cache.pop_back();
    cache.push_front(std::move(entry)); return cache.front();
}

void OreGenerator::configure(FastNoiseLite& noise, int seed, float frequency) {
    noise.SetSeed(seed);
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noise.SetFrequency(frequency);
    noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    noise.SetFractalOctaves(2);
    noise.SetFractalLacunarity(2.0f);
    noise.SetFractalGain(0.55f);
}

OreGenerator::OreGenerator(const Noise&, uint64_t seed)
    : m_context(seed)
{
    configure(m_coal, m_context.noiseSeed(COAL_DOMAIN), 0.045f);
    configure(m_iron, m_context.noiseSeed(IRON_DOMAIN), 0.052f);
    configure(m_gold, m_context.noiseSeed(GOLD_DOMAIN), 0.060f);
    configure(m_diamond, m_context.noiseSeed(DIAM_DOMAIN), 0.070f);
    configure(m_vein, m_context.noiseSeed(VEIN_DOMAIN), 0.012f);
}

float OreGenerator::triangle(float y, float minY, float peakY, float maxY) {
    if (y <= minY || y >= maxY) return 0.0f;
    if (y <= peakY) return (y - minY) / (peakY - minY);
    return (maxY - y) / (maxY - peakY);
}

bool OreGenerator::emeraldBiome(Biome biome) {
    switch (biome) {
        case Biome::MOUNTAINS: case Biome::HILLS: case Biome::MEADOW:
        case Biome::GLACIAL_PEAKS: case Biome::ALPINE_TUNDRA:
        case Biome::ROCKY_STEPPE: case Biome::LIMESTONE_HIGHLANDS:
        case Biome::VOLCANIC_HIGHLANDS: return true;
        default: return false;
    }
}

BlockId OreGenerator::getOre(float x, float y, float z, BlockId existing,
                             Biome biome, const std::function<bool()>& exposed) const {
    const int worldX = static_cast<int>(std::floor(x));
    const int worldY = static_cast<int>(std::floor(y));
    const int worldZ = static_cast<int>(std::floor(z));
    if (emeraldBiome(biome) && worldY >= -16 && worldY <= 319 &&
        (existing == BlockId::STONE || existing == BlockId::DEEPSLATE ||
         existing == BlockId::GRANITE || existing == BlockId::TUFF)) {
        const int chunkX = floorDiv16(worldX);
        const int chunkZ = floorDiv16(worldZ);
        const auto& candidates = emeraldCandidates(m_context.seed(), chunkX, chunkZ);
        const uint32_t index = localOreIndex(
            worldX - chunkX * 16, worldY, worldZ - chunkZ * 16);
        if (candidates.count(index) != 0)
            return existing == BlockId::DEEPSLATE
                ? BlockId::DEEPSLATE_EMERALD_ORE : BlockId::EMERALD_ORE;
    }
    if (existing != BlockId::STONE && existing != BlockId::DEEPSLATE)
        return BlockId::AIR;

    float vein = 0.5f + 0.5f * m_vein.GetNoise(x, y * 0.75f, z);
    auto score = [vein, x, y, z](const FastNoiseLite& noise) {
        float n = 0.5f + 0.5f * noise.GetNoise(x, y, z);
        return n * 0.72f + vein * 0.28f;
    };

    const float diamondY = std::clamp((16.0f - y) / 76.0f, 0.0f, 1.0f);
    if (diamondY > 0.0f && score(m_diamond) > 0.973f - diamondY * 0.020f)
        return BlockId::DIAMOND_ORE;

    const float goldY = triangle(y, -64.0f, -16.0f, 33.0f);
    if (goldY > 0.0f && score(m_gold) > 0.966f - goldY * 0.018f)
        return BlockId::GOLD_ORE;

    const float lowIron = triangle(y, -64.0f, 16.0f, 73.0f);
    const float highIron = std::clamp((y - 80.0f) / 176.0f, 0.0f, 1.0f);
    const float ironY = std::max(lowIron, highIron);
    if (ironY > 0.0f && score(m_iron) > 0.960f - ironY * 0.022f)
        return BlockId::IRON_ORE;

    const float coalY = triangle(y, -1.0f, 96.0f, 257.0f);
    if (coalY > 0.0f && score(m_coal) > 0.956f - coalY * 0.024f)
        return BlockId::COAL_ORE;

    const auto& lapis = lapisCandidates(m_context.seed(), floorDiv16(worldX), floorDiv16(worldZ));
    const auto index = localOreIndex(worldX - floorDiv16(worldX) * 16, worldY, worldZ - floorDiv16(worldZ) * 16);
    if (lapis.ordinary.count(index) || (lapis.buried.count(index) && exposed && !exposed()))
        return existing == BlockId::DEEPSLATE ? BlockId::DEEPSLATE_LAPIS_ORE : BlockId::LAPIS_ORE;
    if(worldY<=15 && redstoneCandidates(m_context.seed(),floorDiv16(worldX),floorDiv16(worldZ)).blocks.count(index))
        return Redstone::worldBlock(existing==BlockId::DEEPSLATE?Redstone::NativeBlock::DEEPSLATE_REDSTONE_ORE:Redstone::NativeBlock::REDSTONE_ORE);
    return BlockId::AIR;
}
