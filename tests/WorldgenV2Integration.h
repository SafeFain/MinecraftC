#pragma once
#include "plugins/PluginManager.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include "world/WorldGenerator.h"
#include "world/LodTerrainSystem.h"
#include "game/SaveStore.h"
#include "threading/ThreadPool.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <map>
#include <iostream>

namespace WorldgenV2Integration {
inline void check(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
inline uint16_t at(const std::vector<MC_LodSpan>& spans,int y) {
    for(const auto& s:spans)if(y>=s.bottom&&y<=s.top)return s.block;
    return 0;
}
inline bool same(const LodColumn& a,const LodColumn& b) {
    if(a.spans.size()!=b.spans.size())return false;
    for(size_t i=0;i<a.spans.size();++i) {
        const auto& x=a.spans[i];const auto& y=b.spans[i];
        if(x.bottom!=y.bottom||x.top!=y.top||x.block!=y.block)return false;
    }
    return true;
}
struct Counters {
    Plugins::GeneratorContent original;
    std::atomic<uint32_t> voxel{0},batch{0},terrain{0},decoration{0};
};
inline void instrument(Plugins::GeneratorContent& g,Counters& c) {
    c.original=g;g.userdata=&c;
    g.voxel=[](const MC_GenerationContext* ctx,int x,int y,int z,uint16_t* out,void* u)->int32_t {
        auto& c=*static_cast<Counters*>(u);++c.voxel;return c.original.voxel(ctx,x,y,z,out,c.original.userdata);
    };
    g.batch=[](const MC_GenerationContext* ctx,const MC_GenerationVolume* v,uint16_t* out,uint32_t cap,void* u)->int32_t {
        auto& c=*static_cast<Counters*>(u);++c.batch;return c.original.batch(ctx,v,out,cap,c.original.userdata);
    };
    g.terrainLod=[](const MC_GenerationContext* ctx,const MC_LodCell* v,MC_LodSpan* out,uint32_t cap,uint32_t* n,void* u)->int32_t {
        auto& c=*static_cast<Counters*>(u);++c.terrain;return c.original.terrainLod(ctx,v,out,cap,n,c.original.userdata);
    };
    g.decorationLod=[](const MC_GenerationContext* ctx,const MC_LodCell* v,MC_LodSpan* out,uint32_t cap,uint32_t* n,void* u)->int32_t {
        auto& c=*static_cast<Counters*>(u);++c.decoration;return c.original.decorationLod(ctx,v,out,cap,n,c.original.userdata);
    };
}
struct HeavenReference {
    WorldGenerator generator{123,WorldType::Normal,DimensionId::Heaven};
    std::map<std::pair<int,int>,std::vector<uint16_t>> chunks;
    std::map<std::tuple<int,int,int>,LodTileData> tiles;
};
// Avoid MSVC 14.51 /O2 miscompiling quotient/remainder adjustment in bulk loops.
inline int floorDiv(int x,int d) {return x<0?(x+1)/d-1:x/d;}
inline void heavenComparison() {
    HeavenReference reference;
    for(int z=-3;z<0;++z)for(int x=-3;x<0;++x) {
        auto c=std::make_unique<Chunk>(x,z);reference.generator.generate(*c);c->copyRawBlocks(reference.chunks[{x,z}]);
    }
    reference.tiles[{-1,-1,0}]=extractExactLodChunk(reference.chunks.at({-1,-1}));
    for(int level:{1,3,4}) {
        auto tile=buildApproximateLodTile(reference.generator,{-1,-1,uint8_t(level)});
        // Native Heaven decoration can overlap the base/other features. The
        // V2 terrain contract requires canonical disjoint material intervals;
        // compare the native paint order's voxel occupancy, preserving gaps.
        for(auto& column:tile.columns) {
            std::array<uint16_t,Config::WORLD_HEIGHT> blocks{};
            for(const auto& span:column.spans)for(int y=span.bottom;y<=span.top;++y)
                blocks[size_t(y-Config::WORLD_MIN_Y)]=uint16_t(span.block);
            column.spans.clear();
            for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y) {
                const auto block=static_cast<BlockId>(blocks[size_t(y-Config::WORLD_MIN_Y)]);
                if(block==BlockId::AIR)continue;
                if(!column.spans.empty()&&column.spans.back().top==y-1&&column.spans.back().block==block)
                    column.spans.back().top=int16_t(y);
                else column.spans.push_back({int16_t(y),int16_t(y),block});
            }
        }
        reference.tiles[{-1,-1,level}]=std::move(tile);
    }
    Plugins::GeneratorContent g;g.key="worldgen_v2_demo:heaven_reference";g.revision="9";g.dimension=1;
    g.lodRevision="reference-1";g.userdata=&reference;
    g.column=[](const MC_GenerationContext*,int x,int z,MC_SurfaceSample* out,void* u)->int32_t {
        const auto c=static_cast<HeavenReference*>(u)->generator.sampleTerrainColumn(x,z);
        out->height=c.height;out->water_level=c.waterLevel;out->biome=uint32_t(c.biome);return 0;
    };
    g.voxel=[](const MC_GenerationContext*,int x,int y,int z,uint16_t* out,void* u)->int32_t {
        const auto& blocks=static_cast<HeavenReference*>(u)->chunks.at({floorDiv(x,16),floorDiv(z,16)});
        *out=blocks[size_t(x-floorDiv(x,16)*16+(z-floorDiv(z,16)*16)*16+(y-Config::WORLD_MIN_Y)*256)];return 0;
    };
    g.batch=[](const MC_GenerationContext*,const MC_GenerationVolume* v,uint16_t* out,uint32_t,void* u)->int32_t {
        const auto& chunks=static_cast<HeavenReference*>(u)->chunks;
        for(uint32_t z=0;z<v->depth;++z)for(uint32_t x=0;x<v->width;++x) {
            const int wx=v->x+int(x),wz=v->z+int(z),cx=floorDiv(wx,16),cz=floorDiv(wz,16);
            const auto& blocks=chunks.at({cx,cz});
            for(uint32_t y=0;y<v->height;++y)out[x+z*v->width+y*v->width*v->depth]=
                blocks[size_t(wx-cx*16+(wz-cz*16)*16+(v->y+int(y)-Config::WORLD_MIN_Y)*256)];
        }
        return 0;
    };
    // A test adapter feeds the immutable native reference to the generic span
    // path. It is not a runtime Heaven provider or a new generation algorithm.
    g.terrainLod=[](const MC_GenerationContext*,const MC_LodCell* cell,MC_LodSpan* out,uint32_t cap,uint32_t* count,void* u)->int32_t {
        int level=0;while((1u<<level)<cell->cell_size)++level;
        const int side=16*int(cell->cell_size),tx=floorDiv(cell->x,side),tz=floorDiv(cell->z,side);
        const auto& tile=static_cast<HeavenReference*>(u)->tiles.at({tx,tz,level});
        const auto& spans=tile.at((cell->x-tx*side)/int(cell->cell_size),(cell->z-tz*side)/int(cell->cell_size)).spans;
        if(spans.size()>cap)return -1;
        *count=uint32_t(spans.size());
        for(size_t i=0;i<spans.size();++i)out[i]={spans[i].bottom,spans[i].top,uint16_t(spans[i].block)};
        return 0;
    };
    Plugins::mutableContentState().extensions.generators.emplace(g.key,g);
    WorldGenerator adapter(123,WorldType::Normal,DimensionId::Heaven,{g.key,"9","{}",{}});
    std::vector<std::unique_ptr<Chunk>> owned;std::vector<Chunk*> chunks;
    for(int z=-3;z<0;++z)for(int x=-3;x<0;++x){owned.push_back(std::make_unique<Chunk>(x,z));chunks.push_back(owned.back().get());}
    std::vector<RegionGenerationData::PendingBlock> pending;adapter.generateRegion(-3,-3,3,6,chunks,pending);
    for(auto* c:chunks) {
        std::vector<uint16_t> out;c->copyRawBlocks(out);check(out==reference.chunks.at({c->cx,c->cz}),"Heaven v9 batch reference mismatch");
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
            check(adapter.samplePluginBlock(c->worldX(),y,c->worldZ())==c->getBlock(0,y,0),"Heaven v9 point authority mismatch");
    }
    for(const auto& pair:reference.tiles) {
        const auto level=uint8_t(std::get<2>(pair.first));const auto tile=buildApproximateLodTile(adapter,{-1,-1,level});
        for(size_t i=0;i<tile.columns.size();++i)check(same(tile.columns[i],pair.second.columns[i]),"Heaven v9 generic LOD occupancy reference mismatch");
    }
    Plugins::mutableContentState().extensions.generators.erase(g.key);
}
inline int run(const std::filesystem::path& assets,const std::filesystem::path& module,const std::filesystem::path& badModule) {
    for(int divisor:{1,2,3,16,48,256})for(unsigned offset=0;offset<256;++offset) {
        const int value=-128+int(offset);
        check(floorDiv(value,divisor)==int(std::floor(double(value)/divisor)),"negative reference cell division mismatch");
    }
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-worldgen-v2-tests";
    std::filesystem::remove_all(root);const auto package=root/"mods"/"worldgen_v2_demo";
    std::filesystem::create_directories(package);std::filesystem::copy_file(module,package/module.filename());
    {std::ofstream f(package/"mod.json");f<<"{\"id\":\"worldgen_v2_demo\",\"version\":\"1.0.0\",\"api\":1,\"network_compatibility\":\"worldgen-v2-1\",\"native\":{\"linux\":\""<<module.filename().string()<<"\",\"windows\":\""<<module.filename().string()<<"\",\"macos\":\""<<module.filename().string()<<"\"}}";}
    Plugins::PluginManager plugins({assets,root});plugins.initialize();
    check(Plugins::mutableContentState().extensions.generators.count("worldgen_v2_demo:islands"),"V2 native registration failed");
    check(plugins.extension(nullptr,MC_WORLDGEN_EXTENSION,1)&&plugins.extension(nullptr,MC_WORLDGEN_EXTENSION,2)&&
        plugins.extension(nullptr,MC_WORLDGEN_EXTENSION,3)&&!plugins.extension(nullptr,MC_WORLDGEN_EXTENSION,4),"V1/V2 extension discovery mismatch");
    auto& g=Plugins::mutableContentState().extensions.generators.at("worldgen_v2_demo:islands");Counters counters;instrument(g,counters);
    Plugins::GenerationConfig config{g.key,"1","{}",{{"worldgen_v2_demo:markers","1"}}};
    WorldGenerator generator(12,WorldType::Normal,DimensionId::Overworld,config);
    auto singleton=std::make_unique<Chunk>(-1,-1);generator.generate(*singleton);
    check(counters.batch==1&&counters.voxel==0,"singleton did not use exactly one bulk callback");
    std::vector<std::unique_ptr<Chunk>> owned;std::vector<Chunk*> chunks;
    for(int z=-3;z<0;++z)for(int x=-3;x<0;++x){owned.push_back(std::make_unique<Chunk>(x,z));chunks.push_back(owned.back().get());}
    std::vector<RegionGenerationData::PendingBlock> pending;std::reverse(chunks.begin(),chunks.end());
    const auto batchBegin=std::chrono::steady_clock::now();
    generator.generateRegion(-3,-3,3,6,chunks,pending);
    const auto batchTime=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-batchBegin).count();
    check(counters.batch==2&&counters.voxel==0,"region did not use exactly one bulk callback");
    std::vector<uint16_t> expected,out;singleton->copyRawBlocks(expected);owned.back()->copyRawBlocks(out);
    check(expected==out,"negative singleton/region mismatch");
    for(auto* chunk:chunks)for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
        const auto column=generator.samplePluginColumnBlocks(chunk->worldX()+x,chunk->worldZ()+z);
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
            check(column[size_t(y-Config::WORLD_MIN_Y)]==uint16_t(chunk->getBlock(x,y,z)),"point/bulk/modifier disagreement");
    }
    // Chunk owns large voxel/light arrays; keep fixtures off the Windows stack.
    g.batch=nullptr;auto legacy=std::make_unique<Chunk>(-1,-1);generator.generate(*legacy);legacy->copyRawBlocks(out);check(expected==out,"V1 generation fallback mismatch");
    const auto legacyBegin=std::chrono::steady_clock::now();
    generator.generateRegion(-3,-3,3,6,chunks,pending);
    const auto legacyTime=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-legacyBegin).count();
    std::cout<<"3x3 native generation: batch "<<batchTime<<" ms (1 callback), V1 "<<legacyTime<<" ms (884736 voxel callbacks)\n";
    g.batch=[](const MC_GenerationContext* c,const MC_GenerationVolume* v,uint16_t* o,uint32_t n,void* u)->int32_t {
        auto& t=*static_cast<Counters*>(u);++t.batch;return t.original.batch(c,v,o,n,t.original.userdata);
    };
    const uint32_t batches=counters.batch;
    generator.generatePluginChunks({owned.front().get(),nullptr,owned.back().get()});
    check(counters.batch==batches+2,"sparse request did not use per-chunk batches");
    auto concurrent=[&]{auto c=std::make_unique<Chunk>(-1,-1);generator.generate(*c);std::vector<uint16_t> b;c->copyRawBlocks(b);return b;};
    auto a=std::async(std::launch::async,concurrent),b=std::async(std::launch::async,concurrent);
    check(a.get()==expected&&b.get()==expected,"concurrent generation mismatch");
    WorldGenerator other(13,WorldType::Normal,DimensionId::Overworld,config);auto different=std::make_unique<Chunk>(-1,-1);other.generate(*different);different->copyRawBlocks(out);
    check(out!=expected,"different seed did not vary bulk terrain");
    counters.voxel=0;counters.terrain=0;counters.decoration=0;
    for(uint8_t level:{uint8_t(0),uint8_t(1),uint8_t(3),uint8_t(4)}) {
        const LodTileKey key{-1,-1,level};const auto tile=buildApproximateLodTile(generator,key);
        const auto edges=sampleApproximateLodNeighborEdges(generator,key);
        const int cell=1<<level,side=16*cell;
        for(int sideIndex=0;sideIndex<4;++sideIndex) {
            const int dx=sideIndex==2?1:sideIndex==3?-1:0,dz=sideIndex==0?-1:sideIndex==1?1:0;
            const auto neighbor=buildApproximateLodTile(generator,{-1+dx,-1+dz,level});
            for(int i=0;i<16;++i)check(same(edges[sideIndex][i],neighbor.at(sideIndex==2?0:sideIndex==3?15:i,
                sideIndex==0?15:sideIndex==1?0:i)),"tile/neighbor sampling mismatch");
        }
        if(!level)for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
            std::vector<MC_LodSpan> spans;for(const auto& s:tile.at(x,z).spans)spans.push_back({s.bottom,s.top,uint16_t(s.block)});
            for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)check(at(spans,y)==uint16_t(singleton->getBlock(x,y,z)),"LOD level zero/exact mismatch");
        }
        const auto spans=generator.samplePluginLodColumn(-side,-side,cell);check(spans.size()>=10,"five island layers collapsed");
        const auto mesh=buildLodTileMesh(tile,cell,384,nullptr,&edges);
        check(mesh.opaqueIndexCount+mesh.translucentIndexCount==mesh.indexCount&&mesh.translucentIndexOffset==mesh.opaqueIndexCount,"LOD mesh layer handoff mismatch");
    }
    check(counters.voxel==0&&counters.terrain>0&&counters.decoration>0,"LOD callbacks fell back to voxel scans");
    // Revision identity, exact fallback, AIR overwrite and callback faults.
    const auto identity=generator.lodSamplingFingerprint();g.lodRevision="2";
    check(identity!=generator.lodSamplingFingerprint(),"LOD revision missing from cache identity");g.lodRevision="1";
    auto& modifier=Plugins::mutableContentState().extensions.modifiers.at(config.modifiers.front().first);
    modifier.lodRevision="2";check(identity!=generator.lodSamplingFingerprint(),"modifier LOD revision missing from identity");modifier.lodRevision="1";
    const auto fast=generator.samplePluginLodColumn(-15,-1,1);
    const auto savedTerrain=g.terrainLod,savedDecoration=g.decorationLod,savedModifier=modifier.decorationLod;
    g.terrainLod=nullptr;g.decorationLod=nullptr;modifier.decorationLod=nullptr;
    const auto fallback=generator.samplePluginLodColumn(-15,-1,1);
    for(int y=-64;y<320;++y)check(at(fast,y)==at(fallback,y),"LOD legacy fallback differs");
    g.terrainLod=savedTerrain;g.decorationLod=savedDecoration;modifier.decorationLod=savedModifier;
    g.decorationLod=[](const MC_GenerationContext*,const MC_LodCell*,MC_LodSpan* o,uint32_t,uint32_t* n,void*)->int32_t {*n=1;o[0]={-64,319,0};return 0;};
    auto noModifiers=config;noModifiers.modifiers.clear();WorldGenerator clear(12,WorldType::Normal,DimensionId::Overworld,noModifiers);
    check(clear.samplePluginLodColumn(0,0,1).empty(),"AIR decoration did not clear terrain");g.decorationLod=savedDecoration;
    const auto savedBatch=g.batch;
    for(int fault=0;fault<3;++fault) {
        if(fault==0)g.batch=[](const MC_GenerationContext*,const MC_GenerationVolume*,uint16_t*,uint32_t,void*)->int32_t{return -1;};
        if(fault==1)g.batch=[](const MC_GenerationContext*,const MC_GenerationVolume*,uint16_t*,uint32_t,void*)->int32_t{return 0;};
        if(fault==2)g.batch=[](const MC_GenerationContext*,const MC_GenerationVolume*,uint16_t*,uint32_t,void*)->int32_t{throw std::runtime_error("fixture");};
        bool refused=false;try{generator.generate(*singleton);}catch(...){refused=true;}
        singleton->copyRawBlocks(out);check(refused&&out==expected,"bad bulk result altered published terrain");
    }
    g.batch=savedBatch;
    for(int fault=0;fault<4;++fault) {
        g.terrainLod=[](const MC_GenerationContext*,const MC_LodCell*,MC_LodSpan*,uint32_t cap,uint32_t* n,void*)->int32_t {*n=cap+1;return 0;};
        if(fault==1)g.terrainLod=[](const MC_GenerationContext*,const MC_LodCell*,MC_LodSpan* o,uint32_t,uint32_t* n,void*)->int32_t {*n=1;o[0]={-65,0,1};return 0;};
        if(fault==2)g.terrainLod=[](const MC_GenerationContext*,const MC_LodCell*,MC_LodSpan* o,uint32_t,uint32_t* n,void*)->int32_t {*n=2;o[0]={0,10,1};o[1]={5,12,1};return 0;};
        if(fault==3)g.terrainLod=[](const MC_GenerationContext*,const MC_LodCell*,MC_LodSpan* o,uint32_t,uint32_t* n,void*)->int32_t {*n=1;o[0]={0,1,MC_INVALID_ID};return 0;};
        bool refused=false;try{buildApproximateLodTile(generator,{0,0,0});}catch(...){refused=true;}check(refused,"invalid LOD spans accepted");
    }
    g.terrainLod=savedTerrain;
    auto edited=std::make_unique<Chunk>(-1,-1);generator.generate(*edited);edited->setBlock(8,10,8,BlockId::DIAMOND_ORE);
    std::vector<uint16_t> editedBlocks;edited->copyRawBlocks(editedBlocks);
    {
        SaveStore store(root/"exact-cache");ThreadPool pool(1);LodTerrainSystem system;
        system.setSaveStore(&store);system.reset(&generator);system.setThreadPool(&pool);
        LanLodTestAccess::observeExact(system,*edited);pool.waitIdle();system.setThreadPool(nullptr);system.processCompleted(nullptr);
        check(system.hasExactChunk(-1,-1),"edited exact column cache was not saved");
    }
    g.lodRevision="new-approximation";
    {
        SaveStore store(root/"exact-cache");ThreadPool pool(1);LodTerrainSystem system;
        system.setSaveStore(&store);system.reset(&generator);
        system.configure({true,32,LodAggressiveness::PowerSaver,LodPrecision::Low});system.setThreadPool(&pool);
        LanLodTestAccess::requestOnlyTile(system,{-1,-1,0});pool.waitIdle();system.setThreadPool(nullptr);system.processCompleted(nullptr);
        check(LanLodTestAccess::residentExactMatches(system,{-1,-1,0},editedBlocks),"LOD-only revision discarded exact edited columns");
    }
    g.lodRevision="1";
    // A late error in a sparse request cannot publish its earlier chunk.
    g.batch=[](const MC_GenerationContext* ctx,const MC_GenerationVolume* v,uint16_t* out,uint32_t cap,void* u)->int32_t {
        if(v->x>0)return -1;
        auto& c=*static_cast<Counters*>(u);return c.original.batch(ctx,v,out,cap,c.original.userdata);
    };
    auto early=std::make_unique<Chunk>(0,0),late=std::make_unique<Chunk>(4,0);bool refused=false;
    try{generator.generatePluginChunks({early.get(),late.get()});}catch(...){refused=true;}
    check(refused&&!early->generated&&!late->generated&&early->getBlock(1,0,1)==BlockId::AIR,"late bulk fault published a partial request");
    g.batch=savedBatch;
    auto cacheRun=[&](const std::filesystem::path& path) {
        SaveStore store(path);ThreadPool pool(1);LodTerrainSystem system;
        system.setSaveStore(&store);system.reset(&generator);
        system.configure({true,32,LodAggressiveness::PowerSaver,LodPrecision::Low});
        system.setThreadPool(&pool);
        const uint32_t before=counters.terrain;
        system.update({0.5,80,0.5},8,{});pool.waitIdle();
        // processCompleted normally fills the next streaming window. Stop
        // enqueueing before measuring this one completed cache request.
        system.setThreadPool(nullptr);system.processCompleted(nullptr);
        check(system.residentCpuBytes()>0,"LOD cache roundtrip produced no resident data");
        const uint32_t sampled=uint32_t(counters.terrain)-before;
        check(LanLodTestAccess::residentApproximationsMatch(system,generator),"resident/cache path altered floating island spans");
        const auto tiles=system.residentTileCount();
        check(tiles>0,"cache fixture has no resident tiles");
        std::cout<<"LOD cache: "<<sampled<<" callbacks / "<<tiles<<" tiles\n";
        return sampled/uint32_t(tiles);
    };
    const uint32_t cold=cacheRun(root/"cache"),warm=cacheRun(root/"cache");
    check(cold>warm&&warm>0,"warm LOD cache did not avoid tile sampling");
    g.lodRevision="changed";check(cacheRun(root/"cache")==cold,"changed LOD revision reused a stale disk cache");g.lodRevision="1";
    modifier.lodRevision="changed";check(cacheRun(root/"cache")==cold,"changed modifier LOD revision reused stale disk cache");modifier.lodRevision="1";
    g.terrainLod=[](const MC_GenerationContext*,const MC_LodCell*,MC_LodSpan*,uint32_t,uint32_t*,void*)->int32_t{return -1;};
    {
        SaveStore store(root/"fault-cache");ThreadPool pool(1);LodTerrainSystem system;
        system.setSaveStore(&store);system.reset(&generator);
        system.configure({true,32,LodAggressiveness::PowerSaver,LodPrecision::Low});
        system.setThreadPool(&pool);
        system.update({0.5,80,0.5},8,{});pool.waitIdle();
        // processCompleted normally fills the next streaming window. Stop
        // enqueueing before measuring this one completed cache request.
        system.setThreadPool(nullptr);system.processCompleted(nullptr);
        check(system.residentCpuBytes()==0,"faulted LOD request published data");
        if(std::filesystem::exists(root/"fault-cache"))for(const auto& entry:std::filesystem::recursive_directory_iterator(root/"fault-cache"))
            check(entry.path().extension()!=".lod","faulted LOD request wrote a cache");
    }
    g.terrainLod=savedTerrain;
    {std::lock_guard<std::mutex> lock(Plugins::generationFaultMutex());Plugins::generationFault().clear();}
    Plugins::LootContent loot;loot.key="worldgen_v2_demo:loot";loot.revision="1";
    Plugins::mutableContentState().extensions.loot.emplace(loot.key,loot);
    Plugins::StructureContent structure;structure.key="worldgen_v2_demo:structure";structure.revision="1";
    structure.cell=16;structure.chance=100;structure.radius=1;structure.height=2;structure.tolerance=4;
    structure.blocks={{0,0,0,uint16_t(BlockId::CHEST),loot.key},{0,1,0,0,""}};
    Plugins::mutableContentState().extensions.structures.emplace(structure.key,structure);
    generator.generateRegion(-3,-3,3,6,chunks,pending);
    bool foundChest=false;
    for(auto* c:chunks) {
        auto single=std::make_unique<Chunk>(c->cx,c->cz);generator.generate(*single);
        std::vector<uint16_t> a,b;c->copyRawBlocks(a);single->copyRawBlocks(b);
        check(a==b&&c->pluginStructures.size()==single->pluginStructures.size(),"bulk structure overwrite/metadata mismatch");
        for(size_t i=0;i<c->pluginStructures.size();++i) {
            const auto& x=c->pluginStructures[i];const auto& y=single->pluginStructures[i];
            check(x.structureKey==y.structureKey&&x.structureRevision==y.structureRevision&&x.lootKey==y.lootKey&&
                x.lootRevision==y.lootRevision&&x.lootSeed==y.lootSeed,"bulk structure/loot identity mismatch");
            if(x.id==BlockId::CHEST){foundChest=true;check(x.needsBlockEntity&&x.lootKey==loot.key,"bulk chest metadata missing");}
        }
    }
    check(foundChest,"bulk structure fixture placed no chest");
    Plugins::mutableContentState().extensions.structures.erase(structure.key);Plugins::mutableContentState().extensions.loot.erase(loot.key);
    Plugins::ModifierContent clearing;clearing.key="worldgen_v2_demo:clearing";clearing.revision="1";clearing.lodRevision="1";
    clearing.decorate=[](const MC_GenerationContext*,int x,int z,MC_DecorationBlock* out,uint32_t cap,uint32_t* count,void*)->int32_t {
        if(cap<385)return -1;
        *count=0;for(int y=-64;y<320;++y)out[(*count)++]={x,y,z,0};
        out[(*count)++]={x,100,z,uint16_t(BlockId::WOOD)};return 0;
    };
    clearing.decorationLod=[](const MC_GenerationContext*,const MC_LodCell* cell,MC_LodSpan* out,uint32_t cap,uint32_t* count,void*)->int32_t {
        if(cap<2)return -1;
        *count=0;
        if((cell->x+int(cell->cell_size/2))%16||(cell->z+int(cell->cell_size/2))%16)return 0;
        out[(*count)++]={-64,319,0};out[(*count)++]={100,100,uint16_t(BlockId::WOOD)};return 0;
    };
    Plugins::mutableContentState().extensions.modifiers.emplace(clearing.key,clearing);
    WorldGenerator builtinModified(12,WorldType::Superflat,DimensionId::Overworld,{"","","{}",{{clearing.key,"1"}}});
    const auto painted=builtinModified.samplePluginLodColumn(0,0,2,{{0,5,uint16_t(BlockId::WOOD)},{3,8,uint16_t(BlockId::STONE)}});
    check(painted.size()==2&&painted.front().top==2&&at(painted,4)==uint16_t(BlockId::STONE),
        "overlapping builtin decoration lost its paint order during plugin composition");
    {
        SaveStore store(root/"builtin-modifier");ThreadPool pool(1);LodTerrainSystem system;
        system.setSaveStore(&store);system.reset(&builtinModified);
        system.configure({true,32,LodAggressiveness::PowerSaver,LodPrecision::Low});system.setThreadPool(&pool);
        system.update({0.5,80,0.5},8,{});pool.waitIdle();system.setThreadPool(nullptr);system.processCompleted(nullptr);
        check(system.residentTileCount()>0&&LanLodTestAccess::residentApproximationsMatch(system,builtinModified),
            "builtin height-field repair undid a plugin modifier's AIR clearing");
        auto c=std::make_unique<Chunk>(-1,-1);builtinModified.generate(*c);const auto spans=builtinModified.samplePluginLodColumn(-16,-16,1);
        for(int y=-64;y<320;++y)check(at(spans,y)==uint16_t(c->getBlock(0,y,0)),"builtin modifier point/LOD authority mismatch");
    }
    Plugins::mutableContentState().extensions.modifiers.erase(clearing.key);
    heavenComparison();
    Plugins::BlockContent plant;plant.key="worldgen_v2_demo:plant";plant.name="LOD plant";
    plant.properties.solid=false;plant.properties.shape=RenderShape::Cross;
    plant.properties.layer=RenderLayer::Cutout;
    Plugins::mutableContentState().blocks.emplace(4096,plant);
    LodTileData plantTile;plantTile.at(0,0).spans.push_back({300,300,static_cast<BlockId>(4096)});
    check(buildLodTileMesh(plantTile,2,6).indexCount>0,"custom fine-cell plant decoration was filtered as builtin-only");
    Plugins::mutableContentState().blocks.erase(4096);
    g=counters.original;
    plugins.shutdown();
    std::filesystem::copy_file(badModule,package/badModule.filename());
    {std::ofstream f(package/"mod.json");f<<"{\"id\":\"worldgen_v2_demo\",\"version\":\"1.0.0\",\"api\":1,\"native\":{\"linux\":\""<<badModule.filename().string()<<"\",\"windows\":\""<<badModule.filename().string()<<"\",\"macos\":\""<<badModule.filename().string()<<"\"}}";}
    Plugins::PluginManager rejected({assets,root});rejected.initialize();
    check(!Plugins::mutableContentState().extensions.generators.count("worldgen_v2_demo:islands")&&
        !Plugins::mutableContentState().extensions.modifiers.count("worldgen_v2_demo:markers"),"failed V2 registration did not roll back");
    rejected.shutdown();std::filesystem::remove_all(root);
    std::cout<<"Worldgen V2 native bulk, multilayer LOD, faults and Heaven reference passed\n";return 0;
}
}
