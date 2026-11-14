#pragma once
// Used only by the real two-process Vulkan LAN fixture.
inline bool LanSmokeScenario::netherTravel(GameSession& session) {
        const auto roster=session.roomRoster();
        const auto peer=std::find_if(roster.begin(),roster.end(),[](const auto& entry){return entry.id!=0;});
        if(peer==roster.end())return false;
        auto& guest=SessionScenario::guest(session,peer->id);
        if(guest.loading)return false;
        auto& runtime=SessionScenario::runtime(session,DimensionId::Nether);
        SessionScenario::context(session)->workers.waitIdle();
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
        guest.profile.positions[2]={.5,1.01,8.5};guest.profile.positioned[2]=true;
        return session.pluginTravel(peer->id,uint32_t(DimensionId::Nether));
}
