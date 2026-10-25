#pragma once
#include "plugins/official_heaven/HeavenGenerator.h"
#include "reference/heaven_v9/FrozenHeavenV9.h"
#include "reference/heaven_v9/FrozenHeavenLod.h"
#include "world/ChunkMeshBuilder.h"
#include <future>

inline void testHeavenMigration() {
    for (uint64_t seed : {123ULL, 987654321ULL, 0xffffffffffffffffULL}) {
        const uint64_t derived = WorldGenContext(seed).derive(0x484556454E5F5345ULL);
        FrozenHeavenReference::FrozenHeavenV9 reference(derived);
        OfficialHeaven::HeavenGenerator provider(derived);
        WorldGenerator runtime(seed,WorldType::Normal,DimensionId::Heaven);
        require(runtime.customGenerator(),"P1 runtime still bypasses registered generator");
        require(reference.heavenSpawnBlock()==provider.heavenSpawnBlock(),
                "P1 independent spawn reference changed");
        const auto spawn=reference.heavenSpawnBlock();
        std::set<std::pair<int,int>> positions{{-2,-2},{-1,-1},{0,0},{1,1}};
        const auto add=[&](int x,int z) {
            const int cx=static_cast<int>(std::floor(double(x)/16));
            const int cz=static_cast<int>(std::floor(double(z)/16));
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
                positions.emplace(cx+dx,cz+dz);
        };
        add(spawn.x,spawn.z);
        for(auto type:{StructureType::XiguangRuin,StructureType::StarCrystalGeode,
                       StructureType::CloudspireTower,StructureType::SkywayShrine}) {
            const auto a=reference.locateNearestHeavenStructure(type,-130,-130,4096);
            const auto b=provider.locateNearestHeavenStructure(type,-130,-130,4096);
            require(bool(a)==bool(b),"P1 independent locate availability changed");
            if(a) {
                require(a->worldX==b->worldX&&a->baseY==b->baseY&&a->worldZ==b->worldZ,
                        "P1 independent structure anchor changed");
                add(a->worldX,a->worldZ);
            }
        }
        for(const auto& position:positions) {
            const auto frozenLayers=reference.sampleHeavenLayers(position.first*16,position.second*16);
            const auto surface=runtime.sampleTerrainColumn(position.first*16,position.second*16);
            const auto main=reference.sampleHeavenIsland(position.first*16,position.second*16);
            require(surface.height==main.top&&surface.densityMaxY==main.top&&
                    surface.densityMinY==(main.present?main.bottom:Config::WORLD_MIN_Y),
                    "P1 surface density bounds differ from frozen main layer");
            const auto registeredLayers=runtime.sampleGenerationLayers(position.first*16,position.second*16);
            require(registeredLayers.size()==frozenLayers.size(),"P1 logical layer slots differ");
            for(size_t i=0;i<frozenLayers.size();++i) {
                const auto& a=frozenLayers[i];const auto& b=registeredLayers[i];
                require(a.bottom==b.bottom&&a.top==b.top&&uint32_t(a.present)==b.present&&uint32_t(a.biome)==b.biome,
                        "P1 logical layer differs from independent frozen reference");
            }
            for(int size:{2,4,8,16,32}) {
                const int x=position.first*16,z=position.second*16;
                const auto expected=FrozenHeavenReference::approximateLod(reference,x,z,size);
                const auto actual=runtime.samplePluginLodColumn(x,z,size);
                std::array<uint16_t,Config::CHUNK_SIZE_Y> expectedBlocks{},actualBlocks{};
                for(const auto& s:expected)for(int y=s.bottom;y<=s.top;++y)expectedBlocks[size_t(y-Config::WORLD_MIN_Y)]=s.block;
                for(const auto& s:actual)for(int y=s.bottom;y<=s.top;++y)actualBlocks[size_t(y-Config::WORLD_MIN_Y)]=s.block;
                if(expectedBlocks!=actualBlocks)for(size_t y=0;y<expectedBlocks.size();++y)if(expectedBlocks[y]!=actualBlocks[y]) {
                    std::cerr<<"P1 coarse mismatch seed="<<seed<<" cell="<<x<<","<<z<<" size="<<size
                             <<" y="<<int(y)+Config::WORLD_MIN_Y<<" expected="<<expectedBlocks[y]<<" actual="<<actualBlocks[y]<<'\n';break;
                }
                require(expectedBlocks==actualBlocks,"P1 coarse LOD differs from independent frozen reference");
            }
            auto a=std::make_unique<Chunk>(position.first,position.second);
            auto b=std::make_unique<Chunk>(position.first,position.second);
            FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*a,reference);
            OfficialHeaven::HeavenGenerator::populateHeaven(*b,provider);
            std::vector<uint16_t> left,right;a->copyRawBlocks(left);b->copyRawBlocks(right);
            require(left==right,"P1 independent full voxel reference changed");
            runtime.generate(*b);b->copyRawBlocks(right);
            if(left!=right) {
                for(size_t i=0;i<left.size();++i)if(left[i]!=right[i]) {
                    std::cerr<<"P1 mismatch seed="<<seed<<" chunk="<<position.first<<","<<position.second
                             <<" xyz="<<(i%16)<<","<<(i/256+Config::WORLD_MIN_Y)<<","<<((i/16)%16)
                             <<" expected="<<left[i]<<" actual="<<right[i]<<'\n';break;
                }
            }
            require(left==right,"P1 registered V2 output differs from independent native reference");
            for(const auto& local:std::array<std::pair<int,int>,5>{{{0,0},{15,0},{0,15},{15,15},{8,8}}}) {
                const int wx=position.first*16+local.first,wz=position.second*16+local.second;
                const auto column=runtime.samplePluginColumnBlocks(wx,wz);
                const auto lod=runtime.samplePluginLodColumn(wx,wz,1);
                std::array<uint16_t,Config::CHUNK_SIZE_Y> denseLod{};
                for(const auto& span:lod)for(int y=span.bottom;y<=span.top;++y)
                    denseLod[static_cast<size_t>(y-Config::WORLD_MIN_Y)]=span.block;
                for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y) {
                    const auto expected=static_cast<uint16_t>(a->getBlock(local.first,y,local.second));
                    require(column[static_cast<size_t>(y-Config::WORLD_MIN_Y)]==expected,
                            "P1 point column differs from independent native reference");
                    require(denseLod[static_cast<size_t>(y-Config::WORLD_MIN_Y)]==expected,
                            "P1 fine LOD differs from independent native reference");
                }
            }
        }
        std::vector<std::unique_ptr<Chunk>> region;
        std::vector<Chunk*> targets;
        const int cx=int(std::floor(double(spawn.x)/16))-1,cz=int(std::floor(double(spawn.z)/16))-1;
        for(int z=0;z<3;++z)for(int x=0;x<3;++x){region.push_back(std::make_unique<Chunk>(cx+x,cz+z));targets.push_back(region.back().get());}
        std::reverse(targets.begin(),targets.end());
        std::vector<RegionGenerationData::PendingBlock> pending;
        runtime.generateRegion(cx,cz,3,6,targets,pending);
        require(pending.empty(),"P1 region unexpectedly used another metadata channel");
        for(const auto& chunk:region) {
            auto expected=std::make_unique<Chunk>(chunk->cx,chunk->cz);
            FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*expected,reference);
            std::vector<uint16_t> a,b;expected->copyRawBlocks(a);chunk->copyRawBlocks(b);
            require(a==b,"P1 reversed region batch differs from frozen singleton");
        }
        std::vector<std::future<std::vector<uint16_t>>> concurrent;
        for(int i=0;i<4;++i)concurrent.push_back(std::async(std::launch::async,[&,i]{
            auto chunk=std::make_unique<Chunk>(cx+i-2,cz-i);runtime.generate(*chunk);
            std::vector<uint16_t> blocks;chunk->copyRawBlocks(blocks);return blocks;
        }));
        for(int i=3;i>=0;--i) {
            auto expected=std::make_unique<Chunk>(cx+i-2,cz-i);
            FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*expected,reference);
            std::vector<uint16_t> blocks;expected->copyRawBlocks(blocks);
            require(blocks==concurrent[size_t(i)].get(),"P1 concurrent completion order changed voxels");
        }
    }
}

inline void benchmarkHeavenMigration() {
    constexpr uint64_t seed=987654321;
    FrozenHeavenReference::FrozenHeavenV9 reference(WorldGenContext(seed).derive(0x484556454E5F5345ULL));
    WorldGenerator runtime(seed,WorldType::Normal,DimensionId::Heaven);
    std::array<double,5> nativeGeneration{},pluginGeneration{},nativeLod{},pluginLod{};
    std::array<double,5> nativeRegion{},pluginRegion{},nativeInitial{},pluginInitial{};
    std::array<double,5> nativeLoading{},pluginLoading{};
    uint64_t checksum=0;
    const auto measure=[](auto work){const auto start=std::chrono::steady_clock::now();work();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
    const auto& descriptor=Plugins::content().extensions.generators.at("official_heaven:heaven");
    const MC_GenerationContext context{sizeof(MC_GenerationContext),1,runtime.getSeed(),"{}"};
    std::vector<uint16_t> probeBlocks(16*16*Config::CHUNK_SIZE_Y);
    const auto baseCost=measure([&]{for(int z=-3;z<3;++z)for(int x=-3;x<3;++x){MC_GenerationVolume volume{sizeof(volume),x*16,Config::WORLD_MIN_Y,z*16,16,Config::CHUNK_SIZE_Y,16};require(descriptor.batch(&context,&volume,probeBlocks.data(),uint32_t(probeBlocks.size()),descriptor.userdata)==0,"benchmark batch failed");}});
    const auto structureCost=measure([&]{for(int z=-3;z<3;++z)for(int x=-3;x<3;++x)checksum+=runtime.pluginStructureBlocks(x*16,z*16).size();});
    const auto publishCost=measure([&]{for(int i=0;i<36;++i){auto chunk=std::make_unique<Chunk>(0,0);chunk->loadRawBlocks(probeBlocks);}});
    std::cout<<"benchmark components base_ms="<<baseCost<<" structure_ms="<<structureCost<<" publication_ms="<<publishCost<<'\n';
    for(int round=0;round<=5;++round) {
        const auto generation=[&](bool plugin) {
            return measure([&]{for(int z=-3;z<3;++z)for(int x=-3;x<3;++x) {
                auto chunk=std::make_unique<Chunk>(x,z);
                if(plugin)runtime.generate(*chunk);
                else FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*chunk,reference);
                checksum+=uint16_t(chunk->getBlock(8,100,8));
            }});
        };
        const auto lod=[&](bool plugin) {
            return measure([&]{for(int size:{2,4,8,16,32})for(int z=-8;z<8;++z)for(int x=-8;x<8;++x) {
                const auto spans=plugin?runtime.samplePluginLodColumn(x*size,z*size,size):FrozenHeavenReference::approximateLod(reference,x*size,z*size,size);
                for(const auto& span:spans)checksum+=uint64_t(span.block)+uint64_t(span.top-span.bottom+1);
            }});
        };
        const auto region=[&](bool plugin){return measure([&]{
            for(int rz=-3;rz<3;rz+=3)for(int rx=-3;rx<3;rx+=3) {
                std::vector<std::unique_ptr<Chunk>> chunks;std::vector<Chunk*> targets;
                for(int z=0;z<3;++z)for(int x=0;x<3;++x){chunks.push_back(std::make_unique<Chunk>(rx+x,rz+z));targets.push_back(chunks.back().get());}
                if(plugin){std::vector<RegionGenerationData::PendingBlock> pending;runtime.generateRegion(rx,rz,3,6,targets,pending);}
                else for(auto& chunk:chunks)FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*chunk,reference);
                for(const auto& chunk:chunks)checksum+=uint16_t(chunk->getBlock(8,100,8));
            }
        });};
        const auto initial=[&](bool plugin){return measure([&]{
            // A distinct seed per round gives both paths an empty seeded cache.
            const auto initialSeed=seed+uint64_t(round)+1;auto chunk=std::make_unique<Chunk>(-1,-1);
            if(plugin){WorldGenerator fresh(initialSeed,WorldType::Normal,DimensionId::Heaven);fresh.generate(*chunk);}
            else {FrozenHeavenReference::FrozenHeavenV9 fresh(WorldGenContext(initialSeed).derive(0x484556454E5F5345ULL));FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*chunk,fresh);}
            checksum+=uint16_t(chunk->getBlock(8,100,8));
        });};
        const auto loading=[&](bool plugin){return measure([&]{
            std::vector<std::unique_ptr<Chunk>> chunks;std::vector<Chunk*> targets;
            std::map<std::pair<int,int>,Chunk*> neighbors;
            for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x) {
                chunks.push_back(std::make_unique<Chunk>(x,z));targets.push_back(chunks.back().get());neighbors[{x,z}]=targets.back();
            }
            if(plugin){std::vector<RegionGenerationData::PendingBlock> pending;runtime.generateRegion(-1,-1,3,6,targets,pending);}
            else for(auto& chunk:chunks)FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*chunk,reference);
            const auto read=[&](int x,int y,int z){const int cx=x/16-(x%16<0),cz=z/16-(z%16<0);const auto found=neighbors.find({cx,cz});
                return found==neighbors.end()?BlockId::AIR:found->second->getBlock(x-cx*16,y,z-cz*16);};
            for(const auto& chunk:chunks){ChunkMesh mesh;mesh.build(chunk->worldX(),chunk->worldZ(),chunk->rawBlocks(),chunk->getColumnMaxYData(),read,
                [](int,int,int){return LightSample{15,0};});checksum+=mesh.vertices.size()+mesh.indices.size()+mesh.opaqueIndexCount+mesh.translucentIndexCount;}
        });};
        double a=0,b=0,c=0,d=0,e=0,f=0,g=0,h=0,j=0,k=0;
        if(round%2){b=generation(true);a=generation(false);d=lod(true);c=lod(false);f=region(true);e=region(false);h=initial(true);g=initial(false);k=loading(true);j=loading(false);}
        else {a=generation(false);b=generation(true);c=lod(false);d=lod(true);e=region(false);f=region(true);g=initial(false);h=initial(true);j=loading(false);k=loading(true);}
        std::cout<<"benchmark round="<<round<<" warmup="<<(round==0)<<" native_generation_ms="<<a
                 <<" provider_generation_ms="<<b<<" native_lod_ms="<<c<<" provider_lod_ms="<<d
                 <<" native_region_ms="<<e<<" provider_region_ms="<<f<<" native_initial_chunk_ms="<<g<<" provider_initial_chunk_ms="<<h
                 <<" native_cpu_loading_ms="<<j<<" provider_cpu_loading_ms="<<k<<'\n';
        if(round){const auto i=size_t(round-1);nativeGeneration[i]=a;pluginGeneration[i]=b;nativeLod[i]=c;pluginLod[i]=d;nativeRegion[i]=e;pluginRegion[i]=f;nativeInitial[i]=g;pluginInitial[i]=h;nativeLoading[i]=j;pluginLoading[i]=k;}
    }
    const auto median=[](auto values){std::sort(values.begin(),values.end());return values[2];};
    std::cout<<"benchmark median native_generation_ms="<<median(nativeGeneration)<<" provider_generation_ms="<<median(pluginGeneration)
             <<" generation_ratio="<<median(pluginGeneration)/median(nativeGeneration)<<" native_lod_ms="<<median(nativeLod)
             <<" provider_lod_ms="<<median(pluginLod)<<" lod_ratio="<<median(pluginLod)/median(nativeLod)
             <<" native_region_ms="<<median(nativeRegion)<<" provider_region_ms="<<median(pluginRegion)<<" region_ratio="<<median(pluginRegion)/median(nativeRegion)
             <<" native_initial_chunk_ms="<<median(nativeInitial)<<" provider_initial_chunk_ms="<<median(pluginInitial)<<" initial_chunk_ratio="<<median(pluginInitial)/median(nativeInitial)
             <<" native_cpu_loading_ms="<<median(nativeLoading)<<" provider_cpu_loading_ms="<<median(pluginLoading)<<" cpu_loading_ratio="<<median(pluginLoading)/median(nativeLoading)<<" checksum="<<checksum<<'\n';
}
