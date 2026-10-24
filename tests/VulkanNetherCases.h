#pragma once
#include "renderer/NetherEnvironment.h"
#include "world/LodTerrainSystem.h"
#include <set>

namespace VulkanNetherCases {
inline int floorDivision(int n,int d){return n/d-(n%d<0);}
inline int run(VulkanRenderer& renderer,Window& window,const std::filesystem::path& assets,const std::filesystem::path& output) {
    if(!Plugins::content().requirements.empty())throw std::runtime_error("Native Nether smoke requires plugins off");
    std::filesystem::create_directories(output);
    constexpr int width=960,height=640;
    const auto capture=[&](const std::string& name) {
        renderer.waitIdle();const auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
        if(pixels.size()!=size_t(width*height*4))throw std::runtime_error("Invalid native capture");
        std::ofstream file(output/(name+".ppm"),std::ios::binary);file<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
        if(!file)throw std::runtime_error("Cannot write native capture");
    };
    renderer.setVisualQuality(VisualQuality::VeryHigh);
    EnhancedVisualSettings settings;settings.enabled=true;settings.custom=true;settings.gi.enabled=true;settings.gi.distance=128;settings.gi.temporalStability=0;
    settings.bloom=false;settings.lightShafts=false;settings.reflections=false;
    renderer.setEnhancedVisualSettings(settings);
    WorldGenerator generator(7,WorldType::Normal,DimensionId::Nether);
    EntityModelRegistry models;models.loadAll(assets);models.uploadAll(renderer.modelRenderer());
    std::array<glm::ivec2,5> anchors{};std::array<bool,5> found{};
    for(int z=-16;z<=16;++z)for(int x=-16;x<=16;++x) {
        const glm::ivec2 point(x*64+32,z*64+32);
        const auto biome=size_t(generator.nether().biomeAt(point.x,point.y));
        if(!found[biome]){found[biome]=true;anchors[biome]=point;}
    }
    if(std::find(found.begin(),found.end(),false)!=found.end())throw std::runtime_error("Native smoke biome window incomplete");
    constexpr const char* names[]={"wastes","soul-valley","crimson","warped","basalt"};
    constexpr const char* structures[]={"minecraftc:nether_fortress","minecraftc:bastion_bridge","minecraftc:bastion_stable","minecraftc:bastion_housing","minecraftc:bastion_treasure","minecraftc:nether_fossil","minecraftc:ruined_portal"};
    for(int scenario=0;scenario<13;++scenario) {
        const bool structure=scenario>=5&&scenario<12,top=scenario==12;
        const int biome=scenario<5?scenario:0,grid=structure?7:5;
        auto anchor=anchors[size_t(biome)];int floor=std::clamp(generator.nether().surface(anchor.x,anchor.y),33,90);
        std::string name=names[biome];
        if(structure) {
            const auto found=generator.nether().locate(structures[scenario-5],0,0,4096);
            if(!found)throw std::runtime_error("Native structure capture missing");
            anchor={found->anchor.x,found->anchor.z};floor=found->anchor.y;name="structure-"+std::string(found->key()).substr(11);
        }
        if(top){floor=254;name="build-limit-255";}
        const int ox=floorDivision(anchor.x,16)-grid/2,oz=floorDivision(anchor.y,16)-grid/2;
        std::vector<std::unique_ptr<Chunk>> chunks;std::vector<Chunk*> view;
        for(int z=0;z<grid;++z)for(int x=0;x<grid;++x){chunks.push_back(std::make_unique<Chunk>(ox+x,oz+z));view.push_back(chunks.back().get());}
        std::vector<RegionGenerationData::PendingBlock> pending;generator.generateRegion(ox,oz,grid,Config::REGION_PADDING,view,pending);
        const auto blockAt=[&](int x,int y,int z) {
            const int cx=floorDivision(x,16)-ox,cz=floorDivision(z,16)-oz;
            return cx<0||cx>=grid||cz<0||cz>=grid||!Config::isValidWorldY(y)?BlockId::AIR:chunks[size_t(cx+cz*grid)]->getBlock(x-(ox+cx)*16,y,z-(oz+cz)*16);
        };
        const auto put=[&](int x,int y,int z,BlockId block) {
            const int cx=floorDivision(x,16)-ox,cz=floorDivision(z,16)-oz;if(cx<0||cx>=grid||cz<0||cz>=grid)return;
            auto& c=*chunks[size_t(cx+cz*grid)];c.setBlock(x-c.worldX(),y,z-c.worldZ(),block);c.setBlockLight(x-c.worldX(),y,z-c.worldZ(),getLightEmission(block));
        };
        constexpr BlockId floors[]={BlockId::NETHERRACK,BlockId::SOUL_SOIL,BlockId::CRIMSON_NYLIUM,BlockId::WARPED_NYLIUM,BlockId::BASALT};
        if(!structure&&!top) {
        // A small showroom exposes rigs and portal transparency inside the real cavern.
        for(int z=-15;z<=15;++z)for(int x=-15;x<=15;++x)for(int y=0;y<=13;++y)
            put(anchor.x+x,floor+y,anchor.y+z,y==0?floors[biome]:BlockId::AIR);
        for(int orientation=0;orientation<2;++orientation)for(int u=-1;u<=2;++u)for(int v=0;v<=4;++v) {
            const bool edge=u==-1||u==2||v==0||v==4;
            const int x=anchor.x+(orientation?7:u-3),z=anchor.y+(orientation?u-3:-3);
            put(x,floor+v,z,edge?BlockId::OBSIDIAN:orientation?BlockId::NETHER_PORTAL_Z:BlockId::NETHER_PORTAL_X);
        }
        for(int z=-3;z<=3;++z)for(int x=8;x<=12;++x)put(anchor.x+x,floor,anchor.y+z,BlockId::LAVA);
        put(anchor.x,floor+1,anchor.y+10,respawnAnchorBlock(4));put(anchor.x+1,floor+1,anchor.y+10,BlockId::SHROOMLIGHT);
        }
        if(top) {
            for(int z=-12;z<=12;++z)for(int x=-12;x<=12;++x)put(anchor.x+x,254,anchor.y+z,BlockId::BLACKSTONE);
            put(anchor.x,255,anchor.y,BlockId::SHROOMLIGHT);
        }
        for(auto& chunk:chunks)for(int z=0;z<16;++z)for(int x=0;x<16;++x)for(int y=0;y<128;++y) {
            const auto block=chunk->getBlock(x,y,z);if(getLightEmission(block))chunk->setBlockLight(x,y,z,getLightEmission(block));
        }
        std::vector<ChunkMesh> meshes(chunks.size());
        for(size_t i=0;i<chunks.size();++i) {
            int maxima[16][16];chunks[i]->copyColumnMaxY(maxima);
            meshes[i].build(chunks[i]->worldX(),chunks[i]->worldZ(),chunks[i]->rawBlocks(),maxima,blockAt,[&](int x,int y,int z){return LightSample{0,getLightEmission(blockAt(x,y,z))};});
            if(meshes[i].translucentIndexOffset!=meshes[i].opaqueIndexCount||meshes[i].shadowCasterIndexOffset!=meshes[i].opaqueIndexCount+meshes[i].translucentIndexCount)throw std::runtime_error("Native index-layer handoff mismatch");
            if(std::any_of(meshes[i].vertices.begin(),meshes[i].vertices.end(),[](const auto& v){return v.skyLight!=0;}))throw std::runtime_error("Native exact mesh contains sky light");
            renderer.uploadChunkMesh(meshes[i]);
        }
        const LodTileKey key{floorDivision(anchor.x,64),floorDivision(anchor.y,64),2};
        const auto lodData=buildApproximateLodTile(generator,key);
        auto lod=buildLodTileMesh(lodData,4,4,nullptr,nullptr,true,false);renderer.uploadChunkMesh(lod);
        if(lod.vertices.empty()||std::any_of(lod.vertices.begin(),lod.vertices.end(),[](const auto& v){return v.skyLight!=0;}))throw std::runtime_error("Native cave LOD sky policy");
        const auto environment=netherEnvironment(generator.nether(),{anchor.x,floor+3,anchor.y});
        if(environment.daylight!=0||environment.sunSize!=0||environment.moonSize!=0||environment.starIntensity!=0)throw std::runtime_error("Native sky bodies enabled");
        const glm::vec3 camera(structure?anchor.x+.5f:anchor.x-10.f,structure?floor+4.f:floor+6.f,anchor.y-14.f);
        const auto vp=glm::perspective(glm::radians(68.f),window.aspectRatio(),.1f,256.f)*glm::lookAt(camera,glm::vec3(anchor.x,floor+3.f,anchor.y+4.f),glm::vec3(0,1,0));
        const uint64_t scene=700+uint64_t(scenario);
        const auto frame=[&](bool distant) {
            renderer.beginFrame();renderer.setEnvironment(environment,camera);renderer.setViewProjection(vp);renderer.renderSky(environment,glm::inverse(vp),camera,false);
            renderer.beginVoxelGiFrame(glm::dvec3(camera),scene);for(const auto& chunk:chunks)renderer.submitVoxelGiChunk(*chunk);renderer.endVoxelGiFrame();
            if(distant)renderer.renderLod(lod,glm::translate(glm::mat4(1),glm::vec3(key.x*64,0,key.z*64)),vp,{float(key.x*64),4,float(key.z*64),4},0,4096,false);
            else for(size_t i=0;i<chunks.size();++i)renderer.renderChunk(meshes[i],glm::translate(glm::mat4(1),glm::vec3(chunks[i]->worldX(),0,chunks[i]->worldZ())),vp);
            if(!distant&&!structure&&!top) {
                models.beginFrame();int i=0;
#define MODEL(type,egg,keyName,label,h,sizeX,sizeY,sizeZ,speed,damage,monster,fire) {const auto t=EntityType::type;if(models.definition(t).usesPlaceholder)throw std::runtime_error("Native model placeholder");models.advance(t,uint64_t(i+1),1.f/60);models.queue(t,uint64_t(i+1),{anchor.x+(i%6)*2.4-6,floor+1.+(t==EntityType::Ghast?3:0),anchor.y+3.+(i/6)*5},{0,0,-1},uint32_t(i+1),{0,0,0},camera,renderer.modelRenderer(),{1,1,1},{0,.8f});++i;}
                MC_NETHER_MOBS(MODEL)
#undef MODEL
                models.endFrame();renderer.flushModels(vp);
            }
            if(distant)renderer.renderLod(lod,glm::translate(glm::mat4(1),glm::vec3(key.x*64,0,key.z*64)),vp,{float(key.x*64),4,float(key.z*64),4},0,4096,true);
            else for(size_t i=chunks.size();i-->0;)renderer.renderChunk(meshes[i],glm::translate(glm::mat4(1),glm::vec3(chunks[i]->worldX(),0,chunks[i]->worldZ())),vp,true);
            PostProcessState post;post.environment=environment;post.inverseViewProjection=glm::inverse(vp);post.cameraPosition=camera;post.sceneId=scene;
            renderer.finishScene(post);renderer.endFrame();
            if(!renderer.voxelGiStatus().active)throw std::runtime_error("Native GI inactive");
        };
        for(int settle=0;settle<60;++settle)frame(false);
        VulkanGiSmokeProbe::requestCapture(renderer);frame(false);capture("nether-"+name);
        if(!top){for(int settle=0;settle<4;++settle)frame(true);
        VulkanGiSmokeProbe::requestCapture(renderer);frame(true);capture("nether-lod-"+name);}
        renderer.waitIdle();for(auto& mesh:meshes)renderer.releaseChunkMesh(mesh);renderer.releaseChunkMesh(lod);
        std::cout<<"PASS native Nether "<<name<<" native geometry, no sky light/bodies/clouds, GI and capture\n";
    }
    return 0;
}
}
