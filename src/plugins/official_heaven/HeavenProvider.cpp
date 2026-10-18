#include "plugins/official_heaven/HeavenProvider.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <memory>

namespace OfficialHeaven {
HeavenGenerator& generator(uint64_t seed) {
    // Bounded, thread-local memoization contains only pure derived state.
    thread_local std::vector<std::unique_ptr<HeavenGenerator>> cache;
    for(auto& g:cache)if(g->getSeed()==seed)return *g;
    if(cache.size()==4)cache.erase(cache.begin());
    cache.push_back(std::make_unique<HeavenGenerator>(seed));return *cache.back();
}
namespace {
int cell(int x,int size){const int q=x/size;return x%size<0?q-1:q;}
const std::vector<uint16_t>& baseChunk(uint64_t seed,int cx,int cz) {
    struct Entry {uint64_t seed=0;int x=0,z=0;std::vector<uint16_t> blocks;};
    thread_local std::array<Entry,4> cache;
    thread_local size_t next=0;
    for(const auto& entry:cache)if(!entry.blocks.empty()&&entry.seed==seed&&entry.x==cx&&entry.z==cz)return entry.blocks;
    thread_local auto chunk=std::make_unique<Chunk>(cx,cz);
    chunk->cx=cx;chunk->cz=cz;
    std::fill_n(&chunk->blockAt(0,Config::WORLD_MIN_Y,0),Config::CHUNK_VOLUME,uint16_t(0));
    HeavenGenerator::populateHeaven(*chunk,generator(seed),{},false);
    auto& entry=cache[next];next=(next+1)%cache.size();
    entry.seed=seed;entry.x=cx;entry.z=cz;chunk->copyRawBlocks(entry.blocks);
    return entry.blocks;
}
int32_t voxel(const MC_GenerationContext* c,int32_t x,int32_t y,int32_t z,uint16_t* out,void*) {
    if(!c||!out||!Config::isValidWorldY(y))return -1;
    const int cx=cell(x,16),cz=cell(z,16);
    *out=baseChunk(c->seed,cx,cz)[size_t(x-cx*16+(z-cz*16)*16+(y-Config::WORLD_MIN_Y)*256)];return 0;
}
int32_t batch(const MC_GenerationContext* c,const MC_GenerationVolume* v,uint16_t* out,uint32_t capacity,void*) {
    if(!c||!v||!out||uint64_t(v->width)*v->height*v->depth>capacity||
       v->y<Config::WORLD_MIN_Y||int64_t(v->y)+v->height>Config::WORLD_MAX_Y)return -1;
    if(v->width==16&&v->depth==16&&v->x%16==0&&v->z%16==0) {
        const auto& blocks=baseChunk(c->seed,v->x/16,v->z/16);
        std::copy_n(blocks.data()+size_t(v->y-Config::WORLD_MIN_Y)*256,size_t(v->height)*256,out);
        return 0;
    }
    const int xEnd=v->x+int(v->width),zEnd=v->z+int(v->depth);
    for(int cz=cell(v->z,16);cz<=cell(zEnd-1,16);++cz)for(int cx=cell(v->x,16);cx<=cell(xEnd-1,16);++cx) {
        const auto& blocks=baseChunk(c->seed,cx,cz);
        for(uint32_t y=0;y<v->height;++y)
            for(int z=std::max(v->z,cz*16);z<std::min(zEnd,cz*16+16);++z) {
                const int x=std::max(v->x,cx*16),end=std::min(xEnd,cx*16+16);
                const auto source=size_t(x-cx*16+(z-cz*16)*16+(v->y+int(y)-Config::WORLD_MIN_Y)*256);
                const auto destination=size_t(x-v->x)+size_t(z-v->z)*v->width+size_t(y)*v->width*v->depth;
                std::copy_n(blocks.data()+source,end-x,out+destination);
            }
    }return 0;
}
int32_t column(const MC_GenerationContext* c,int32_t x,int32_t z,MC_SurfaceSample* out,void*) {
    if(!c||!out)return -1;
    const auto a=generator(c->seed).sampleHeavenIsland(x,z);
    constexpr std::array<Biome,8> biomes{{Biome::PLAINS,Biome::FOREST,Biome::MOUNTAINS,Biome::FLOWER_FOREST,Biome::MEADOW,Biome::STONY_SHORE,Biome::SWAMP,Biome::MOUNTAINS}};
    *out={sizeof(*out),a.top,Config::WORLD_MIN_Y-1,uint32_t(biomes[size_t(a.biome)])};return 0;
}
int32_t terrainLod(const MC_GenerationContext* c,const MC_LodCell* cell,MC_LodSpan* out,uint32_t capacity,uint32_t* count,void*) {
    if(!c||!cell||!out||!count)return -1;*count=0;
    if(cell->cell_size==1) {
        const int x=cell->x,z=cell->z,cx=OfficialHeaven::cell(x,16),cz=OfficialHeaven::cell(z,16);
        const auto& b=baseChunk(c->seed,cx,cz);
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y) {
            const auto block=b[size_t(x-cx*16+(z-cz*16)*16+(y-Config::WORLD_MIN_Y)*256)];
            if(!block)continue;
            if(*count&&out[*count-1].block==block&&out[*count-1].top==y-1)out[*count-1].top=y;
            else {if(*count==capacity)return -1;out[(*count)++]={y,y,block};}
        }return 0;
    }
    const auto layers=generator(c->seed).sampleHeavenLayers(cell->x+int(cell->cell_size/2),cell->z+int(cell->cell_size/2));
    for(const auto& a:layers)if(a.present){if(*count==capacity)return -1;out[(*count)++]={a.bottom,a.top,uint16_t(HeavenGenerator::heavenSurfaceBlock(a.biome))};}
    return 0;
}
int32_t decorationLod(const MC_GenerationContext* c,const MC_LodCell* cell,MC_LodSpan* out,uint32_t capacity,uint32_t* count,void*) {
    if(!c||!cell||!out||!count)return -1;*count=0;
    const int size=int(cell->cell_size);if(size==1||size>8)return 0;
    auto& g=generator(c->seed);const auto center=g.sampleHeavenLayers(cell->x+size/2,cell->z+size/2);
    std::array<std::vector<HeavenGenerator::HeavenLodFeature>,5> selected;
    const int samples=std::min(size,4);
    for(int z=0;z<samples;++z)for(int x=0;x<samples;++x) {
        std::array<std::vector<HeavenGenerator::HeavenLodFeature>,5> sampled;
        for(const auto& f:g.sampleHeavenLodFeatures(cell->x+(x*size+size/2)/samples,cell->z+(z*size+size/2)/samples))
            if(center[size_t(f.layer)].biome==f.biome)sampled[size_t(f.layer)].push_back(f);
        for(size_t layer=0;layer<5;++layer)if(center[layer].present&&selected[layer].empty()&&!sampled[layer].empty())selected[layer]=std::move(sampled[layer]);
    }
    // The v9 LOD renderer sorted terrain and decorations together. A hanging
    // feature sampled on a thinner island can overlap the center island;
    // replay that total order so the center terrain retains its old coverage.
    std::vector<MC_LodSpan> spans;
    for(const auto& a:center)if(a.present)spans.push_back({a.bottom,a.top,uint16_t(HeavenGenerator::heavenSurfaceBlock(a.biome))});
    for(size_t layer=0;layer<5;++layer)for(const auto& f:selected[layer]) {
        if(f.replacesSurface) {
            const int top=center[layer].top;
            const auto found=std::find_if(spans.begin(),spans.end(),[&](const auto& span){return span.top==top;});
            if(found!=spans.end()) {
                if(found->bottom<top){found->top=top-1;spans.push_back({top,top,uint16_t(f.block)});}
                else found->block=uint16_t(f.block);
            }
            continue;
        }
        const int bottom=center[layer].top+f.bottomOffset,top=std::min(Config::WORLD_MAX_Y-1,center[layer].top+f.topOffset);
        if(bottom<=top)spans.push_back({bottom,top,uint16_t(f.block)});
    }
    std::sort(spans.begin(),spans.end(),[](const auto& a,const auto& b){return a.bottom<b.bottom;});
    if(spans.size()>capacity)return -1;
    for(const auto& span:spans)out[(*count)++]=span;
    return 0;
}
struct StructureKind {StructureType type;const char *key,*loot;int grid,radius,order;uint64_t domain;};
const std::array<StructureKind,5> kinds{{
    {StructureType::XiguangRuin,"official_heaven:ruin","official_heaven:ruin_loot",192,9,10,0x4845565255494E53ULL},
    {StructureType::StarCrystalGeode,"official_heaven:geode","official_heaven:geode_loot",160,5,20,0x48455647454F4445ULL},
    {StructureType::CloudspireTower,"official_heaven:spire","official_heaven:spire_loot",160,6,30,0x4845565350495245ULL},
    {StructureType::SkywayShrine,"official_heaven:skyway_shrine","official_heaven:skyway_loot",256,3,40,0x534B595741595631ULL},
    {StructureType::None,"official_heaven:spawn_clearance","",16,0,50,0}
}};
int32_t placements(const MC_StructureContext* c,int32_t cx,int32_t cz,MC_StructureInstance* out,uint32_t capacity,uint32_t* count,void* user) {
    if(!c||!out||!count)return -1;*count=0;const auto& k=*static_cast<const StructureKind*>(user);auto& g=generator(c->generation.seed);
    const auto add=[&](int x,int y,int z,uint64_t variant,int order) {
        if(cell(x,k.grid)!=cx||cell(z,k.grid)!=cz)return true;
        if(*count==capacity)return false;out[(*count)++]={{x,y,z},variant,order};return true;
    };
    if(k.type==StructureType::None) {const auto p=g.heavenSpawnBlock();return add(p.x,p.y,p.z,0,0)?0:-1;}
    if(k.type==StructureType::SkywayShrine) {
        const auto origin=g.originHeavenSkyway();if(!add(origin.worldX,origin.mainFloorY,origin.worldZ,1,0))return -1;
        if(const auto a=g.heavenSkywayForCell(cx,cz)) {
            const int64_t dx=int64_t(a->worldX)-origin.worldX,dz=int64_t(a->worldZ)-origin.worldZ;
            if(dx*dx+dz*dz>=24*24&&!add(a->worldX,a->mainFloorY,a->worldZ,0,1))return -1;
        }return 0;
    }
    const auto a=g.heavenStructureForCell(k.type,cx,cz);if(!a)return 0;
    return add(a->worldX,a->baseY,a->worldZ,WorldGenContext::hashPosition(c->generation.seed^k.domain,cx,0,cz),0)?0:-1;
}
int32_t build(const MC_StructureContext* c,MC_StructureBlockV2* out,uint32_t capacity,uint32_t* count,void* user) {
    if(!c||!out||!count)return -1;*count=0;const auto& k=*static_cast<const StructureKind*>(user);bool overflow=false;
    const auto write=[&](int x,int y,int z,BlockId id) {
        if(!Config::isValidWorldY(y))return;
        if(*count==capacity){overflow=true;return;}
        const char* loot=id==BlockId::CHEST?(k.type==StructureType::SkywayShrine&&c->variant?"official_heaven:origin_loot":k.loot):nullptr;
        uint64_t seed=WorldGenContext::hashPosition(c->variant,x,y,z);
        if(k.type==StructureType::SkywayShrine)seed=WorldGenContext::hashPosition(c->generation.seed^k.domain,c->anchor[0],c->anchor[1],c->anchor[2]);
        out[(*count)++]={{x-c->anchor[0],y-c->anchor[1],z-c->anchor[2],uint16_t(id),loot},seed};
    };
    const int x=c->anchor[0],y=c->anchor[1],z=c->anchor[2];
    if(k.type==StructureType::None){write(x,y+1,z,BlockId::AIR);write(x,y+2,z,BlockId::AIR);}
    else if(k.type==StructureType::SkywayShrine)HeavenGenerator::buildSkyway({x,z,y,c->variant!=0},write);
    else HeavenGenerator::buildLandmark({0,0,y,k.type,c->variant,x-k.radius,x+k.radius,z-k.radius,z+k.radius},write);
    return overflow?-1:0;
}
}
int32_t sampleSpawn(const MC_GenerationContext* c,double* out,void*) {
    if(!c||!out)return -1;const auto p=generator(c->seed).heavenSpawnBlock();out[0]=p.x+.5;out[1]=p.y+1.01;out[2]=p.z+.5;return 0;
}
MC_GeneratorDefinitionV2 generatorDefinition() {
    MC_GeneratorDefinitionV2 d{};d.size=sizeof(d);d.base={sizeof(d.base),"official_heaven:heaven","Heaven","9",1,column,voxel,nullptr};
    d.lod_revision="heaven-v9-lod1";d.batch=batch;d.terrain_lod=terrainLod;d.decoration_lod=decorationLod;return d;
}
MC_GeneratorDefinitionV3 generatorDefinitionV3() {
    MC_GeneratorDefinitionV3 d{};d.size=sizeof(d);d.base=generatorDefinition();
    d.layers=[](const MC_GenerationContext* c,int32_t x,int32_t z,MC_GenerationLayer* out,uint32_t capacity,uint32_t* count,void*)->int32_t {
        if(!c||!out||!count||capacity<5)return -1;
        const auto layers=generator(c->seed).sampleHeavenLayers(x,z);*count=5;
        for(size_t i=0;i<5;++i)out[i]={layers[i].bottom,layers[i].top,uint32_t(layers[i].present),uint32_t(layers[i].biome)};
        return 0;
    };return d;
}
std::vector<MC_StructureDefinitionV2> structureDefinitions() {
    std::vector<MC_StructureDefinitionV2> result;
    for(const auto& k:kinds){MC_StructureDefinitionV2 d{};d.size=sizeof(d);d.key=d.name=k.key;d.revision="9";d.dimension=1;
        d.cell_size=k.grid;d.radius=k.radius;d.min_y=k.type==StructureType::SkywayShrine?-88:0;d.max_y=k.type==StructureType::SkywayShrine?90:32;
        d.order=k.order;d.placements=placements;d.build=build;d.userdata=const_cast<StructureKind*>(&k);result.push_back(d);}
    return result;
}
}
