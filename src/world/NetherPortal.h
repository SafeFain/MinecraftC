#pragma once

#include "world/Block.h"
#include "game/GameRules.h"
#include <functional>

class World;

struct NetherPortalFrame {
    glm::ivec3 origin{0}; // lower-left interior
    int width=0,height=0;
    bool alongZ=false;
    glm::ivec3 at(int u,int v) const {
        return origin+glm::ivec3(alongZ?0:u,v,alongZ?u:0);
    }
    BlockId block() const { return alongZ?BlockId::NETHER_PORTAL_Z:BlockId::NETHER_PORTAL_X; }
};

namespace NetherPortal {
using Read=std::function<BlockId(const glm::ivec3&)>;
inline bool isPortal(BlockId block) {
    return block==BlockId::NETHER_PORTAL_X||block==BlockId::NETHER_PORTAL_Z;
}
std::optional<NetherPortalFrame> find(const Read&,glm::ivec3,bool alongZ,bool activeOnly=false);
bool ignite(World&,const glm::ivec3&);
void invalidateAround(World&,const glm::ivec3&);
glm::dvec3 scaledDestination(const glm::dvec3&,DimensionId source);
}
