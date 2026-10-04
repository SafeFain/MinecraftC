#pragma once

#include "world/Structure.h"
#include <array>

struct OverworldStructureFixture {
    StructureType type;
    int x, y, z;
    int radius;
    int height;
};

// Actual accepted v18 anchors for seed 1234567890. Kept shared with the
// optional Vulkan scene so visual inspection exercises the tested terrain.
inline constexpr std::array<OverworldStructureFixture, 6> NEW_STRUCTURE_FIXTURES{{
    {StructureType::DesertTemple, -1656, 70, -3036, 10, 12},
    {StructureType::JungleRuins, -81, 90, -3796, 8, 9},
    {StructureType::SwampHut, 4879, 67, 4386, 6, 10},
    {StructureType::MountainWatchtower, -158, 133, -1227, 6, 18},
    {StructureType::StoneCircle, -3668, 123, 3291, 7, 7},
    {StructureType::AbandonedFarmstead, 19, 85, -398, 10, 9},
}};
