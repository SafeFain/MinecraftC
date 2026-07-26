#pragma once
#include "world/Block.h"

BlockId nextFarmlandState(BlockId farmland, BlockId blockAbove,
                          bool waterNearby, uint64_t randomValue);
BlockId nextCropState(BlockId crop, BlockId soil, uint64_t randomValue);
