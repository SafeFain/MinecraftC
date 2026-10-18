// Compatibility queries for existing tests only; independent acceptance uses
// tests/reference/heaven_v9 instead. No host runtime target compiles this file.
#include "world/WorldGenerator.h"
#include "plugins/official_heaven/HeavenProvider.h"
glm::ivec3 WorldGenerator::findHeavenSpawnBlock(int maximumRadius) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).findHeavenSpawnBlock(maximumRadius); }

WorldGenerator::HeavenSkyway WorldGenerator::findOriginSkyway() const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).findOriginSkyway(); }
std::optional<WorldGenerator::HeavenSkyway> WorldGenerator::heavenSkywayForCell(int x,int z) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).heavenSkywayForCell(x,z); }
std::optional<glm::dvec3> WorldGenerator::heavenSkywayDestination(const glm::ivec3& core,bool upward) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).heavenSkywayDestination(core,upward); }

WorldGenerator::HeavenBiome WorldGenerator::heavenBiomeAt(
    int worldX, int worldZ) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).heavenBiomeAt(worldX,worldZ); }

WorldGenerator::HeavenIslandColumn WorldGenerator::sampleHeavenLayer(
    int worldX, int worldZ, int layer) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).sampleHeavenLayer(worldX,worldZ,layer); }

std::array<WorldGenerator::HeavenIslandColumn,
           WorldGenerator::HEAVEN_LAYER_COUNT>
WorldGenerator::sampleHeavenLayers(int worldX, int worldZ) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).sampleHeavenLayers(worldX,worldZ); }

BlockId WorldGenerator::heavenSurfaceBlock(HeavenBiome biome) { return OfficialHeaven::HeavenGenerator::heavenSurfaceBlock(biome); }

std::vector<WorldGenerator::HeavenLodFeature>
WorldGenerator::sampleHeavenEcologyFeatures(int worldX, int worldZ) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).sampleHeavenEcologyFeatures(worldX,worldZ); }

std::vector<WorldGenerator::HeavenLodFeature>
WorldGenerator::sampleHeavenLodFeatures(int worldX, int worldZ) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).sampleHeavenLodFeatures(worldX,worldZ); }

WorldGenerator::HeavenIslandColumn WorldGenerator::sampleHeavenIsland(
    int worldX, int worldZ) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).sampleHeavenIsland(worldX,worldZ); }

std::optional<LocatedStructure> WorldGenerator::heavenStructureForCell(
    StructureType type, int cellX, int cellZ) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).heavenStructureForCell(type,cellX,cellZ); }

std::optional<LocatedStructure> WorldGenerator::locateNearestHeavenStructure(
    StructureType type, int worldX, int worldZ, int maximumDistance) const { if(!isHeaven())return {}; return OfficialHeaven::generator(m_seed).locateNearestHeavenStructure(type,worldX,worldZ,maximumDistance); }
glm::ivec3 WorldGenerator::heavenSpawnBlock() const {return OfficialHeaven::generator(m_seed).heavenSpawnBlock();}
WorldGenerator::HeavenSkyway WorldGenerator::originHeavenSkyway() const {return OfficialHeaven::generator(m_seed).originHeavenSkyway();}
