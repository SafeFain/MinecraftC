#pragma once
// Used only by the real two-process Vulkan LAN fixture.
inline bool GameSessionTestAccess::netherTravel(GameSession& session) {
        if(session.guests.empty())return false;
        auto& guest=*session.guests.begin()->second;
        if(guest.loading)return false;
        auto& runtime=session.ensureDimension(DimensionId::Nether);
        runtime.world.setThreadPool(nullptr);runtime.entities.setNaturalSpawningEnabled(false);
        runtime.world.setAdditionalStreamingInterests({{0,0,2}});
        for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x) {
            auto* chunk=runtime.world.getChunk(x,z);
            for(int bz=0;bz<16;++bz)for(int bx=0;bx<16;++bx)chunk->setBlock(bx,0,bz,BlockId::BLACKSTONE);
            chunk->generated=true;chunk->lifecycle=Chunk::LifecycleState::Renderable;
        }
        runtime.world.update({.5,1.01,8.5},0);
        WorldMetadata::PersistedEntity piglin;piglin.type=static_cast<uint16_t>(EntityType::Piglin);piglin.position={-2,1.01,6.5};piglin.health=16;
        runtime.entities.loadEntities({piglin});runtime.entities.spawnItem({2.5,1.3,6.5},{ItemId::NETHERITE_INGOT,2,0});
        guest.player.inventory().slot(0)={ItemId::NETHERITE_SWORD,1,17};
        guest.player.statusEffects().apply(StatusEffectType::FireResistance,0,6000);
        return session.travelLanPlayer(guest,DimensionId::Nether,false,glm::dvec3(.5,1.01,8.5));
}
