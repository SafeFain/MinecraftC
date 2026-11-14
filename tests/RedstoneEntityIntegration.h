#pragma once
#include "entity/SlimeRules.h"
#include "network/EntityProtocol.h"
#include "world/RedstoneBlocks.h"

namespace RedstoneEntityIntegration {
inline int run(const std::filesystem::path& assets) {
    using EntityAiScenarios::Scene;using EntityAiScenarios::PresentationMode;
    static_assert(static_cast<uint16_t>(EntityType::ExperienceOrb)==29);
    static_assert(static_cast<uint16_t>(EntityType::Slime)==30);
    require(entityTypeForSpawnEgg(SpawnEggMob::Slime)==EntityType::Slime,"slime spawn egg mapping");
    require(!nativeFireImmune(EntityType::Slime),"slime inherited magma fire immunity");
    for(uint8_t size=0;size<3;++size) {
        Scene scene(assets,PresentationMode::Unset);scene.add(EntityType::Slime,{8.5,1,.5});
        auto saved=scene.mobs.saveEntities();saved[0].nether.magmaSize=size;saved[0].health=float(slimeHealth(size));saved[0].behaviorSeed=2;
        scene.mobs.clear();scene.mobs.loadEntities(saved);
        require(scene.mobs.entities().size()==1 && scene.mobs.entities()[0].nether.magmaSize==size,"slime size save/load");
        require(std::abs(EntityManager::entitySize(scene.mobs.entities()[0]).x-.51f*float(1u<<size))<.0001f,"slime collision size");
        saved=scene.mobs.saveEntities();saved[0].health=0;scene.mobs.clear();scene.mobs.loadEntities(saved);scene.step();
        if(size) {
            require(scene.mobs.entities().size()>=2 && scene.mobs.entities().size()<=4,"slime split count");
            for(const auto& child:scene.mobs.entities())require(child.type==EntityType::Slime && child.nether.magmaSize==size-1,"slime split type or size");
        } else {
            unsigned count=0;for(const auto& item:scene.mobs.entities()) {
                require(item.type==EntityType::Item && item.item.id==ItemId::SLIME_BALL,"slime dropped magma loot");count+=item.item.count;
            }
            require(count==2,"small slime drop count");
        }
    }
    {
        Scene scene(assets,PresentationMode::Unset);scene.add(EntityType::Slime,{8.5,1,.5});
        scene.mobs.update(scene.player,.05f,false,true,false,false,false,false,6000);
        require(scene.mobs.entities().empty(),"peaceful slime split or dropped loot");
    }
    // Unlike the pure AABB helper, this exercises all real entity classes
    // through the simulation's public piston operation. No presentation is bound.
    for(unsigned face=0;face<6;++face) {
        Scene scene(assets,PresentationMode::Unset);
        const auto delta=glm::dvec3(Redstone::vector(Redstone::offset(static_cast<Redstone::Face>(face))))*.5;
        const Redstone::PistonSweep sweep{{8,10,8},{9,11,9},delta};
        const glm::dvec3 position{8.5,10.1,8.5};
        scene.mobs.spawnItem(position,{ItemId::STONE,1});
        require(scene.mobs.spawnMob(EntityType::Cow,position),"sweep mob fixture");
        const auto before=scene.mobs.saveEntities();scene.mobs.pushPiston(sweep);
        for(size_t i=0;i<before.size();++i) {
            const auto actual=scene.mobs.entities()[i].position-before[i].position;
            require(glm::dot(actual,delta)>0,"piston failed to push a mob or dropped item along its sweep");
        }
    }
    EntityModelRegistry models;models.loadAll(assets);
    const auto& slime=models.definition(EntityType::Slime);
    require(!slime.usesPlaceholder && slime.asset && slime.graph,"slime model or action graph missing");
    bool gel=false,core=false;
    for(const auto& primitive:slime.asset->primitives) {
        require(primitive.material>=0&&size_t(primitive.material)<slime.asset->materials.size(),"slime primitive lost material");
        const auto mode=slime.asset->materials[primitive.material].alphaMode;
        gel|=mode==model::AlphaMode::Blend;core|=mode==model::AlphaMode::Opaque;
    }
    require(gel&&core,"slime needs translucent gel and an opaque core");
    Lan::EntityBatch batch;EntitySnapshot snapshot;snapshot.id=1;snapshot.type=EntityType::Slime;snapshot.health=16;snapshot.nether.magmaSize=2;
    batch.entities.push_back(snapshot);const auto decoded=Lan::decodeEntityBatch(Lan::encodeEntityBatch(batch));
    require(decoded.entities[0].type==EntityType::Slime && decoded.entities[0].nether.magmaSize==2,"LAN slime state");
    std::cout<<"Redstone slime integration passed\n";return 0;
}
} // namespace RedstoneEntityIntegration
