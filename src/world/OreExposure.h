#pragma once
#include "world/HeightPipeline.h"
#include "world/CaveGenerator.h"
#include <array>
#include <glm/glm.hpp>

// Sampling independent of loaded chunks prevents buried-ore seams at chunk and
// region boundaries. Called only for rare buried-lapis candidate voxels.
inline bool oreExposed(const HeightPipeline& height, const CaveGenerator& caves, int x, int y, int z, const CaveVolume* generated = nullptr) {
    std::vector<CaveColumnInfo> columns;
    columns.reserve(9);
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        const auto column = height.sampleColumn(x+dx,z+dz);
        columns.push_back({column.height,column.waterLevel,column.height < column.waterLevel || column.river});
    }
    const bool covered = generated && generated->contains(x-1,y,z) && generated->contains(x+1,y,z) &&
        generated->contains(x,y,z-1) && generated->contains(x,y,z+1);
    const auto fallback = covered ? CaveVolume{} : caves.generateVolume(x-1,z-1,3,3,columns);
    const auto& volume = covered ? *generated : fallback;
    for (const auto offset : {glm::ivec3(-1,0,0), glm::ivec3(1,0,0), glm::ivec3(0,-1,0), glm::ivec3(0,1,0), glm::ivec3(0,0,-1), glm::ivec3(0,0,1)}) {
        const auto& column = columns[static_cast<size_t>((offset.z+1)*3+offset.x+1)];
        if ((y+offset.y > column.surfaceY && y+offset.y > column.waterLevel) || volume.get(x+offset.x,y+offset.y,z+offset.z) == CaveCell::Air) return true;
    }
    return false;
}
