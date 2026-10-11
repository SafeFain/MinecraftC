#pragma once

#include "renderer/RenderDevice.h"

#include <array>
#include "world/Block.h"

MeshData buildHeldCubeMesh(const std::array<int, 6>& tiles, int tilesPerSide,
                           bool atlasTilesAreVerticallyFlipped);

MeshData buildHeldBlockShapeMesh(BlockId block, int tilesPerSide);
