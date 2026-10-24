#include "world/WorldGenerator.h"
#include "world/WorldGenContext.h"
#include <tuple>

namespace {
int portalCell(int value){const int q=value/192;return q-(value%192<0);}
}

std::optional<NetherStructure> WorldGenerator::overworldPortalForCell(int cx,int cz) const {
    if(m_dimension!=DimensionId::Overworld||m_worldType!=WorldType::Normal||customGenerator())return {};
    const auto seed=WorldGenContext::hashPosition(m_seed^0x5255494e504f5254ULL,cx,0,cz);
    if(seed%5)return {};
    const int x=cx*192+32+int((seed>>8)%128),z=cz*192+32+int((seed>>24)%128);
    const auto column=m_heightPipeline.sampleColumn(x,z);
    if(column.waterLevel>column.height||column.height<Config::WORLD_MIN_Y+4||column.height>Config::WORLD_MAX_Y-9)return {};
    return NetherStructure{NetherStructureKind::RuinedPortal,{x,column.height+1,z},seed};
}

std::vector<RegionGenerationData::PendingBlock> WorldGenerator::overworldPortalBlocks(
    int x,int z,int width,int depth) const {
    std::vector<RegionGenerationData::PendingBlock> blocks;
    if(m_dimension!=DimensionId::Overworld||m_worldType!=WorldType::Normal||customGenerator())return blocks;
    for(int cz=portalCell(z-5);cz<=portalCell(z+depth+4);++cz)
        for(int cx=portalCell(x-5);cx<=portalCell(x+width+4);++cx) {
            const auto portal=overworldPortalForCell(cx,cz);if(!portal)continue;
            const auto p=portal->anchor;
            for(int dz=-4;dz<=4;++dz)for(int dx=-5;dx<=5;++dx) {
                const int wx=p.x+dx,wz=p.z+dz;
                if(wx<x||wx>=x+width||wz<z||wz>=z+depth)continue;
                for(int dy=-1;dy<=6;++dy) {
                    const auto block=portal->block(wx,p.y+dy,wz);if(!block)continue;
                    RegionGenerationData::PendingBlock b;
                    b.worldX=wx;b.worldY=p.y+dy;b.worldZ=wz;b.id=*block;b.overwrite=true;
                    if(*block==BlockId::CHEST){b.needsBlockEntity=true;b.lootProfile=StructureLootProfile::RuinedPortal;b.lootSeed=portal->seed;b.structureKey=portal->key();b.structureRevision="1";}
                    blocks.push_back(std::move(b));
                }
            }
        }
    return blocks;
}

std::optional<NetherStructure> WorldGenerator::locateOverworldPortal(int x,int z,int radius) const {
    if(radius<0||radius>8192||std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000)return {};
    std::optional<NetherStructure> best;
    int64_t distance=int64_t(radius)*radius;
    for(int cz=portalCell(z-radius);cz<=portalCell(z+radius);++cz)
        for(int cx=portalCell(x-radius);cx<=portalCell(x+radius);++cx) {
            const auto portal=overworldPortalForCell(cx,cz);if(!portal)continue;
            const int64_t dx=int64_t(portal->anchor.x)-x,dz=int64_t(portal->anchor.z)-z,d=dx*dx+dz*dz;
            if(d>distance)continue;
            if(d==distance&&best&&std::tie(portal->anchor.x,portal->anchor.z)>=std::tie(best->anchor.x,best->anchor.z))continue;
            best=portal;distance=d;
        }
    return best;
}
