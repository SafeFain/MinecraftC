#pragma once

#include "world/BiomeMap.h"
#include "world/Structure.h"
#include "world/WorldGenContext.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class HeightPipeline;

// ── Overworld structure generation ──────────────────────────────────────
// Order-independent structure placement for the normal overworld. Candidate
// anchors, chance, footprint and layout variant are pure functions of world
// cell coordinates, so a structure appears identically whether the area
// around it is generated as one region or as nine singleton chunks.
struct StructurePlacement {
    int localX = 0;    // anchor local to the requested window origin
    int localZ = 0;
    int baseY = 0;     // terrain height at the anchor
    StructureType type = StructureType::None;
    uint64_t variant = 0;                 // deterministic layout seed
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;  // world-coordinate footprint
};

struct LocatedStructure {
    int worldX = 0;
    int baseY = 0;
    int worldZ = 0;
    StructureType type = StructureType::None;
};

class StructureGenerator {
public:
    // Writes one block. The callback decides whether the block lands inside
    // the current unit (region / chunk) or becomes pending cross-boundary
    // work; pending writes must overwrite unconditionally.
    using StructureWriter =
        std::function<void(int worldX, int worldY, int worldZ, BlockId id)>;
    using SurfaceSampler = std::function<int(int worldX, int worldZ)>;

    StructureGenerator(uint64_t seed, const HeightPipeline& heightPipeline);

    // Anchors are restricted to [originX, originX+width) ×
    // [originZ, originZ+depth); footprints may extend beyond the window.
    void generateStructuresRegion(int originX, int originZ, int width, int depth,
                                  std::vector<StructurePlacement>& out) const;

    std::vector<StructurePlacement> generateStructures(int chunkWorldX,
                                                       int chunkWorldZ) const;

    // Finds the nearest accepted deterministic anchor without generating or
    // loading chunks. Search expands by placement cell and stops only after
    // no unvisited cell can contain a closer anchor.
    std::optional<LocatedStructure> locateNearest(
        StructureType type, int worldX, int worldZ,
        int maximumDistance = 8192) const;

    // Pure coordinate query: does a deterministic structure reservation cover
    // this column? Both generation paths use it for tree suppression so the
    // results stay identical regardless of region boundaries.
    bool reservationAt(int worldX, int worldZ) const;

    // Write every block of a placement through the writer. Pure function of
    // the placement, so region and singleton paths produce identical blocks.
    static void build(const StructurePlacement& placement,
                      const StructureWriter& write,
                      const SurfaceSampler& surfaceSampler = {});

private:
    struct TypeParams {
        StructureType type = StructureType::None;
        int cell = 64;
        int chancePercent = 10;
        int tolerance = 2;
        int maxBuildHeight = 8;
    };
    struct Candidate {
        int x = 0;
        int z = 0;
        StructureType type = StructureType::None;
        uint64_t variant = 0;
        uint64_t priority = 0;
        bool chance = false;
        int minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    };

    WorldGenContext m_context;
    uint64_t m_structureSeed;
    const HeightPipeline& m_heightPipeline;

    static const TypeParams& params(StructureType type);
    static bool acceptsBiome(StructureType type, Biome biome);
    static int halfSize(StructureType type, uint64_t variant);
    static int maxHalfSize(StructureType type);
    static int floorDiv(int value, int divisor);

    Candidate candidateForCell(StructureType type, int cellX, int cellZ) const;
    bool accept(const Candidate& candidate) const;
    bool winsOverlapSpacing(const Candidate& candidate) const;
    bool terrainFits(const Candidate& candidate) const;
};
