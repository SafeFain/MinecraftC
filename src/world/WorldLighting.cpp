#include "world/WorldLighting.h"

#include "Config.h"
#include "world/Block.h"
#include "world/BlockLightLogic.h"
#include "world/Chunk.h"
#include "world/ChunkStore.h"
#include "world/World.h"

#include <algorithm>
#include <array>
#include <queue>
#include <unordered_set>
#include <vector>

void WorldLighting::rebuild() {
    std::queue<BlockLightNode> blockQueue;
    std::queue<BlockLightNode> skyQueue;
    m_chunks.withUnique([&](ChunkStore& store) {
        if (!m_lightDirty) return;
        auto findCell=[&](int wx,int y,int wz) -> std::pair<Chunk*,glm::ivec3> {
            if(!Config::isValidWorldY(y)) return {nullptr,glm::ivec3(0)};
            const int cx=World::worldToChunkX(wx),cz=World::worldToChunkZ(wz);
            Chunk* chunk=store.findUnlocked(cx,cz);
            if(chunk==nullptr||!chunk->generated.load()) return {nullptr,glm::ivec3(0)};
            return {chunk,{wx-cx*Config::CHUNK_SIZE_X,y,wz-cz*Config::CHUNK_SIZE_Z}};
        };

        bool hasSources = false;
        std::vector<Chunk*> initialized;
        std::unordered_set<Chunk*> lightChanged;
        store.forEachUniqueUnlocked([&](Chunk* chunk) {
            if (!chunk->generated.load()||chunk->lightingInitialized.load()) return;
            chunk->clearLight();
            initialized.push_back(chunk);
            lightChanged.insert(chunk);
        for (int z=0;z<Config::CHUNK_SIZE_Z;++z) for(int x=0;x<Config::CHUNK_SIZE_X;++x) {
            uint8_t vertical=15;
            for(int y=Config::WORLD_MAX_Y-1;y>=Config::WORLD_MIN_Y;--y) {
                const BlockId block=chunk->getBlock(x,y,z);
                const uint8_t damping=getLightDampening(block);
                if(damping>=15) vertical=0;
                else if(vertical>0 && damping>0)
                    vertical=static_cast<uint8_t>(vertical>damping?vertical-damping:0);
                if(vertical>0) {
                    chunk->setSkyLight(x,y,z,vertical);
                }
                const uint8_t emission=getLightEmission(block);
                if(emission>0) {
                    chunk->setBlockLight(x,y,z,emission);
                    blockQueue.push({chunk->worldX()+x,y,chunk->worldZ()+z,emission});
                    hasSources=true;
                }
            }
        }
        chunk->lightingInitialized=true;
        });

    // Direct columns are already complete.  Seed flood fill only along an
    // exposed horizontal frontier instead of queueing every open-sky voxel.
    constexpr int horizontal[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(Chunk* chunk:initialized){
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
            for(int z=0;z<Config::CHUNK_SIZE_Z;++z)
                for(int x=0;x<Config::CHUNK_SIZE_X;++x){
                    const uint8_t value=chunk->getSkyLight(x,y,z);if(value==0)continue;
                    const int wx=chunk->worldX()+x,wz=chunk->worldZ()+z;
                    bool frontier=false;
                    for(const auto& d:horizontal){auto [neighbor,p]=findCell(wx+d[0],y,wz+d[1]);
                        if(neighbor&&neighbor->getSkyLight(p.x,p.y,p.z)<value){frontier=true;break;}}
                    if(frontier)skyQueue.push({wx,y,wz,value});
                }
    }
    // Existing neighbor borders are also sources for a newly initialized
    // chunk, and vice versa.
    for(Chunk* chunk:initialized)for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
        for(int edge=0;edge<4;++edge)for(int i=0;i<Config::CHUNK_SIZE_X;++i){
            const int x=edge==0?0:edge==1?Config::CHUNK_SIZE_X-1:i;
            const int z=edge==2?0:edge==3?Config::CHUNK_SIZE_Z-1:i;
            const int wx=chunk->worldX()+x,wz=chunk->worldZ()+z;
            const int nx=wx+(edge==0?-1:edge==1?1:0);
            const int nz=wz+(edge==2?-1:edge==3?1:0);
            auto [neighbor,p]=findCell(nx,y,nz);if(!neighbor)continue;
            for(const auto& seed:std::array<std::pair<int,int>,2>{{{wx,wz},{nx,nz}}}){
                auto [source,sp]=findCell(seed.first,y,seed.second);if(!source)continue;
                const uint8_t sky=source->getSkyLight(sp.x,sp.y,sp.z);
                const uint8_t block=source->getBlockLight(sp.x,sp.y,sp.z);
                if(sky)skyQueue.push({seed.first,y,seed.second,sky});
                if(block)blockQueue.push({seed.first,y,seed.second,block});
            }
        }

    constexpr int directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto spread=[&](std::queue<BlockLightNode>& queue,bool sky) {
        while(!queue.empty()) {
            const auto node=queue.front();queue.pop();
            auto [origin,op]=findCell(node.x,node.y,node.z);
            if(!origin)continue;
            const uint8_t current=sky?origin->getSkyLight(op.x,op.y,op.z):
                                      origin->getBlockLight(op.x,op.y,op.z);
            if(current!=node.light||current==0)continue;
            for(const auto& d:directions) {
                const int nx=node.x+d[0],ny=node.y+d[1],nz=node.z+d[2];
                auto [target,p]=findCell(nx,ny,nz);if(!target)continue;
                const uint8_t damping=getLightDampening(target->getBlock(p.x,p.y,p.z));
                if(damping>=15)continue;
                uint8_t loss=static_cast<uint8_t>(std::max<int>(1,damping));
                if(sky&&d[1]==-1&&current==15&&damping==0)loss=0;
                const uint8_t next=current>loss?static_cast<uint8_t>(current-loss):0;
                uint8_t old=sky?target->getSkyLight(p.x,p.y,p.z):target->getBlockLight(p.x,p.y,p.z);
                if(next<=old)continue;
                if(sky)target->setSkyLight(p.x,p.y,p.z,next);
                else target->setBlockLight(p.x,p.y,p.z,next);
                lightChanged.insert(target);
                queue.push({nx,ny,nz,next});
            }
        }
    };
    spread(skyQueue,true);
    spread(blockQueue,false);
    for(Chunk* chunk:lightChanged)chunk->markDirty();
    m_lightHasSources = hasSources;
    m_lightDirty=false;
    });
}

void WorldLighting::updateLightingAt(const glm::ivec3& position) {
    struct RemovalNode { int x=0,y=0,z=0;uint8_t light=0; };
    m_chunks.withUnique([&](ChunkStore& store) {
        if(m_lightDirty)return;
        auto findCell=[&](int wx,int y,int wz)->std::pair<Chunk*,glm::ivec3>{
            if(!Config::isValidWorldY(y))return {nullptr,glm::ivec3(0)};
            const int cx=World::worldToChunkX(wx),cz=World::worldToChunkZ(wz);
            Chunk* chunk=store.findUnlocked(cx,cz);
            if(chunk==nullptr||!chunk->generated.load())
                return {nullptr,glm::ivec3(0)};
            return {chunk,{wx-cx*Config::CHUNK_SIZE_X,y,
                                      wz-cz*Config::CHUNK_SIZE_Z}};
        };
    std::unordered_set<Chunk*> changed;
    auto get=[&](int x,int y,int z,bool sky){auto [c,p]=findCell(x,y,z);
        return c?(sky?c->getSkyLight(p.x,p.y,p.z):c->getBlockLight(p.x,p.y,p.z)):uint8_t{0};};
    auto set=[&](int x,int y,int z,bool sky,uint8_t value){auto [c,p]=findCell(x,y,z);
        if(!c)return;
        const uint8_t old=sky?c->getSkyLight(p.x,p.y,p.z):c->getBlockLight(p.x,p.y,p.z);
        if(old==value)return;
        if(sky)c->setSkyLight(p.x,p.y,p.z,value);else c->setBlockLight(p.x,p.y,p.z,value);
        changed.insert(c);};
    constexpr int dirs[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto updateChannel=[&](bool sky,std::queue<RemovalNode>& removal,
                           std::queue<BlockLightNode>& addition){
        while(!removal.empty()){
            const auto node=removal.front();removal.pop();
            for(const auto& d:dirs){const int x=node.x+d[0],y=node.y+d[1],z=node.z+d[2];
                const uint8_t neighbor=get(x,y,z,sky);if(neighbor==0)continue;
                const bool descendingFullSky=sky&&d[1]==-1&&node.light==15&&neighbor==15;
                if(neighbor<node.light||descendingFullSky){set(x,y,z,sky,0);removal.push({x,y,z,neighbor});}
                else addition.push({x,y,z,neighbor});
            }
        }
        while(!addition.empty()){
            const auto node=addition.front();addition.pop();
            const uint8_t current=get(node.x,node.y,node.z,sky);
            if(current!=node.light||current==0)continue;
            for(const auto& d:dirs){const int x=node.x+d[0],y=node.y+d[1],z=node.z+d[2];
                auto [target,p]=findCell(x,y,z);if(!target)continue;
                const uint8_t damping=getLightDampening(target->getBlock(p.x,p.y,p.z));
                if(damping>=15)continue;
                uint8_t loss=static_cast<uint8_t>(std::max<int>(1,damping));
                if(sky&&d[1]==-1&&current==15&&damping==0)loss=0;
                const uint8_t next=current>loss?static_cast<uint8_t>(current-loss):0;
                if(next<=get(x,y,z,sky))continue;
                set(x,y,z,sky,next);addition.push({x,y,z,next});
            }
        }
    };

    std::queue<RemovalNode> blockRemoval,skyRemoval;
    std::queue<BlockLightNode> blockAddition,skyAddition;
    const uint8_t oldBlock=get(position.x,position.y,position.z,false);
    const auto [cell,local]=findCell(position.x,position.y,position.z);
    if(!cell)return;
    const uint8_t emission=getLightEmission(cell->getBlock(local.x,local.y,local.z));
    if(oldBlock>emission){set(position.x,position.y,position.z,false,emission);
        blockRemoval.push({position.x,position.y,position.z,oldBlock});}
    else if(emission>oldBlock)set(position.x,position.y,position.z,false,emission);
    if(emission>0)blockAddition.push({position.x,position.y,position.z,emission});
    for(const auto& d:dirs){const int x=position.x+d[0],y=position.y+d[1],z=position.z+d[2];
        const uint8_t value=get(x,y,z,false);if(value)blockAddition.push({x,y,z,value});}
    updateChannel(false,blockRemoval,blockAddition);

    uint8_t vertical=15;
    for(int y=Config::WORLD_MAX_Y-1;y>=Config::WORLD_MIN_Y;--y){
        auto [column,p]=findCell(position.x,y,position.z);if(!column)continue;
        const uint8_t damping=getLightDampening(column->getBlock(p.x,p.y,p.z));
        if(damping>=15)vertical=0;else if(vertical>0&&damping>0)
            vertical=static_cast<uint8_t>(vertical>damping?vertical-damping:0);
        const uint8_t old=get(position.x,y,position.z,true);
        if(old>vertical){set(position.x,y,position.z,true,vertical);
            skyRemoval.push({position.x,y,position.z,old});}
        else if(vertical>old)set(position.x,y,position.z,true,vertical);
        if(vertical>0)skyAddition.push({position.x,y,position.z,vertical});
    }
    for(const auto& d:dirs){const int x=position.x+d[0],y=position.y+d[1],z=position.z+d[2];
        const uint8_t value=get(x,y,z,true);if(value)skyAddition.push({x,y,z,value});}
    updateChannel(true,skyRemoval,skyAddition);
    for(Chunk* chunk:changed){chunk->markDirty();
        if(chunk->cx!=World::worldToChunkX(position.x)||chunk->cz!=World::worldToChunkZ(position.z))continue;
        if(local.x==0){Chunk* n=store.findUnlocked(chunk->cx-1,chunk->cz);if(n)n->markDirty();}
        if(local.x==Config::CHUNK_SIZE_X-1){Chunk* n=store.findUnlocked(chunk->cx+1,chunk->cz);if(n)n->markDirty();}
        if(local.z==0){Chunk* n=store.findUnlocked(chunk->cx,chunk->cz-1);if(n)n->markDirty();}
        if(local.z==Config::CHUNK_SIZE_Z-1){Chunk* n=store.findUnlocked(chunk->cx,chunk->cz+1);if(n)n->markDirty();}
    }
    });
}
