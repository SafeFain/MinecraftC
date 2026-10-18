#pragma once
#include "world/Noise.h"
#include "world/Chunk.h"
#include "world/StructureGenerator.h"
#include "FrozenSkywayPolicy.h"
#include "Config.h"
#include <array>
#include <optional>
#include <vector>
namespace FrozenHeavenReference {
class FrozenHeavenV9 {
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
    struct HeavenLodFeature {
        int layer = 0;
        int bottomOffset = 1;
        int topOffset = 1;
        BlockId block = BlockId::AIR;
        bool replacesSurface = false;
        HeavenBiome biome = HeavenBiome::DawnMeadow;
    };
    using HeavenSkyway = SkywayPolicy::HeavenSkyway;

    using StructureWriter = StructureGenerator::StructureWriter;
    static void buildLandmark(const StructurePlacement&,const StructureWriter&);
    using StructureSetter = std::function<void(int,int,int,BlockId,StructureLootProfile,uint64_t)>;
    explicit FrozenHeavenV9(uint64_t derivedSeed);
    uint64_t getSeed() const { return m_seed; }
    bool isHeaven() const { return true; }
    glm::ivec3 heavenSpawnBlock() const { return m_heavenSpawnBlock; }
    HeavenSkyway originHeavenSkyway() const { return m_originSkyway; }
    glm::ivec3 findHeavenSpawnBlock(int maximumRadius = 512) const;
    HeavenSkyway findOriginSkyway() const;
    std::optional<HeavenSkyway> heavenSkywayForCell(int,int) const;
    std::optional<glm::dvec3> heavenSkywayDestination(const glm::ivec3&,bool) const;
    HeavenBiome heavenBiomeAt(int,int) const;
    HeavenIslandColumn sampleHeavenLayer(int,int,int) const;
    std::array<HeavenIslandColumn,HEAVEN_LAYER_COUNT> sampleHeavenLayers(int,int) const;
    static BlockId heavenSurfaceBlock(HeavenBiome);
    std::vector<HeavenLodFeature> sampleHeavenEcologyFeatures(int,int) const;
    std::vector<HeavenLodFeature> sampleHeavenLodFeatures(int,int) const;
    HeavenIslandColumn sampleHeavenIsland(int,int) const;
    std::optional<LocatedStructure> heavenStructureForCell(StructureType,int,int) const;
    std::optional<LocatedStructure> locateNearestHeavenStructure(StructureType,int,int,int=8192) const;
    static void populateHeaven(Chunk&,FrozenHeavenV9&,const StructureSetter& = {});
private:
    uint64_t m_seed;
    Noise m_noise;
    glm::ivec3 m_heavenSpawnBlock{0,200,0};
    HeavenSkyway m_originSkyway{5,0,200,true};
};
}
