#pragma once
#include "reference/heaven_v9/FrozenParticleSystem.h"
#include "reference/heaven_v9/FrozenEnvironment.h"
#include "reference/heaven_v9/FrozenLoot.h"
#include "world/ChunkStore.h"
#include "world/WorldPersistence.h"
#include "plugins/EnvironmentPresentation.h"
#include "renderer/ParticleSystem.h"
#include <cmath>
#include <map>
#include <set>

namespace HeavenParticleMigration {
inline int run() {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    for(int sample=0;sample<=100;++sample) {
        FrozenHeavenReference::RenderEnvironment frozen;
        RenderEnvironment current;
        frozen.daylight=current.daylight=sample*.01f;
        frozen.starIntensity=current.starIntensity=1-current.daylight;
        const float angle=sample*.062831853f;
        frozen.sunDirection=current.sunDirection={std::cos(angle),std::sin(angle),0};
        frozen.directIntensity=current.directIntensity=.25f+current.daylight*.75f;
        frozen.ambientIntensity=current.ambientIntensity=.15f+current.daylight*.85f;
        const auto expected=FrozenHeavenReference::applyHeavenEnvironment(frozen);
        bool clouds=true;const auto actual=Plugins::applyEnvironment(current,1,clouds);
        check(actual.skyStyle==RenderSkyStyle::Layered,"P1 registered sky style differs");
        check(actual.skyFlags==(SkyNoAtmosphere|SkyNoLowClouds|SkyNoVoxelClouds|SkyNoLightShafts)&&
              actual.gradeColor==glm::vec3(1.018f,1.008f,.982f)&&actual.gradeStrength==.36f&&actual.particleIntensity==.8f,
              "P1 registered occlusion/grading policies differ from frozen sky/post behavior");
        check(glm::length(expected.zenithColor-actual.zenithColor)<.000001f&&
              glm::length(expected.horizonColor-actual.horizonColor)<.000001f&&
              glm::length(expected.fogColor-actual.fogColor)<.000001f&&
              glm::length(expected.directColor-actual.directColor)<.000001f&&
              glm::length(expected.ambientColor-actual.ambientColor)<.000001f,
              "P1 environment colors differ from independent frozen reference");
        check(std::abs(expected.starIntensity-actual.starIntensity)<.000001f&&
              std::abs(expected.directIntensity-actual.directIntensity)<.000001f&&
              std::abs(expected.ambientIntensity-actual.ambientIntensity)<.000001f,
              "P1 environment intensity differs from independent frozen reference");
    }
    for(uint64_t seed:{123ULL,987654321ULL,0xffffffffffffffffULL}) {
        World world;world.resetForNewSeed(seed,WorldType::Normal,DimensionId::Heaven);
        FrozenHeavenReference::FrozenHeavenV9 generator(WorldGenContext(seed).derive(0x484556454E5F5345ULL));
        WorldGenerator runtime(seed,WorldType::Normal,DimensionId::Heaven);
        std::set<std::pair<int,int>> locations;
        const auto add=[&](int x,int z){const int cx=int(std::floor(double(x)/16)),cz=int(std::floor(double(z)/16));
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)locations.emplace(cx+dx,cz+dz);};
        const auto origin=generator.originHeavenSkyway();add(origin.worldX,origin.worldZ);
        for(const auto type:{StructureType::XiguangRuin,StructureType::StarCrystalGeode,StructureType::CloudspireTower,StructureType::SkywayShrine}) {
            const auto anchor=generator.locateNearestHeavenStructure(type,-130,-130,4096);
            check(bool(anchor),"P1 frozen chest fixture missing");add(anchor->worldX,anchor->worldZ);
        }
        size_t comparedChests=0;
        for(const auto& location:locations) {
            auto frozen=std::make_unique<Chunk>(location.first,location.second);
            std::map<std::tuple<int,int,int>,std::pair<StructureLootProfile,uint64_t>> expected;
            FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*frozen,generator,
                [&](int x,int y,int z,BlockId block,StructureLootProfile profile,uint64_t lootSeed){
                    if(block==BlockId::CHEST)expected[{x,y,z}]={profile,lootSeed};});
            auto current=std::make_unique<Chunk>(location.first,location.second);runtime.generate(*current);
            for(const auto& metadata:current->pluginStructures)if(metadata.id==BlockId::CHEST) {
                const auto found=expected.find({metadata.worldX,metadata.worldY,metadata.worldZ});
                check(found!=expected.end()&&found->second.second==metadata.lootSeed,"P1 frozen chest metadata/seed differs");
                BlockEntity before;before.type=BlockEntityType::Chest;
                FrozenHeavenReference::populateGeneratedLoot(before,found->second.first,found->second.second);
                ChunkStore store;store.get(0,0)->generated=true;WorldPersistence persistence(store);
                persistence.registerGeneratedBlockEntityUnlocked(0,0,Config::worldYToStorageY(100)*256,BlockId::CHEST,StructureLootProfile::None,metadata.lootSeed,&metadata);
                const auto* after=persistence.getBlockEntity({0,100,0});check(after,"P1 registered chest missing");
                for(size_t slot=0;slot<before.chest.size();++slot)check(before.chest[slot].id==after->chest[slot].id&&
                    before.chest[slot].count==after->chest[slot].count&&before.chest[slot].damage==after->chest[slot].damage,"P1 complete frozen chest inventory differs");
                ++comparedChests;
            }
        }
        check(comparedChests>=4,"P1 insufficient independent chest coverage");
        FrozenHeavenReference::ParticleSystem reference(generator);
        ParticleSystem actual;
        for(int frame=0;frame<450;++frame) {
            const float dt=std::array<float,4>{{0,.016f,.1f,.25f}}[size_t(frame%4)];
            const float daylight=float((frame/75)%3)*.5f;
            const glm::dvec3 viewer(-640+frame*1.37,90+frame*.01,320-frame*.79);
            reference.update(world,viewer,dt,0,seed+uint64_t(frame),DimensionId::Heaven,daylight);
            actual.update(world,viewer,dt,0,seed+uint64_t(frame),DimensionId::Heaven,daylight);
            const auto left=reference.buildRenderData(viewer);
            const auto right=actual.buildRenderData(viewer);
            check(left.size()==right.size(),"P1 independent particle emission/lifetime count differs");
            for(size_t i=0;i<left.size();++i) {
                const auto& a=left[i];const auto& b=right[i];
                check(a.kind==b.kind,"P1 particle shape differs");
                check(glm::length(a.position-b.position)<.00001f,"P1 particle motion differs");
                check(std::abs(a.phase-b.phase)<.000001f&&std::abs(a.rotation-b.rotation)<.000001f&&std::abs(a.size-b.size)<.000001f,
                      "P1 particle phase/rotation/size differs");
                // Colors previously came from the frozen weather fragment shader.
                constexpr std::array<glm::vec3,8> palette{{{1,.86f,.55f},{.62f,.95f,.55f},{1,.72f,.30f},{.45f,.85f,1},
                    {.95f,.97f,1},{.78f,.80f,.86f},{.40f,1,.80f},{.85f,.75f,1}}};
                glm::vec3 tint;
                if(a.kind==6) {
                    tint=glm::mix(glm::vec3(.40f,.58f,1),glm::vec3(1,.84f,.38f),std::clamp(a.texture,0.f,1.f));
                    tint=glm::mix(tint,glm::vec3(.78f,.42f,1),.5f+.5f*std::sin(a.phase*9));
                } else {tint=palette[size_t(a.texture)];if(a.kind==8)tint*=1.2f;}
                check(glm::length(tint-glm::vec3(b.color))<.000001f,"P1 particle biome/daylight color differs");
            }
        }
        actual.update(world,{0,90,0},0,0,seed,DimensionId::Overworld,1);
        check(actual.size()==0,"P1 ambient particles survived dimension switch");
    }
    std::cout<<"Independent Heaven particle emission/motion/colors passed\n";
    return 0;
}
}
