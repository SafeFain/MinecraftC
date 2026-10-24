#include "world/NetherPortal.h"
#include "world/World.h"
#include "game/SaveStore.h"
#include <queue>
#include <set>
#include <tuple>

bool NetherPortal::ignite(World& world,const glm::ivec3& p) {
    if(world.dimension()!=DimensionId::Overworld&&world.dimension()!=DimensionId::Nether)return false;
    bool missing=false;
    Read read=[&](glm::ivec3 q) {auto b=world.getLoadedBlock(q.x,q.y,q.z);if(!b)missing=true;return b.value_or(BlockId::AIR);};
    for(bool alongZ:{false,true}) {
        missing=false;
        const auto frame=find(read,p,alongZ);
        if(!frame||missing)continue;
        for(int y=0;y<frame->height;++y)for(int x=0;x<frame->width;++x) {
            const auto q=frame->at(x,y);world.setBlock(q.x,q.y,q.z,frame->block());
        }
        world.indexNetherPortal(frame->origin);
        return true;
    }
    return false;
}

void NetherPortal::invalidateAround(World& world,const glm::ivec3& p) {
    std::queue<glm::ivec3> queue;
    std::set<std::tuple<int,int,int>> visited;
    for(const auto& d:FACE_OFFSETS)queue.push(p+d);
    while(!queue.empty()&&visited.size()<4096) {
        const auto q=queue.front();queue.pop();
        if(!visited.emplace(q.x,q.y,q.z).second)continue;
        const auto block=world.getBlock(q.x,q.y,q.z);
        if(!isPortal(block))continue;
        bool missing=false;
        const auto frame=find([&](glm::ivec3 r){const auto b=world.getLoadedBlock(r.x,r.y,r.z);if(!b)missing=true;return b.value_or(BlockId::AIR);},q,block==BlockId::NETHER_PORTAL_Z,true);
        if(frame||missing)continue;
        world.setBlock(q.x,q.y,q.z,BlockId::AIR);
        for(const auto& d:FACE_OFFSETS)queue.push(q+d);
    }
}


void World::indexNetherPortal(const glm::ivec3& p) {if(m_saveStore)m_saveStore->indexNetherPortal(p);}
std::vector<glm::ivec3> World::savedNetherPortalsNear(const glm::dvec3& p,int radius) const {
    return m_saveStore?m_saveStore->netherPortalsNear(p,radius):std::vector<glm::ivec3>{};
}
