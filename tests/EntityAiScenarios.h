#pragma once
#include "EntityAiTestRenderer.h"
#include "entity/EntityManager.h"
#include "player/Player.h"
#include "world/World.h"
#include "game/WorldCatalog.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace EntityAiScenarios {
struct Scene {
    World world;
    Player player{world};
    EntityManager mobs{world};
    EntityAiTestRenderer renderer;
    explicit Scene(const std::filesystem::path& assets) {
        for (int cx=-3; cx<=3; ++cx) for (int cz=-3; cz<=3; ++cz) {
            Chunk* chunk=world.getChunk(cx,cz);
            for (int x=0;x<16;++x) for (int z=0;z<16;++z)
                chunk->setBlock(x,0,z,BlockId::STONE);
            chunk->generated=true;
        }
        const int previous=Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE=3;
        world.update({0.5,1.0,0.5},0);
        Config::RENDER_DISTANCE=previous;
        player.setPosition({0.5,1,0.5});
        player.configureRules(GameMode::Survival,Difficulty::Normal);
        mobs.setNaturalSpawningEnabled(false);
        mobs.initializeModels(assets,renderer);
    }
    void block(int x,int y,int z,BlockId id) {
        Chunk* chunk=world.getChunk(World::worldToChunkX(x),World::worldToChunkZ(z));
        chunk->setBlock(x-chunk->worldX(),y,z-chunk->worldZ(),id);
    }
    uint64_t add(EntityType type,glm::dvec3 position) {
        if (!mobs.spawnMob(type,position)) std::abort();
        return mobs.entities().back().id;
    }
    void step(float dt=1.0f/60.0f, bool targetable=false, uint64_t tick=6000) {
        mobs.update(player,dt,false,false,targetable,false,false,false,tick);
    }
};
// Optional reproducible Vulkan fixture; caller chooses an isolated save root.
inline int writeDemo(const std::filesystem::path& root) {
    WorldCatalog catalog(root / "saves");
    const auto id=catalog.create("AI Navigation Smoke",42,GameMode::Creative,
        Difficulty::Normal,true,WorldType::Superflat);
    auto store=catalog.open(id);
    auto metadata=store.loadMetadata();
    metadata.playerPosition={.5,-60,2.5}; metadata.worldSpawn={0,-60,2};
    metadata.worldTicks=3000; metadata.overworldDayPhase=.15f;
    metadata.inventory.slot(0)={ItemId::COW_SPAWN_EGG,64,0};
    metadata.inventory.slot(1)={ItemId::ZOMBIE_SPAWN_EGG,64,0};
    metadata.inventory.slot(2)={ItemId::VILLAGER_SPAWN_EGG,64,0};
    store.saveMetadata(metadata);
    std::map<std::pair<int,int>,std::vector<WorldMetadata::PersistedEntity>> entities;
    for (int i=0;i<12;++i) {
        WorldMetadata::PersistedEntity e;
        e.type=static_cast<uint8_t>(i<8 ? static_cast<EntityType>(1+i%4) : EntityType::Villager);
        e.position={-4.5+3*(i%4),-60,-4.5-3*(i/4)};
        e.health=20; e.behaviorSeed=static_cast<uint32_t>(123+i);
        entities[{World::worldToChunkX(e.position.x),World::worldToChunkZ(e.position.z)}].push_back(e);
    }
    for (const auto& [key,values]:entities) store.saveChunkEntities(key.first,key.second,values);
    std::map<std::pair<int,int>,std::vector<BlockOverride>> edits;
    const auto block=[&](int x,int y,int z,BlockId type) {
        const int cx=World::worldToChunkX(x),cz=World::worldToChunkZ(z);
        const uint32_t index=static_cast<uint32_t>((x-cx*16)+(z-cz*16)*16+(y+64)*256);
        edits[{cx,cz}].push_back({index,type});
    };
    for (int z=-9;z<=-5;++z) for (int y=-60;y<=-58;++y) block(2,y,z,BlockId::COBBLESTONE);
    block(6,-60,-7,BlockId::COMPOSTER);
    block(6,-60,-10,bedBlock(BedPart::Foot,BedDirection::East));
    block(7,-60,-10,bedBlock(BedPart::Head,BedDirection::East));
    for (const auto& [key,values]:edits) store.saveChunkOverrides(key.first,key.second,values);
    std::cout<<"AI demo save: "<<store.worldDirectory()<<'\n';
    return 0;
}
inline int benchmark(const std::filesystem::path& assets) {
    for (bool maze : {false,true}) for (int count : {40,100}) {
        Scene scene(assets);
        if (maze) for (int x=-16;x<=16;x+=8) for (int z=-24;z<=24;++z)
            if (z%12!=0 && z%12!=1)
                for (int y=1;y<=3;++y) scene.block(x,y,z,BlockId::STONE);
        const EntityType types[]={EntityType::Cow,EntityType::Pig,EntityType::Chicken,
                                  EntityType::Skeleton,EntityType::Villager};
        for (int i=0;i<count;++i)
            scene.mobs.spawnMob(maze ? types[i%5] : static_cast<EntityType>(1+i%4),
                {-18.5+4*(i%10),1,-18.5+4*(i/10)});
        std::vector<double> times;
        size_t rays=0, nodes=0, peakNodes=0;
        for (int frame=0;frame<900;++frame) {
            const auto start=std::chrono::steady_clock::now();
            scene.step();
            const auto end=std::chrono::steady_clock::now();
            rays+=scene.mobs.aiStats().sightQueries;
            nodes+=scene.mobs.aiStats().pathNodes;
            peakNodes=std::max(peakNodes,scene.mobs.aiStats().pathNodes);
            if(scene.mobs.aiStats().pathNodes>Config::AI_PATH_NODES_PER_FRAME ||
               scene.mobs.aiStats().poiBlocks>Config::AI_POI_BLOCKS_PER_FRAME ||
               scene.mobs.aiStats().activeSearches>Config::AI_ACTIVE_SEARCH_LIMIT)
                throw std::runtime_error("AI benchmark exceeded a work budget");
            if(frame>=300) times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
        }
        std::sort(times.begin(),times.end());
        if (scene.mobs.entities().size()!=static_cast<size_t>(count))
            throw std::runtime_error("AI benchmark changed its entity population");
        std::cout<<"AI benchmark scenario="<<(maze ? "mixed_maze" : "open_animals")<<" count="<<count<<" median_us="<<times[times.size()/2]
                 <<" p95_us="<<times[times.size()*95/100]
                 <<" sight_queries="<<rays<<" path_nodes="<<nodes<<" peak_nodes="<<peakNodes<<'\n';
    }
    return 0;
}
}
