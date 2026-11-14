#pragma once
#include "world/WorldGenContext.h"

inline bool isSlimeChunk(uint64_t seed,int chunkX,int chunkZ) {
    return WorldGenContext::hashPosition(seed^0x534c494d455f3233ULL,chunkX,0,chunkZ)%10==0;
}
inline unsigned slimeHealth(uint8_t size) {return size<=2?1u<<(2*size):0;}
inline unsigned slimeDamage(uint8_t size) {return size==0?0:size<=2?1u<<size:0;}
