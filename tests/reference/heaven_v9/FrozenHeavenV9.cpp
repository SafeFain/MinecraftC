#include "FrozenHeavenV9.h"
#include "world/WorldGenContext.h"
#include "FrozenPlantSupport.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace FrozenHeavenReference {
namespace {
void fillBox(const StructureGenerator::StructureWriter& write, int x0, int y0,
             int z0, int x1, int y1, int z1, BlockId id) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) write(x, y, z, id);
}

void fillRing(const StructureGenerator::StructureWriter& write, int x0, int y,
              int z0, int x1, int z1, BlockId id) {
    for (int x = x0; x <= x1; ++x) {
        write(x, y, z0, id);
        write(x, y, z1, id);
    }
    for (int z = z0 + 1; z < z1; ++z) {
        write(x0, y, z, id);
        write(x1, y, z, id);
    }
}

void buildDoor(const StructureGenerator::StructureWriter& write,int x,int y,int z,
               BedDirection direction,DoorMaterial material,bool rightHinge=false) {
    DoorState state;state.material=material;state.direction=direction;state.rightHinge=rightHinge;
    write(x,y,z,doorBlock(state));state.upper=true;write(x,y+1,z,doorBlock(state));
}

void buildXiguangRuin(const StructurePlacement& placement,
                      const StructureGenerator::StructureWriter& write) {
    const int cx=(placement.minX+placement.maxX)/2;
    const int cz=(placement.minZ+placement.maxZ)/2;
    const int base=placement.baseY;
    const uint64_t variant=placement.variant;
    // Raised radial sanctuary with four independently ruined gateways.
    for (int dz=-6; dz<=6; ++dz) for (int dx=-6; dx<=6; ++dx) {
        const int d2=dx*dx+dz*dz;
        if (d2<=36) write(cx+dx,base+1,cz+dz,
            d2>24 ? slabBlock(ArchitecturalMaterial::Sunstone,BlockHalf::Bottom)
                  : BlockId::SUNSTONE);
    }
    for (const BedDirection direction : {BedDirection::North,BedDirection::East,
                                         BedDirection::South,BedDirection::West}) {
        const glm::ivec3 out=bedDirectionOffset(direction);
        const glm::ivec3 side(-out.z,0,out.x);
        for (int step=6; step<=9; ++step)
            write(cx+out.x*step,base+1,cz+out.z*step,
                  slabBlock(ArchitecturalMaterial::Sunstone,BlockHalf::Bottom));
        const int intact=3+static_cast<int>(WorldGenContext::hashPosition(
            variant,out.x,2,out.z)%4u);
        for (const int s : {-2,2}) for (int y=2; y<=intact; ++y)
            write(cx+out.x*7+side.x*s,base+y,cz+out.z*7+side.z*s,
                  BlockId::SUNSTONE);
        for (int s=-2; s<=2; ++s)
            if ((variant+s+static_cast<int>(direction))%5!=0)
                write(cx+out.x*7+side.x*s,base+intact,
                      cz+out.z*7+side.z*s,
                      slabBlock(ArchitecturalMaterial::Sunstone,BlockHalf::Bottom));
    }
    for (int y=2; y<=4; ++y) write(cx,base+y,cz,BlockId::SUNSTONE);
    write(cx,base+5,cz,BlockId::STAR_CRYSTAL);
    write(cx+2,base+2,cz,BlockId::CHEST);
    for (int i=0;i<12;++i) {
        const uint64_t h=WorldGenContext::hashPosition(variant,cx+i,7,cz-i);
        const int dx=static_cast<int>((h>>8)%19)-9;
        const int dz=static_cast<int>((h>>24)%19)-9;
        write(cx+dx,base+1,cz+dz,(h&1u)?BlockId::SUNSTONE:BlockId::MOSS);
    }
}

void buildStarCrystalGeode(const StructurePlacement& placement,
                           const StructureGenerator::StructureWriter& write) {
    const int cx=(placement.minX+placement.maxX)/2;
    const int cz=(placement.minZ+placement.maxZ)/2;
    const int base=placement.baseY;
    const uint64_t variant=placement.variant;
    // Cracked hollow shell; south side and roof deliberately remain open.
    for(int dy=0;dy<=7;++dy) for(int dz=-5;dz<=5;++dz)
        for(int dx=-5;dx<=5;++dx) {
            const int d2=dx*dx+dz*dz+(dy-3)*(dy-3);
            if(d2<17||d2>29) continue;
            const uint64_t h=WorldGenContext::hashPosition(variant,dx,dy,dz);
            if ((dz<-3&&std::abs(dx)<=1&&dy<=3) || h%13u==0) continue;
            write(cx+dx,base+dy,cz+dz,
                  h%5u==0?BlockId::MOSS:BlockId::CLOUDSTONE);
        }
    fillBox(write,cx-3,base,cz-3,cx+3,base,cz+3,BlockId::MOSS);
    for(int dy=1;dy<=5;++dy)
        write(cx,base+dy,cz,BlockId::STAR_CRYSTAL);
    for (const auto& p : std::array<glm::ivec3,4>{{
            {-2,1,1},{2,1,-1},{1,1,2},{-1,1,-2}}}) {
        write(cx+p.x,base+p.y,cz+p.z,BlockId::STAR_CRYSTAL);
        write(cx+p.x,base+p.y+1,cz+p.z,BlockId::STAR_CRYSTAL);
    }
    write(cx+3,base+1,cz+1,BlockId::CHEST);
}

void buildCloudspireTower(const StructurePlacement& placement,
                          const StructureGenerator::StructureWriter& write) {
    const int cx=(placement.minX+placement.maxX)/2;
    const int cz=(placement.minZ+placement.maxZ)/2;
    const int base=placement.baseY;
    const int height=18+static_cast<int>(placement.variant%7u);
    fillBox(write,cx-4,base,cz-4,cx+4,base,cz+4,BlockId::SUNSTONE);
    constexpr std::array<std::pair<int,int>,8> stairPath{{
        {0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    for(int y=1;y<=height;++y) {
        const int radius=y>height-5?2:3;
        fillRing(write,cx-radius,base+y,cz-radius,cx+radius,cz+radius,
                 y%5==0?BlockId::CLOUDSTONE:BlockId::SUNSTONE);
        if(y<=3){write(cx,base+y,cz-3,BlockId::AIR);}
        if(y==6||y==12) {
            for(int dz=-5;dz<=5;++dz) for(int dx=-5;dx<=5;++dx) {
                const int edge=std::max(std::abs(dx),std::abs(dz));
                if(edge>=2)
                    write(cx+dx,base+y,cz+dz,
                          slabBlock(ArchitecturalMaterial::Cloudstone,
                                    BlockHalf::Bottom));
                if(edge==5)
                    write(cx+dx,base+y+1,cz+dz,
                          slabBlock(ArchitecturalMaterial::Cloudstone,
                                    BlockHalf::Bottom));
            }
        }
        const size_t phase=static_cast<size_t>(y-1)%stairPath.size();
        const auto [sx,sz]=stairPath[phase];
        const auto [nx,nz]=stairPath[(phase+1)%stairPath.size()];
        const BedDirection direction = nz < sz ? BedDirection::North :
            nx > sx ? BedDirection::East :
            nz > sz ? BedDirection::South : BedDirection::West;
        write(cx+sx,base+y,cz+sz,stairBlock(
            ArchitecturalMaterial::Sunstone,BlockHalf::Bottom,
            direction));
    }
    fillBox(write,cx-3,base+height+1,cz-3,cx+3,base+height+1,cz+3,
            slabBlock(ArchitecturalMaterial::Cloudstone,BlockHalf::Bottom));
    for (const glm::ivec3& o : std::array<glm::ivec3,4>{{
            {3,0,0},{-3,0,0},{0,0,3},{0,0,-3}}}) {
        write(cx+o.x,base+height+2,cz+o.z,BlockId::SUNSTONE);
        write(cx+o.x,base+height+3,cz+o.z,BlockId::STAR_CRYSTAL);
    }
    write(cx,base+height+2,cz,BlockId::CLOUDSTONE);
    write(cx,base+height+3,cz,BlockId::STAR_CRYSTAL);
    write(cx+1,base+1,cz+1,BlockId::CHEST);
    write(cx,base+3,cz-3,BlockId::SUNSTONE);
    buildDoor(write,cx,base+1,cz-3,BedDirection::North,DoorMaterial::Skyroot);
}

void buildSkywayShrine(const StructurePlacement& placement,
                       const StructureGenerator::StructureWriter& write) {
    const int cx = (placement.minX + placement.maxX) / 2;
    const int cz = (placement.minZ + placement.maxZ) / 2;
    const int base = placement.baseY;
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dx = -3; dx <= 3; ++dx) {
            const bool alternate = ((dx + dz +
                static_cast<int>(placement.variant & 1u)) & 1) != 0;
            write(cx + dx, base, cz + dz,
                  alternate ? BlockId::CLOUDSTONE : BlockId::SUNSTONE);
        }
    }
    write(cx, base + 1, cz, BlockId::STAR_CRYSTAL);
    write(cx + 2, base + 1, cz, BlockId::CHEST);
}

constexpr uint64_t HEAVEN_SEED_DOMAIN = 0x484556454E5F5345ULL;
constexpr uint64_t HEAVEN_ISLAND_DOMAIN = 0x48454156454E4953ULL;
constexpr uint64_t HEAVEN_LANDMARK_DOMAIN = 0x4845565255494E53ULL;
constexpr uint64_t HEAVEN_DECOR_DOMAIN = 0x48455645434F4C4FULL;
constexpr uint64_t HEAVEN_LAYER1_DOMAIN = 0x4845564C41594531ULL;
constexpr uint64_t HEAVEN_LAYER2_DOMAIN = 0x4845564C41594532ULL;
constexpr uint64_t HEAVEN_LAYER4_DOMAIN = 0x4845564C41594534ULL;
constexpr uint64_t HEAVEN_LAYER5_DOMAIN = 0x4845564C41594535ULL;
constexpr uint64_t HEAVEN_SKYWAY_DOMAIN = 0x534B595741595631ULL;
constexpr int HEAVEN_SKYWAY_CELL = 256;
constexpr std::array<int, 5> HEAVEN_SKYWAY_OFFSETS{{-88, -44, 0, 44, 88}};
constexpr uint64_t HEAVEN_GEODE_DOMAIN = 0x48455647454F4445ULL;
constexpr uint64_t HEAVEN_SPIRE_DOMAIN = 0x4845565350495245ULL;
constexpr int HEAVEN_LANDMARK_CELL = 192;
constexpr int HEAVEN_GEODE_CELL = 160;
constexpr int HEAVEN_SPIRE_CELL = 160;

// Shared field scales.  Every layer samples world coordinates directly, so
// boundaries cannot change output; the periods are far beyond the explored
// Heaven window and negative coordinates stay deterministic.
constexpr float HEAVEN_UNDERSIDE_SCALE = 0.008f;
constexpr float HEAVEN_SPIKE_SCALE = 0.018f;

float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) return value < edge0 ? 0.0f : 1.0f;
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return value % divisor < 0 ? quotient - 1 : quotient;
}

int heavenBiomeBand(int worldX, int worldZ, int layer) {
    // Broad, world-coordinate 256-cell bands guarantee that a normal
    // exploration window encounters every exclusive biome.  The per-layer
    // shift makes stacked islands sample different biomes while retaining
    // deterministic boundaries across chunks and negative coordinates.
    constexpr int cellSize = 256;
    int band = floorDiv(worldX + layer * 97, cellSize) +
               2 * floorDiv(worldZ + layer * 53, cellSize);
    band %= FrozenHeavenV9::HEAVEN_BIOME_COUNT;
    return band < 0 ? band + FrozenHeavenV9::HEAVEN_BIOME_COUNT : band;
}

Biome heavenCompatBiome(FrozenHeavenV9::HeavenBiome biome) {
    // The shared Biome enum keeps its 30 overworld values; Heaven columns
    // only ever carry these aliases for weather/locate compatibility.  The
    // visible surface, decoration, and generation language come exclusively
    // from HeavenBiome.
    switch (biome) {
        case FrozenHeavenV9::HeavenBiome::DawnMeadow: return Biome::PLAINS;
        case FrozenHeavenV9::HeavenBiome::SkyrootGrove: return Biome::FOREST;
        case FrozenHeavenV9::HeavenBiome::SunstoneHeights: return Biome::MOUNTAINS;
        case FrozenHeavenV9::HeavenBiome::StarCrystalGarden: return Biome::FLOWER_FOREST;
        case FrozenHeavenV9::HeavenBiome::CloudbloomFields: return Biome::MEADOW;
        case FrozenHeavenV9::HeavenBiome::SkystoneBarrens: return Biome::STONY_SHORE;
        case FrozenHeavenV9::HeavenBiome::GlimmerFen: return Biome::SWAMP;
        case FrozenHeavenV9::HeavenBiome::MoonpearlTerrace: return Biome::MOUNTAINS;
    }
    return Biome::PLAINS;
}

struct HeavenLayerProfile {
    int layer = 0;
    uint64_t domain = 0;
    float maskScale = 0.0055f;
    float detailScale = 0.014f;
    float broadScale = 0.0f;   // ≤0 disables the third field
    float presentThreshold = 0.10f;
    int topMin = 92;
    int topMax = 120;
    float topBase = 106.0f;
    float topAmplitude = 14.0f;
    float topScale = 0.0045f;
    float reliefScale = 0.012f;
    float reliefAmplitude = 2.0f;
    int baseY = 80;            // hard bottom clamp, keeps layers separated
    int minDepth = 4;
    int maxDepth = 10;
    int maxSpikeDepth = 0;
    float warpScale = 0.0f;    // L3 keeps the warped macro field
};

constexpr std::array<HeavenLayerProfile, 5> HEAVEN_LAYERS = {{
    // L1 浮屿浅滩 — common low drift shoals
    {0, HEAVEN_LAYER1_DOMAIN, 0.0075f, 0.023f, 0.0f, 0.02f,
     92, 120, 106.0f, 14.0f, 0.0045f, 0.012f, 2.0f, 80, 4, 10, 0, 0.0f},
    // L2 云间群岛 — mid-level isles
    {1, HEAVEN_LAYER2_DOMAIN, 0.0068f, 0.021f, 0.0f, 0.06f,
     136, 164, 150.0f, 14.0f, 0.0045f, 0.011f, 2.0f, 124, 6, 14, 0, 0.0f},
    // L3 主岛群 — keeps the warped macro + detail + broad composition and
    // stays the spawn layer with trees, ruins, geode, and tower anchors.
    {2, HEAVEN_ISLAND_DOMAIN, 0.0055f, 0.014f, 0.0022f, 0.10f,
     184, 216, 200.0f, 16.0f, 0.0045f, 0.010f, 3.0f, 172, 6, 44, 8, 0.0028f},
    // L4 高空群岛 — sparse high isles (replaces the old satellite band)
    {3, HEAVEN_LAYER4_DOMAIN, 0.0065f, 0.021f, 0.0f, 0.16f,
     232, 264, 248.0f, 16.0f, 0.0045f, 0.010f, 2.0f, 220, 5, 12, 0, 0.0f},
    // L5 天顶孤岛 — rarest, near the build limit
    {4, HEAVEN_LAYER5_DOMAIN, 0.0062f, 0.022f, 0.0f, 0.28f,
     280, 308, 294.0f, 14.0f, 0.0045f, 0.009f, 2.0f, 268, 4, 9, 0, 0.0f},
}};

bool inChunk(int worldX, int worldZ, int baseX, int baseZ) {
    return worldX >= baseX && worldX < baseX + Config::CHUNK_SIZE_X &&
           worldZ >= baseZ && worldZ < baseZ + Config::CHUNK_SIZE_Z;
}

int hashPercent(uint64_t seed, int x, int y, int z) {
    return static_cast<int>(WorldGenContext::hashPosition(seed, x, y, z) % 100u);
}

} // namespace

FrozenHeavenV9::FrozenHeavenV9(uint64_t derivedSeed)
    : m_seed(derivedSeed),m_noise(derivedSeed) {
    m_heavenSpawnBlock=findHeavenSpawnBlock();
    m_originSkyway=findOriginSkyway();
}
glm::ivec3 FrozenHeavenV9::findHeavenSpawnBlock(int maximumRadius) const {
    glm::ivec2 best{0};
    glm::ivec2 fallback{0};
    bool hasFallback = false;
    int bestScore = std::numeric_limits<int>::max();
    constexpr int step = 8;
    for (int radius = 0; radius <= maximumRadius; radius += step) {
        for (int z = -radius; z <= radius; z += step) {
            for (int x = -radius; x <= radius; x += step) {
                if (radius > 0 && std::abs(x) != radius && std::abs(z) != radius)
                    continue;
                const HeavenIslandColumn center = sampleHeavenLayer(x, z, 2);
                if (!center.present || center.top >= Config::WORLD_MAX_Y - 8)
                    continue;
                bool broadIsland = true;
                for (int dz = -6; dz <= 6 && broadIsland; dz += 3) {
                    for (int dx = -6; dx <= 6; dx += 3) {
                        const HeavenIslandColumn neighbor =
                            sampleHeavenLayer(x + dx, z + dz, 2);
                        if (!neighbor.present ||
                            std::abs(neighbor.top - center.top) > 8) {
                            broadIsland = false;
                            break;
                        }
                    }
                }
                if (!broadIsland) continue;
                if (!hasFallback) {
                    fallback = {x, z};
                    hasFallback = true;
                }
                int relief = 0;
                for (const glm::ivec2 offset : {glm::ivec2{-2, 0}, {2, 0},
                                                {0, -2}, {0, 2}}) {
                    const HeavenIslandColumn neighbor =
                        sampleHeavenLayer(x + offset.x, z + offset.y, 2);
                    if (!neighbor.present) {
                        relief = 99;
                        break;
                    }
                    relief = std::max(relief,
                        std::abs(neighbor.top - center.top));
                }
                const int score = relief * 100 + radius;
                if (relief <= 2 && score < bestScore) {
                    best = {x, z};
                    bestScore = score;
                }
            }
        }
        if (bestScore != std::numeric_limits<int>::max()) break;
    }
    if (bestScore == std::numeric_limits<int>::max() && hasFallback)
        best = fallback;
    const HeavenIslandColumn chosen = sampleHeavenLayer(best.x, best.y, 2);
    return {best.x, chosen.present ? chosen.top : 200, best.y};
}

FrozenHeavenV9::HeavenSkyway FrozenHeavenV9::findOriginSkyway() const {
    return SkywayPolicy::findOrigin(m_heavenSpawnBlock,[this](int x,int z){return sampleHeavenLayer(x,z,2);});
}

std::optional<FrozenHeavenV9::HeavenSkyway> FrozenHeavenV9::heavenSkywayForCell(int x,int z) const {
    if(!isHeaven())return {};
    return SkywayPolicy::forCell(m_seed,x,z,[this](int a,int b){return sampleHeavenLayer(a,b,2);});
}

std::optional<glm::dvec3> FrozenHeavenV9::heavenSkywayDestination(const glm::ivec3& core,bool upward) const {
    if(!isHeaven())return {};
    return SkywayPolicy::destination(core,upward,m_originSkyway,[this](int x,int z){return heavenSkywayForCell(x,z);});
}

FrozenHeavenV9::HeavenBiome FrozenHeavenV9::heavenBiomeAt(
    int worldX, int worldZ) const {
    return sampleHeavenIsland(worldX, worldZ).biome;
}

FrozenHeavenV9::HeavenIslandColumn FrozenHeavenV9::sampleHeavenLayer(
    int worldX, int worldZ, int layer) const {
    const HeavenLayerProfile& profile = HEAVEN_LAYERS[
        static_cast<size_t>(std::clamp(layer, 0, HEAVEN_LAYER_COUNT - 1))];
    HeavenIslandColumn island;
    island.biome = static_cast<HeavenBiome>(
        heavenBiomeBand(worldX, worldZ, profile.layer));
    const float x = static_cast<float>(worldX);
    const float z = static_cast<float>(worldZ);
    const float layerOffset = static_cast<float>(profile.layer * 37);

    // Island mask: the main layer keeps the warped macro + detail + broad
    // composition that shapes its archipelagos; the other four layers use a
    // two-octave broad/detail pair with independent domains and thresholds.
    // Every field samples world coordinates directly, so boundaries and
    // negative coordinates stay deterministic.
    float field = 0.0f;
    if (profile.warpScale > 0.0f) {
        const float warpX = m_noise.noise2D(
            x * profile.warpScale + 31.0f, z * profile.warpScale - 47.0f);
        const float warpZ = m_noise.noise2D(
            x * profile.warpScale - 83.0f, z * profile.warpScale + 71.0f);
        const float macro = m_noise.octave2D(
            (x + warpX * 72.0f) * profile.maskScale + 113.0f,
            (z + warpZ * 72.0f) * profile.maskScale - 127.0f,
            2, 0.55f, 2.0f);
        const float detail = m_noise.noise2D(
            x * profile.detailScale + 173.0f,
            z * profile.detailScale - 229.0f);
        const float broad = m_noise.noise2D(
            x * profile.broadScale - 401.0f,
            z * profile.broadScale + 311.0f);
        field = macro * 0.76f + detail * 0.20f + broad * 0.10f;
    } else {
        const float domainOffset =
            static_cast<float>(profile.domain & 0xffu);
        const float broadF = m_noise.octave2D(
            x * profile.maskScale + 211.0f + domainOffset,
            z * profile.maskScale - 337.0f - domainOffset,
            2, 0.55f, 2.0f);
        const float detailF = m_noise.noise2D(
            x * profile.detailScale - 419.0f + domainOffset * 0.5f,
            z * profile.detailScale + 503.0f - domainOffset * 0.5f);
        field = broadF * 0.72f + detailF * 0.28f;
    }
    if (field <= profile.presentThreshold) {
        // Void columns still carry a biome for weather/decoration queries
        // but have no terrain height or density range.
        return island;
    }
    island.present = true;

    // The summit height is independent of the footprint.  Its two coherent
    // fields give wide plateaus with gentle slopes instead of a per-island
    // spherical cap, while rounding keeps adjacent walkable columns smooth.
    const float topBase = m_noise.octave2D(
        x * profile.topScale + 521.0f + layerOffset,
        z * profile.topScale - 607.0f - layerOffset,
        2, 0.5f, 2.0f);
    const float topRelief = m_noise.noise2D(
        x * profile.reliefScale - 733.0f + layerOffset,
        z * profile.reliefScale + 809.0f - layerOffset);
    island.top = std::clamp(static_cast<int>(std::lround(
        profile.topBase + topBase * profile.topAmplitude +
        topRelief * profile.reliefAmplitude)),
        profile.topMin, profile.topMax);

    // Interior depth is driven by mask density, not distance to an anchor.
    // The underside field varies the taper and, on the main layer, a sparse
    // spike field gives islands a broken, End-like lower silhouette.
    const float normalizedDensity = std::clamp(
        (field - profile.presentThreshold) /
            (1.0f - profile.presentThreshold),
        0.0f, 1.0f);
    const float interior = smoothstep(0.0f, 0.58f, normalizedDensity);
    const float undersideNoise = 0.5f + 0.5f * m_noise.noise2D(
        x * HEAVEN_UNDERSIDE_SCALE + 947.0f + layerOffset,
        z * HEAVEN_UNDERSIDE_SCALE - 1013.0f - layerOffset);
    const float depthValue = static_cast<float>(profile.minDepth) + interior *
        (static_cast<float>(profile.maxDepth - profile.minDepth) +
         undersideNoise * 4.0f);
    const int depth = std::clamp(static_cast<int>(std::lround(depthValue)),
                                 profile.minDepth, profile.maxDepth);
    int spikeDepth = 0;
    if (profile.maxSpikeDepth > 0) {
        const float spikeNoise = 0.5f + 0.5f * m_noise.noise2D(
            x * HEAVEN_SPIKE_SCALE - 1217.0f,
            z * HEAVEN_SPIKE_SCALE + 1289.0f);
        spikeDepth = spikeNoise > 0.72f
            ? std::min(profile.maxSpikeDepth, static_cast<int>(std::lround(
                (spikeNoise - 0.72f) * 55.0f))) : 0;
    }
    // The hard baseY clamp keeps every layer's block range clear of the band
    // below it, so stacked islands in one column keep an air gap.
    island.bottom = std::min(island.top, std::max(
        profile.baseY, island.top - depth - spikeDepth + 1));
    return island;
}

std::array<FrozenHeavenV9::HeavenIslandColumn,
           FrozenHeavenV9::HEAVEN_LAYER_COUNT>
FrozenHeavenV9::sampleHeavenLayers(int worldX, int worldZ) const {
    std::array<HeavenIslandColumn, HEAVEN_LAYER_COUNT> layers;
    for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer)
        layers[static_cast<size_t>(layer)] =
            sampleHeavenLayer(worldX, worldZ, layer);
    return layers;
}

BlockId FrozenHeavenV9::heavenSurfaceBlock(HeavenBiome biome) {
    switch (biome) {
        case HeavenBiome::SunstoneHeights: return BlockId::SUNSTONE;
        case HeavenBiome::MoonpearlTerrace: return BlockId::MOONSTONE;
        case HeavenBiome::StarCrystalGarden: return BlockId::AETHER_MOSS;
        case HeavenBiome::GlimmerFen: return BlockId::GLIMMER_SILT;
        case HeavenBiome::SkystoneBarrens: return BlockId::SKYSTONE;
        default: return BlockId::AETHER_GRASS;
    }
}

std::vector<FrozenHeavenV9::HeavenLodFeature>
FrozenHeavenV9::sampleHeavenEcologyFeatures(int worldX, int worldZ) const {
    std::vector<HeavenLodFeature> features;
    if (!isHeaven()) return features;
    constexpr uint64_t domain = 0x48455645434F5639ULL;
    const auto layers = sampleHeavenLayers(worldX, worldZ);
    const int cellX = floorDiv(worldX, 8), cellZ = floorDiv(worldZ, 8);
    const int anchorX = cellX * 8 + 4, anchorZ = cellZ * 8 + 4;
    const int dx = std::abs(worldX - anchorX), dz = std::abs(worldZ - anchorZ);
    for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer) {
        const auto& island = layers[static_cast<size_t>(layer)];
        if (!island.present) continue;
        // Spawn standing area remains clear; shrines are stamped after ecology.
        if (layer == 2 && std::abs(worldX-m_heavenSpawnBlock.x) <= 3 &&
            std::abs(worldZ-m_heavenSpawnBlock.z) <= 3) continue;
        const int patch = hashPercent(m_seed ^ domain, cellX, layer, cellZ);
        const int roll = hashPercent(m_seed ^ domain, worldX, 19 + layer, worldZ);
        const bool clustered = dx <= 2 && dz <= 2 && patch < 45;
        const int density = layer == 2 ? 36 : layer == 4 ? 8 : 20;
        const auto add = [&](BlockId block, int bottom, int top, bool replace = false) {
            if (island.top + top >= Config::WORLD_MAX_Y ||
                island.top + bottom < HEAVEN_LAYERS[static_cast<size_t>(layer)].baseY)
                return;
            features.push_back({layer, bottom, top, block, replace, island.biome});
        };
        switch (island.biome) {
            case HeavenBiome::DawnMeadow:
            case HeavenBiome::SkyrootGrove:
            case HeavenBiome::CloudbloomFields:
                if (clustered) {
                    add(BlockId::AETHER_MOSS, 0, 0, true);
                    if (roll < density) add(roll < density/3 ? BlockId::CLOUDBERRY_BUSH :
                        island.biome == HeavenBiome::SkyrootGrove ? BlockId::SKY_FERN :
                        BlockId::DAWN_BELL, 1, 1);
                }
                break;
            case HeavenBiome::StarCrystalGarden:
                if (clustered && roll < density)
                    add(roll < density/2 ? BlockId::SKY_FERN : BlockId::STAR_CRYSTAL, 1, 1);
                break;
            case HeavenBiome::GlimmerFen: {
                // A shallow 3x3 pool requires a complete same-height floor and
                // a surrounding retaining ring. No source can drain into void.
                bool pool = dx <= 1 && dz <= 1 && patch < 12;
                const auto center = sampleHeavenLayer(anchorX, anchorZ, layer);
                if (pool && center.present && center.top == island.top &&
                    center.biome == HeavenBiome::GlimmerFen) {
                    for (int ox=-2; ox<=2 && pool; ++ox)
                        for (int oz=-2; oz<=2; ++oz) {
                            const auto neighbor=sampleHeavenLayer(anchorX+ox,anchorZ+oz,layer);
                            if (!neighbor.present || neighbor.top != center.top ||
                                neighbor.bottom >= center.top-1 ||
                                neighbor.biome != HeavenBiome::GlimmerFen) { pool=false; break; }
                        }
                } else pool=false;
                if (pool) add(BlockId::WATER, 0, 0, true);
                else if (clustered && roll < density) add(BlockId::GLIMMER_REED, 1, 1);
                break;
            }
            case HeavenBiome::MoonpearlTerrace:
                if (clustered && roll < density) add(BlockId::MOONFLOWER, 1, 1);
                break;
            case HeavenBiome::SunstoneHeights:
            case HeavenBiome::SkystoneBarrens:
                if (dx <= 1 && dz <= 1 && patch < 12)
                    add(island.biome == HeavenBiome::SkystoneBarrens ? BlockId::SKYSTONE :
                        BlockId::SUNSTONE, 1, dx+dz == 0 ? 3 : 1);
                break;
        }
        if ((island.biome == HeavenBiome::SkyrootGrove ||
             island.biome == HeavenBiome::CloudbloomFields) && roll < 10) {
            const int bottom = island.bottom - island.top;
            add(BlockId::HANGING_CLOUD_VINE, bottom-2, bottom-1);
        }
    }
    return features;
}

std::vector<FrozenHeavenV9::HeavenLodFeature>
FrozenHeavenV9::sampleHeavenLodFeatures(int worldX, int worldZ) const {
    std::vector<HeavenLodFeature> features;
    if (!isHeaven()) return features;

    const auto layers = sampleHeavenLayers(worldX, worldZ);
    const HeavenIslandColumn& mainIsland = layers[2];
    if (mainIsland.present && mainIsland.biome == HeavenBiome::SkyrootGrove &&
        hashPercent(m_seed ^ HEAVEN_DECOR_DOMAIN, worldX, 17, worldZ) < 10) {
        const int trunkHeight = 5 + hashPercent(
            m_seed ^ HEAVEN_DECOR_DOMAIN, worldX, 23, worldZ) % 4;
        features.push_back({2, 1, trunkHeight - 2, BlockId::SKYROOT_WOOD, false});
        features.push_back({2, trunkHeight - 1, trunkHeight,
                            BlockId::SKYROOT_LEAVES, false});
    }

    for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer) {
        const HeavenIslandColumn& island = layers[static_cast<size_t>(layer)];
        if (!island.present || island.top + 1 >= Config::WORLD_MAX_Y) continue;
        const int roll = hashPercent(
            m_seed ^ HEAVEN_DECOR_DOMAIN, worldX, 31 + layer * 7, worldZ);
        if (layer == HEAVEN_LAYER_COUNT - 1) {
            if (roll < 60)
                features.push_back({layer, 1, 1, BlockId::STAR_CRYSTAL, false});
            continue;
        }
        const float layerScale = layer == 2 ? 1.0f : 0.6f;
        const auto rollUnder = [&](int salt, int threshold) {
            return hashPercent(m_seed ^ HEAVEN_DECOR_DOMAIN,
                               worldX, salt + layer * 11, worldZ) <
                   static_cast<int>(threshold * layerScale);
        };
        const auto add = [&](BlockId block, int bottom = 1, int top = 1) {
            features.push_back({layer, bottom, top, block, false});
        };
        const auto replaceSurface = [&](BlockId block) {
            features.push_back({layer, 0, 0, block, true});
        };
        switch (island.biome) {
            case HeavenBiome::DawnMeadow:
                if (rollUnder(41, 4)) add(BlockId::STARFLOWER);
                else if (rollUnder(53, 9)) replaceSurface(BlockId::MOSS);
                else if (rollUnder(61, 2)) {
                    add(BlockId::SKYROOT_WOOD, 1, 1);
                    add(BlockId::SKYROOT_LEAVES, 2, 2);
                } else if (roll < static_cast<int>(20 * layerScale))
                    add(BlockId::STARFLOWER);
                break;
            case HeavenBiome::SunstoneHeights:
                if (rollUnder(41, 5)) {
                    const int height = 1 + hashPercent(
                        m_seed ^ HEAVEN_DECOR_DOMAIN,
                        worldX, 71 + layer * 11, worldZ) % 3;
                    add(BlockId::SUNSTONE, 1, height);
                } else if (rollUnder(53, 8)) add(BlockId::SUNSTONE, 1, 2);
                else if (rollUnder(61, 10)) add(BlockId::STAR_CRYSTAL);
                break;
            case HeavenBiome::StarCrystalGarden:
                if (rollUnder(41, 3)) {
                    const int height = 2 + hashPercent(
                        m_seed ^ HEAVEN_DECOR_DOMAIN,
                        worldX, 71 + layer * 11, worldZ) % 2;
                    add(BlockId::STAR_CRYSTAL, 1, height);
                } else if (rollUnder(53, 6)) add(BlockId::MOSS, 1, 2);
                else if (rollUnder(61, 14)) add(BlockId::STARFLOWER);
                else if (roll < static_cast<int>(9 * layerScale))
                    add(BlockId::STAR_CRYSTAL);
                else if (roll < static_cast<int>(20 * layerScale))
                    add(BlockId::STARFLOWER);
                break;
            case HeavenBiome::CloudbloomFields:
                if (rollUnder(41, 5)) add(BlockId::CLOUDSTONE);
                else if (rollUnder(53, 8)) replaceSurface(BlockId::MOSS);
                else if (roll < static_cast<int>(22 * layerScale))
                    add(BlockId::CLOUD_BLOOM);
                break;
            case HeavenBiome::SkystoneBarrens:
                if (rollUnder(41, 7)) {
                    const int height = 2 + hashPercent(
                        m_seed ^ HEAVEN_DECOR_DOMAIN,
                        worldX, 71 + layer * 11, worldZ) % 3;
                    add(BlockId::SUNSTONE, 1, height);
                } else if (rollUnder(53, 5)) add(BlockId::CLOUDSTONE);
                else if (rollUnder(61, 6)) replaceSurface(BlockId::AETHER_SOIL);
                else if (roll < static_cast<int>(9 * layerScale))
                    add(BlockId::STAR_CRYSTAL);
                break;
            case HeavenBiome::GlimmerFen:
                if (rollUnder(41, 3)) add(BlockId::GLOWSHROOM);
                else if (rollUnder(53, 6)) add(BlockId::MOSS, 1, 2);
                else if (rollUnder(61, 12)) add(BlockId::STAR_CRYSTAL);
                else if (roll < static_cast<int>(12 * layerScale))
                    add(BlockId::GLOWSHROOM);
                break;
            case HeavenBiome::MoonpearlTerrace:
                if (rollUnder(41, 2)) add(BlockId::STAR_CRYSTAL);
                else if (rollUnder(53, 8)) add(BlockId::STARFLOWER);
                else if (rollUnder(61, 6)) replaceSurface(BlockId::AETHER_SOIL);
                else if (roll < static_cast<int>(11 * layerScale))
                    add(BlockId::STARFLOWER);
                else if (roll < static_cast<int>(16 * layerScale))
                    add(BlockId::STAR_CRYSTAL);
                break;
            case HeavenBiome::SkyrootGrove:
                break;
        }
    }
    for (HeavenLodFeature& feature : features)
        feature.biome = layers[static_cast<size_t>(feature.layer)].biome;
    const auto ecology = sampleHeavenEcologyFeatures(worldX, worldZ);
    features.insert(features.end(), ecology.begin(), ecology.end());
    return features;
}

FrozenHeavenV9::HeavenIslandColumn FrozenHeavenV9::sampleHeavenIsland(
    int worldX, int worldZ) const {
    // The main layer (L3) drives spawn, trees, and landmark anchoring.
    return sampleHeavenLayer(worldX, worldZ, 2);
}

std::optional<LocatedStructure> FrozenHeavenV9::heavenStructureForCell(
    StructureType type, int cellX, int cellZ) const {
    if (!isHeaven() || !isHeavenStructure(type)) return {};
    if (type == StructureType::SkywayShrine) {
        const auto skyway = heavenSkywayForCell(cellX, cellZ);
        if (!skyway) return {};
        return LocatedStructure{skyway->worldX, skyway->mainFloorY,
                                skyway->worldZ, type};
    }

    int cellSize = 0;
    int margin = 0;
    int span = 0;
    int chancePercent = 0;
    uint64_t domain = 0;
    int fallbackLayer = -1;
    int retrySalt = 0;
    switch (type) {
        case StructureType::XiguangRuin:
            cellSize = HEAVEN_LANDMARK_CELL;
            margin = 24;
            span = 144;
            chancePercent = 24;
            domain = HEAVEN_LANDMARK_DOMAIN;
            retrySalt = 2;
            break;
        case StructureType::StarCrystalGeode:
            cellSize = HEAVEN_GEODE_CELL;
            margin = 16;
            span = 128;
            chancePercent = 28;
            domain = HEAVEN_GEODE_DOMAIN;
            fallbackLayer = 1;
            retrySalt = 3;
            break;
        case StructureType::CloudspireTower:
            cellSize = HEAVEN_SPIRE_CELL;
            margin = 16;
            span = 128;
            chancePercent = 20;
            domain = HEAVEN_SPIRE_DOMAIN;
            fallbackLayer = 3;
            retrySalt = 4;
            break;
        default:
            return {};
    }

    const uint64_t cellSeed = WorldGenContext::hashPosition(
        m_seed ^ domain, cellX, 0, cellZ);
    if (cellSeed % 100u >= static_cast<uint64_t>(chancePercent)) return {};

    HeavenIslandColumn anchor;
    int anchorX = 0;
    int anchorZ = 0;
    for (int attempt = 0; attempt < 24; ++attempt) {
        const uint64_t candidateSeed = attempt == 0 ? cellSeed :
            WorldGenContext::hashPosition(cellSeed, attempt, retrySalt, -attempt);
        anchorX = cellX * cellSize + margin +
            static_cast<int>((candidateSeed >> 8) %
                             static_cast<uint64_t>(span));
        anchorZ = cellZ * cellSize + margin +
            static_cast<int>((candidateSeed >> 20) %
                             static_cast<uint64_t>(span));
        anchor = sampleHeavenIsland(anchorX, anchorZ);
        int chosenLayer = 2;

        bool eligible = false;
        if (type == StructureType::XiguangRuin) {
            eligible = anchor.present &&
                anchor.biome != HeavenBiome::SunstoneHeights &&
                anchor.biome != HeavenBiome::MoonpearlTerrace &&
                anchor.biome != HeavenBiome::SkystoneBarrens;
        } else {
            if (!anchor.present ||
                (type == StructureType::StarCrystalGeode &&
                 anchor.biome != HeavenBiome::StarCrystalGarden)) {
                anchor = sampleHeavenLayer(anchorX, anchorZ, fallbackLayer);
                chosenLayer = fallbackLayer;
            }
            eligible = anchor.present &&
                (type != StructureType::StarCrystalGeode ||
                 anchor.biome == HeavenBiome::StarCrystalGarden);
        }
        if (eligible) {
            const int radius = type == StructureType::XiguangRuin ? 6 : 4;
            int supported = 0;
            int sampled = 0;
            for (int dz=-radius; dz<=radius; dz+=2) {
                for (int dx=-radius; dx<=radius; dx+=2) {
                    ++sampled;
                    const HeavenIslandColumn column = sampleHeavenLayer(
                        anchorX+dx, anchorZ+dz, chosenLayer);
                    if (column.present && std::abs(column.top-anchor.top)<=4)
                        ++supported;
                }
            }
            const int required = type == StructureType::CloudspireTower
                ? sampled * 4 / 5 : sampled * 2 / 3;
            if (supported >= required &&
                (type != StructureType::CloudspireTower ||
                 anchor.top + 28 < Config::WORLD_MAX_Y))
                return LocatedStructure{anchorX, anchor.top, anchorZ, type};
        }
    }
    return {};
}

std::optional<LocatedStructure> FrozenHeavenV9::locateNearestHeavenStructure(
    StructureType type, int worldX, int worldZ, int maximumDistance) const {
    if (!isHeaven() || !isHeavenStructure(type) || maximumDistance < 0)
        return {};

    const int cellSize = type == StructureType::XiguangRuin
        ? HEAVEN_LANDMARK_CELL
        : type == StructureType::SkywayShrine
            ? HEAVEN_SKYWAY_CELL : HEAVEN_GEODE_CELL;
    const int originCellX = floorDiv(worldX, cellSize);
    const int originCellZ = floorDiv(worldZ, cellSize);
    const int maximumRing = maximumDistance / cellSize + 2;
    const int64_t maximumDistanceSquared =
        static_cast<int64_t>(maximumDistance) * maximumDistance;
    int64_t bestDistanceSquared = maximumDistanceSquared + 1;
    std::optional<LocatedStructure> best;

    if (type == StructureType::SkywayShrine) {
        const int64_t dx = static_cast<int64_t>(m_originSkyway.worldX) - worldX;
        const int64_t dz = static_cast<int64_t>(m_originSkyway.worldZ) - worldZ;
        const int64_t distanceSquared = dx * dx + dz * dz;
        if (distanceSquared <= maximumDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            best = LocatedStructure{m_originSkyway.worldX,
                                    m_originSkyway.mainFloorY,
                                    m_originSkyway.worldZ, type};
        }
    }

    const auto inspect = [&](int offsetX, int offsetZ) {
        const auto candidate = heavenStructureForCell(
            type, originCellX + offsetX, originCellZ + offsetZ);
        if (!candidate) return;
        const int64_t dx = static_cast<int64_t>(candidate->worldX) - worldX;
        const int64_t dz = static_cast<int64_t>(candidate->worldZ) - worldZ;
        const int64_t distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > maximumDistanceSquared ||
            distanceSquared > bestDistanceSquared)
            return;
        if (distanceSquared == bestDistanceSquared && best &&
            (candidate->worldX > best->worldX ||
             (candidate->worldX == best->worldX &&
              candidate->worldZ >= best->worldZ)))
            return;
        bestDistanceSquared = distanceSquared;
        best = candidate;
    };

    for (int ring = 0; ring <= maximumRing; ++ring) {
        if (ring == 0) {
            inspect(0, 0);
        } else {
            for (int x = -ring; x <= ring; ++x) {
                inspect(x, -ring);
                inspect(x, ring);
            }
            for (int z = -ring + 1; z < ring; ++z) {
                inspect(-ring, z);
                inspect(ring, z);
            }
        }
        if (best) {
            const int64_t unvisitedLowerBound =
                static_cast<int64_t>(ring) * cellSize + 1;
            if (unvisitedLowerBound * unvisitedLowerBound >
                bestDistanceSquared)
                break;
        }
    }
    return best;
}

void FrozenHeavenV9::populateHeaven(
    Chunk& chunk, FrozenHeavenV9& generator,
    const StructureSetter& structureSetter) {
    const int baseX = chunk.worldX();
    const int baseZ = chunk.worldZ();
    const auto setLocal = [&](int worldX, int worldY, int worldZ, BlockId id) {
        if (!inChunk(worldX, worldZ, baseX, baseZ) ||
            !Config::isValidWorldY(worldY)) return;
        const int localX = worldX - baseX;
        const int localZ = worldZ - baseZ;
        chunk.blockAt(localX, worldY, localZ) = static_cast<uint16_t>(id);
    };
    const auto setIfAir = [&](int worldX, int worldY, int worldZ, BlockId id) {
        if (!inChunk(worldX, worldZ, baseX, baseZ) ||
            !Config::isValidWorldY(worldY)) return;
        const int localX = worldX - baseX;
        const int localZ = worldZ - baseZ;
        if (chunk.blockAt(localX, worldY, localZ) ==
            static_cast<uint16_t>(BlockId::AIR))
            chunk.blockAt(localX, worldY, localZ) = static_cast<uint16_t>(id);
    };
    const auto buildStructure = [&](const StructurePlacement& placement) {
        buildLandmark(placement,
            [&](int worldX,int worldY,int worldZ,BlockId id) {
                if (!inChunk(worldX,worldZ,baseX,baseZ) ||
                    !Config::isValidWorldY(worldY)) return;
                setLocal(worldX,worldY,worldZ,id);
                if ((id==BlockId::CHEST || id==BlockId::FURNACE) &&
                    structureSetter)
                    structureSetter(worldX,worldY,worldZ,id,
                        id==BlockId::CHEST
                            ? structureLootProfile(placement.type)
                            : StructureLootProfile::None,
                        WorldGenContext::hashPosition(
                            placement.variant,worldX,worldY,worldZ));
            });
    };
    const auto wantsSoil = [](HeavenBiome biome) {
        return biome == HeavenBiome::DawnMeadow ||
               biome == HeavenBiome::SkyrootGrove ||
               biome == HeavenBiome::CloudbloomFields;
    };
    const auto wantsVeins = [](HeavenBiome biome) {
        return biome == HeavenBiome::SunstoneHeights ||
               biome == HeavenBiome::MoonpearlTerrace;
    };
    const auto fillColumn = [&](int worldX, int worldZ,
                                const HeavenIslandColumn& island) {
        if (!island.present) return;
        for (int worldY = island.bottom; worldY <= island.top; ++worldY) {
            BlockId block = BlockId::CLOUDSTONE;
            if (worldY == island.top) {
                block = heavenSurfaceBlock(island.biome);
            } else if (wantsSoil(island.biome) && worldY >= island.top - 2) {
                block = BlockId::AETHER_SOIL;
            } else if (wantsVeins(island.biome) &&
                       hashPercent(generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                   worldX, worldY, worldZ) < 13) {
                block = BlockId::SUNSTONE;
            }
            if (worldY <= island.top-2 && worldY >= island.bottom+2 &&
                (island.biome == HeavenBiome::StarCrystalGarden ||
                 island.biome == HeavenBiome::SunstoneHeights ||
                 island.biome == HeavenBiome::MoonpearlTerrace) &&
                hashPercent(generator.m_seed ^ 0x4845564F52455639ULL,
                    floorDiv(worldX,3), floorDiv(worldY,2), floorDiv(worldZ,3)) < 9)
                block = BlockId::STAR_CRYSTAL_ORE;
            setLocal(worldX, worldY, worldZ, block);
        }
    };

    // Five altitude layers sample independently, so one column can hold up to
    // five stacked islands.  Their hard bottom clamps keep an air gap between
    // bands, and every layer carries its own exclusive biome.
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const auto layers = generator.sampleHeavenLayers(wx, wz);
            for (const HeavenIslandColumn& island : layers)
                fillColumn(wx, wz, island);
        }
    }

    // Skyroot trees are selected in world coordinates on the main layer and
    // each chunk writes only the part of a tree it owns. This keeps region
    // and singleton paths independent of request order while still allowing
    // canopies to cross a chunk boundary.
    for (int worldX = baseX - 5; worldX <= baseX + 20; ++worldX) {
        for (int worldZ = baseZ - 5; worldZ <= baseZ + 20; ++worldZ) {
            const HeavenIslandColumn island =
                generator.sampleHeavenIsland(worldX, worldZ);
            if (!island.present ||
                island.biome != HeavenBiome::SkyrootGrove ||
                hashPercent(generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                            worldX, 17, worldZ) >= 10)
                continue;
            const int trunkHeight = 5 + hashPercent(
                generator.m_seed ^ HEAVEN_DECOR_DOMAIN, worldX, 23, worldZ) % 4;
            for (int y = island.top + 1; y < island.top + trunkHeight; ++y)
                setIfAir(worldX, y, worldZ, BlockId::SKYROOT_WOOD);
            const int canopyY = island.top + trunkHeight - 1;
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    const int distance = std::abs(dx) + std::abs(dz);
                    if (distance > 3 || (distance == 3 && (dx + dz) % 2 != 0))
                        continue;
                    setIfAir(worldX + dx, canopyY, worldZ + dz,
                             BlockId::SKYROOT_LEAVES);
                    if (distance <= 1)
                        setIfAir(worldX + dx, canopyY + 1, worldZ + dz,
                                 BlockId::SKYROOT_LEAVES);
                }
            }
        }
    }

    // Treeless biomes each carry a small library of coordinate-owned
    // micro-features (rings, patches, boulders, spires, fallen logs) so their
    // surfaces and skyline stay varied without a new simulation system.
    // Every feature uses an independent hash salt and writes only through
    // setLocal/setIfAir, which keeps region and singleton output identical
    // regardless of request order.  Minor layers use a lower feature budget;
    // zenith islets keep their lone-crystal language.
    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            const int wx = baseX + x;
            const int wz = baseZ + z;
            const auto layers = generator.sampleHeavenLayers(wx, wz);
            for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer) {
                const HeavenIslandColumn& island =
                    layers[static_cast<size_t>(layer)];
                if (!island.present || island.top + 1 >= Config::WORLD_MAX_Y)
                    continue;
                const int top = island.top;
                const int roll = hashPercent(
                    generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                    wx, 31 + layer * 7, wz);
                if (layer == HEAVEN_LAYER_COUNT - 1) {
                    // Zenith islets carry a lone star crystal so the top
                    // layer reads clearly from below.
                    if (roll < 60)
                        setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                    continue;
                }
                // Full budget on the main layer, reduced on the drift layers.
                const float layerScale = layer == 2 ? 1.0f : 0.6f;
                const auto rollUnder = [&](int salt, int threshold) {
                    return hashPercent(generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                       wx, salt + layer * 11, wz) <
                           static_cast<int>(threshold * layerScale);
                };
                switch (island.biome) {
                    case HeavenBiome::DawnMeadow:
                        if (rollUnder(41, 4)) {
                            // Starflower ring with a centre bloom.
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                        setIfAir(wx + dx, top + 1, wz + dz,
                                                 BlockId::STARFLOWER);
                        } else if (rollUnder(53, 9)) {
                            setLocal(wx, top, wz, BlockId::MOSS);
                            setLocal(wx + 1, top, wz, BlockId::MOSS);
                            setLocal(wx, top, wz + 1, BlockId::MOSS);
                            setLocal(wx + 1, top, wz + 1, BlockId::MOSS);
                        } else if (rollUnder(61, 2)) {
                            for (int i = 0; i < 3; ++i)
                                setIfAir(wx + i, top + 1, wz,
                                         BlockId::SKYROOT_WOOD);
                            setIfAir(wx + 1, top + 2, wz,
                                     BlockId::SKYROOT_LEAVES);
                        } else if (roll < static_cast<int>(20 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                        }
                        break;
                    case HeavenBiome::SunstoneHeights:
                        if (rollUnder(41, 5)) {
                            const int height = 1 + hashPercent(
                                generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                wx, 71 + layer * 11, wz) % 3;
                            for (int y = 1; y <= height; ++y)
                                setIfAir(wx, top + y, wz, BlockId::SUNSTONE);
                        } else if (rollUnder(53, 8)) {
                            for (int dx = 0; dx <= 1; ++dx)
                                for (int dz = 0; dz <= 1; ++dz)
                                    for (int y = 1; y <= 2; ++y)
                                        setIfAir(wx + dx, top + y, wz + dz,
                                                 BlockId::SUNSTONE);
                        } else if (rollUnder(61, 10)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        }
                        break;
                    case HeavenBiome::StarCrystalGarden:
                        if (rollUnder(41, 3)) {
                            const int height = 2 + hashPercent(
                                generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                wx, 71 + layer * 11, wz) % 2;
                            for (int y = 1; y <= height; ++y)
                                setIfAir(wx, top + y, wz,
                                         BlockId::STAR_CRYSTAL);
                        } else if (rollUnder(53, 6)) {
                            setIfAir(wx, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx, top + 2, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 2, wz + 1, BlockId::MOSS);
                        } else if (rollUnder(61, 14)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx + 1, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx, top + 1, wz + 1, BlockId::STARFLOWER);
                        } else if (roll < static_cast<int>(9 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                            if (roll < static_cast<int>(4 * layerScale)) {
                                setIfAir(wx + 1, top + 1, wz,
                                         BlockId::STAR_CRYSTAL);
                                setIfAir(wx, top + 1, wz + 1,
                                         BlockId::STAR_CRYSTAL);
                            }
                        } else if (roll < static_cast<int>(20 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                        }
                        break;
                    case HeavenBiome::CloudbloomFields:
                        if (rollUnder(41, 5)) {
                            setIfAir(wx, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx, top + 1, wz + 1, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::CLOUDSTONE);
                        } else if (rollUnder(53, 8)) {
                            setLocal(wx, top, wz, BlockId::MOSS);
                            setLocal(wx + 1, top, wz, BlockId::MOSS);
                            setLocal(wx, top, wz + 1, BlockId::MOSS);
                            setLocal(wx + 1, top, wz + 1, BlockId::MOSS);
                        } else if (roll < static_cast<int>(22 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::CLOUD_BLOOM);
                            if (rollUnder(61, 40))
                                setIfAir(wx + 1, top + 1, wz,
                                         BlockId::CLOUD_BLOOM);
                            if (rollUnder(71, 40))
                                setIfAir(wx, top + 1, wz + 1,
                                         BlockId::CLOUD_BLOOM);
                            if (rollUnder(81, 20)) {
                                setIfAir(wx + 1, top + 1, wz + 1,
                                         BlockId::CLOUD_BLOOM);
                                setIfAir(wx - 1, top + 1, wz,
                                         BlockId::CLOUD_BLOOM);
                            }
                        }
                        break;
                    case HeavenBiome::SkystoneBarrens:
                        if (rollUnder(41, 7)) {
                            const int height = 2 + hashPercent(
                                generator.m_seed ^ HEAVEN_DECOR_DOMAIN,
                                wx, 71 + layer * 11, wz) % 3;
                            for (int y = 1; y <= height; ++y)
                                setIfAir(wx, top + y, wz, BlockId::SUNSTONE);
                        } else if (rollUnder(53, 5)) {
                            setIfAir(wx, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz, BlockId::CLOUDSTONE);
                            setIfAir(wx, top + 1, wz + 1, BlockId::CLOUDSTONE);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::CLOUDSTONE);
                        } else if (rollUnder(61, 6)) {
                            setLocal(wx, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx, top, wz + 1, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz + 1, BlockId::AETHER_SOIL);
                        } else if (roll < static_cast<int>(9 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        }
                        break;
                    case HeavenBiome::GlimmerFen:
                        if (rollUnder(41, 3)) {
                            setIfAir(wx, top + 1, wz, BlockId::GLOWSHROOM);
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                        setIfAir(wx + dx, top + 1, wz + dz,
                                                 BlockId::GLOWSHROOM);
                        } else if (rollUnder(53, 6)) {
                            setIfAir(wx, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz, BlockId::MOSS);
                            setIfAir(wx, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx + 1, top + 1, wz + 1, BlockId::MOSS);
                            setIfAir(wx, top + 2, wz, BlockId::MOSS);
                            setIfAir(wx + 1, top + 2, wz + 1, BlockId::MOSS);
                        } else if (rollUnder(61, 12)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        } else if (roll < static_cast<int>(12 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::GLOWSHROOM);
                            if (roll < static_cast<int>(5 * layerScale)) {
                                setIfAir(wx + 1, top + 1, wz,
                                         BlockId::GLOWSHROOM);
                                setIfAir(wx, top + 1, wz + 1,
                                         BlockId::GLOWSHROOM);
                                setIfAir(wx + 1, top + 1, wz + 1,
                                         BlockId::GLOWSHROOM);
                            }
                        }
                        break;
                    case HeavenBiome::MoonpearlTerrace:
                        if (rollUnder(41, 2)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                        setIfAir(wx + dx, top + 1, wz + dz,
                                                 BlockId::SUNSTONE);
                        } else if (rollUnder(53, 8)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx + 1, top + 1, wz, BlockId::STARFLOWER);
                            setIfAir(wx, top + 1, wz + 1, BlockId::STARFLOWER);
                        } else if (rollUnder(61, 6)) {
                            setLocal(wx, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz, BlockId::AETHER_SOIL);
                            setLocal(wx, top, wz + 1, BlockId::AETHER_SOIL);
                            setLocal(wx + 1, top, wz + 1, BlockId::AETHER_SOIL);
                        } else if (roll < static_cast<int>(11 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STARFLOWER);
                        } else if (roll < static_cast<int>(16 * layerScale)) {
                            setIfAir(wx, top + 1, wz, BlockId::STAR_CRYSTAL);
                        }
                        break;
                    case HeavenBiome::SkyrootGrove:
                        break;
                }
            }
        }
    }

    // Shared coordinate-owned ecology is applied after legacy decorations,
    // then structures restore their reserved floors and standing volumes.
    for (int x=0; x<Config::CHUNK_SIZE_X; ++x)
        for (int z=0; z<Config::CHUNK_SIZE_Z; ++z) {
            const int wx=baseX+x, wz=baseZ+z;
            const auto layers=generator.sampleHeavenLayers(wx,wz);
            for (const auto& feature : generator.sampleHeavenEcologyFeatures(wx,wz)) {
                const auto& island=layers[static_cast<size_t>(feature.layer)];
                for (int y=island.top+feature.bottomOffset; y<=island.top+feature.topOffset; ++y) {
                    const BlockId current=chunk.getBlock(x,y,z);
                    if (feature.replacesSurface || current==BlockId::AIR ||
                        isFrozenNaturalDecoration(current) || current==BlockId::STARFLOWER ||
                        current==BlockId::CLOUD_BLOOM || current==BlockId::GLOWSHROOM)
                        setLocal(wx,y,wz,feature.block);
                }
                if (feature.block==BlockId::WATER)
                    setLocal(wx,island.top+1,wz,BlockId::AIR);
            }
        }

    // Rare coordinate-owned shrines provide a long-distance landmark. The
    // anchor is selected from a wide cell, but every piece is written through
    // setIfAir so the exact same structure appears regardless of chunk order.
    const int firstCellX = floorDiv(baseX - 9, HEAVEN_LANDMARK_CELL);
    const int lastCellX = floorDiv(baseX + 24, HEAVEN_LANDMARK_CELL);
    const int firstCellZ = floorDiv(baseZ - 9, HEAVEN_LANDMARK_CELL);
    const int lastCellZ = floorDiv(baseZ + 24, HEAVEN_LANDMARK_CELL);
    for (int cellX = firstCellX; cellX <= lastCellX; ++cellX) {
        for (int cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ) {
            const uint64_t cellSeed = WorldGenContext::hashPosition(
                generator.m_seed ^ HEAVEN_LANDMARK_DOMAIN, cellX, 0, cellZ);
            const auto placement = generator.heavenStructureForCell(
                StructureType::XiguangRuin, cellX, cellZ);
            if (!placement) continue;
            const int ax=placement->worldX, az=placement->worldZ;
            buildStructure({0,0,placement->baseY,
                StructureType::XiguangRuin,cellSeed,
                ax-9,ax+9,az-9,az+9});
        }
    }

    // Crystal geode: a floating moss ring with a stacked crystal core marks
    // Starcrystal Garden anchors on the low and main island layers.
    const int firstGeodeCellX = floorDiv(baseX - 5, HEAVEN_GEODE_CELL);
    const int lastGeodeCellX = floorDiv(baseX + 20, HEAVEN_GEODE_CELL);
    const int firstGeodeCellZ = floorDiv(baseZ - 5, HEAVEN_GEODE_CELL);
    const int lastGeodeCellZ = floorDiv(baseZ + 20, HEAVEN_GEODE_CELL);
    for (int cellX = firstGeodeCellX; cellX <= lastGeodeCellX; ++cellX) {
        for (int cellZ = firstGeodeCellZ; cellZ <= lastGeodeCellZ; ++cellZ) {
            const auto placement = generator.heavenStructureForCell(
                StructureType::StarCrystalGeode, cellX, cellZ);
            if (!placement) continue;
            const uint64_t cellSeed=WorldGenContext::hashPosition(
                generator.m_seed^HEAVEN_GEODE_DOMAIN,cellX,0,cellZ);
            const int ax=placement->worldX, az=placement->worldZ;
            buildStructure({0,0,placement->baseY,
                StructureType::StarCrystalGeode,cellSeed,
                ax-5,ax+5,az-5,az+5});
        }
    }

    // Cloudspire tower: a sunstone shaft with a cloudstone cap and crystal
    // finial marks main and high island anchors.  Any biome is eligible so
    // that a normal exploration window reliably discovers the tower; the
    // cap-over-shaft order keeps test probes distinct from the ruin and
    // geode patterns.
    const int firstSpireCellX = floorDiv(baseX - 6, HEAVEN_SPIRE_CELL);
    const int lastSpireCellX = floorDiv(baseX + 21, HEAVEN_SPIRE_CELL);
    const int firstSpireCellZ = floorDiv(baseZ - 6, HEAVEN_SPIRE_CELL);
    const int lastSpireCellZ = floorDiv(baseZ + 21, HEAVEN_SPIRE_CELL);
    for (int cellX = firstSpireCellX; cellX <= lastSpireCellX; ++cellX) {
        for (int cellZ = firstSpireCellZ; cellZ <= lastSpireCellZ; ++cellZ) {
            const uint64_t cellSeed = WorldGenContext::hashPosition(
                generator.m_seed ^ HEAVEN_SPIRE_DOMAIN, cellX, 0, cellZ);
            const auto placement = generator.heavenStructureForCell(
                StructureType::CloudspireTower, cellX, cellZ);
            if (!placement) continue;
            const int ax=placement->worldX, az=placement->worldZ;
            buildStructure({0,0,placement->baseY,
                StructureType::CloudspireTower,cellSeed,
                ax-6,ax+6,az-6,az+6});
        }
    }

    // Skyway shrines keep all five destinations in one X/Z column, so the
    // destination chunk is already resident when the player changes height.
    // The main platform is anchored to a broad L3 island; the other four are
    // deliberately constructed safe landings in the otherwise empty bands.
    const auto placeSkyway = [&](const HeavenSkyway& skyway) {
        const uint64_t lootSeed = WorldGenContext::hashPosition(
            generator.m_seed ^ HEAVEN_SKYWAY_DOMAIN,
            skyway.worldX, skyway.mainFloorY, skyway.worldZ);
        for (int layer = 0; layer < HEAVEN_LAYER_COUNT; ++layer) {
            const int floorY = skyway.mainFloorY +
                HEAVEN_SKYWAY_OFFSETS[static_cast<size_t>(layer)];
            for (int dz = -3; dz <= 3; ++dz) {
                for (int dx = -3; dx <= 3; ++dx) {
                    setLocal(skyway.worldX + dx, floorY,
                             skyway.worldZ + dz,
                             ((std::abs(dx) + std::abs(dz) + layer) & 1)
                                 ? BlockId::CLOUDSTONE : BlockId::SUNSTONE);
                    if (std::abs(dx) < 3 && std::abs(dz) < 3) {
                        setLocal(skyway.worldX + dx, floorY + 1,
                                 skyway.worldZ + dz, BlockId::AIR);
                        setLocal(skyway.worldX + dx, floorY + 2,
                                 skyway.worldZ + dz, BlockId::AIR);
                    }
                    const bool edge = std::abs(dx) == 3 || std::abs(dz) == 3;
                    const bool doorway = (dx == 0 && std::abs(dz) == 3) ||
                                         (dz == 0 && std::abs(dx) == 3);
                    if (edge && !doorway)
                        setLocal(skyway.worldX + dx, floorY + 1,
                                 skyway.worldZ + dz, BlockId::CLOUDSTONE);
                }
            }
            setLocal(skyway.worldX, floorY + 1, skyway.worldZ,
                     BlockId::STAR_CRYSTAL);
            if (layer == 2) {
                const int chestX = skyway.worldX + 2;
                const int chestZ = skyway.worldZ;
                setLocal(chestX, floorY + 1, chestZ, BlockId::CHEST);
                if (structureSetter &&
                    inChunk(chestX, chestZ, baseX, baseZ)) {
                    structureSetter(chestX, floorY + 1, chestZ, BlockId::CHEST,
                        skyway.origin
                            ? StructureLootProfile::OriginSkywayShrine
                            : StructureLootProfile::SkywayShrine,
                        lootSeed);
                }
            }
        }
    };

    const HeavenSkyway originSkyway = generator.m_originSkyway;
    placeSkyway(originSkyway);
    const int firstSkywayCellX = floorDiv(baseX - 4, HEAVEN_SKYWAY_CELL);
    const int lastSkywayCellX = floorDiv(baseX + 19, HEAVEN_SKYWAY_CELL);
    const int firstSkywayCellZ = floorDiv(baseZ - 4, HEAVEN_SKYWAY_CELL);
    const int lastSkywayCellZ = floorDiv(baseZ + 19, HEAVEN_SKYWAY_CELL);
    for (int cellX = firstSkywayCellX; cellX <= lastSkywayCellX; ++cellX) {
        for (int cellZ = firstSkywayCellZ; cellZ <= lastSkywayCellZ; ++cellZ) {
            const auto skyway = generator.heavenSkywayForCell(cellX, cellZ);
            if (!skyway) continue;
            const int dx = skyway->worldX - originSkyway.worldX;
            const int dz = skyway->worldZ - originSkyway.worldZ;
            if (dx * dx + dz * dz < 24 * 24) continue;
            placeSkyway(*skyway);
        }
    }
    // Structures may clear terrain beneath a previously placed plant. Reconcile
    // new vegetation against final support, using the same rules as placement.
    for (int x=0; x<Config::CHUNK_SIZE_X; ++x)
        for (int z=0; z<Config::CHUNK_SIZE_Z; ++z) {
            const auto layers=generator.sampleHeavenLayers(baseX+x,baseZ+z);
            for (const auto& island:layers) {
                if (!island.present) continue;
                const int y=island.top+1;
                const BlockId plant=chunk.getBlock(x,y,z);
                if (plant>=BlockId::SKY_FERN && plant<=BlockId::CLOUDBERRY_BUSH &&
                    !supportsHeavenPlant(plant,chunk.getBlock(x,y-1,z)))
                    setLocal(baseX+x,y,baseZ+z,BlockId::AIR);
                for (int vineY=island.bottom-1; vineY>=island.bottom-2; --vineY)
                    if (chunk.getBlock(x,vineY,z)==BlockId::HANGING_CLOUD_VINE &&
                        chunk.getBlock(x,vineY+1,z)==BlockId::AIR)
                        setLocal(baseX+x,vineY,baseZ+z,BlockId::AIR);
            }
        }

    // The spawn selector operates on terrain columns before decoration.
    // Reserve its standing volume after every decorative/structure pass.
    setLocal(generator.m_heavenSpawnBlock.x,
             generator.m_heavenSpawnBlock.y + 1,
             generator.m_heavenSpawnBlock.z, BlockId::AIR);
    setLocal(generator.m_heavenSpawnBlock.x,
             generator.m_heavenSpawnBlock.y + 2,
             generator.m_heavenSpawnBlock.z, BlockId::AIR);

    for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            int maxY = Config::WORLD_MIN_Y - 1;
            for (int y = Config::WORLD_MAX_Y - 1;
                 y >= Config::WORLD_MIN_Y; --y) {
                if (chunk.blockAt(x, y, z) != static_cast<uint16_t>(BlockId::AIR)) {
                    maxY = y;
                    break;
                }
            }
            chunk.setColumnMaxY(x, z, maxY);
        }
    }
    chunk.finishBulkBlockEdit();
}
void FrozenHeavenV9::buildLandmark(const StructurePlacement& placement,const StructureWriter& write) {
    const int centerX = (placement.minX + placement.maxX) / 2;
    const int centerZ = (placement.minZ + placement.maxZ) / 2;
    const int rotations = static_cast<int>((placement.variant >> 61) & 3u);
    const bool mirror = ((placement.variant >> 60) & 1u) != 0;
    const auto transformOffset = [&](int dx, int dz) {
        if (mirror) dx = -dx;
        for (int turn = 0; turn < rotations; ++turn) {
            const int oldX = dx;
            dx = -dz;
            dz = oldX;
        }
        return std::pair<int, int>{dx, dz};
    };
    const auto transformDirection = [&](BedDirection direction) {
        const glm::ivec3 offset = bedDirectionOffset(direction);
        const auto [dx, dz] = transformOffset(offset.x, offset.z);
        if (dz < 0) return BedDirection::North;
        if (dx > 0) return BedDirection::East;
        if (dz > 0) return BedDirection::South;
        return BedDirection::West;
    };
    const StructureWriter transformedWrite = [&](int x, int y, int z,
                                                   BlockId id) {
        const auto [dx, dz] = transformOffset(x - centerX, z - centerZ);
        DoorState door;
        if(decodeDoor(id,door)) {
            door.direction=transformDirection(door.direction);
            if(mirror)door.rightHinge=!door.rightHinge;
            id=doorBlock(door);
        }
        ArchitecturalBlockState architecture;
        if (decodeArchitecturalBlock(id, architecture) &&
            architecture.shape == RenderShape::Stair) {
            id = stairBlock(architecture.material, architecture.half,
                            transformDirection(architecture.direction));
        } else if (isBed(id)) {
            BedPart part = BedPart::Foot;
            BedDirection direction = BedDirection::North;
            if (decodeBed(id, part, direction))
                id = bedBlock(part, transformDirection(direction));
        }
        const int worldX = centerX + dx;
        const int worldZ = centerZ + dz;
        write(worldX, y, worldZ, id);
    };
    switch(placement.type) {
    case StructureType::XiguangRuin: buildXiguangRuin(placement,transformedWrite); break;
    case StructureType::StarCrystalGeode: buildStarCrystalGeode(placement,transformedWrite); break;
    case StructureType::CloudspireTower: buildCloudspireTower(placement,transformedWrite); break;
    case StructureType::SkywayShrine: buildSkywayShrine(placement,transformedWrite); break;
    default: break;
    }
}
}
