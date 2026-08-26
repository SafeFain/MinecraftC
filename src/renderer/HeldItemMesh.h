#pragma once

#include "renderer/RenderDevice.h"

#include <array>

MeshData buildHeldCubeMesh(const std::array<int, 6>& tiles, int tilesPerSide,
                           bool atlasTilesAreVerticallyFlipped);
