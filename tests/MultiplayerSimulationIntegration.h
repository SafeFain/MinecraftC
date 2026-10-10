#pragma once

namespace MultiplayerSimulationIntegration {
inline int run(const std::filesystem::path& assets) {
    {
        EntityAiScenarios::Scene scene(assets);
        Player guest(scene.world); guest.configureRules(GameMode::Survival, Difficulty::Normal);
        scene.player.setPosition({.5,1,.5}); guest.setPosition({.5,1,2.5});
        scene.mobs.strikeLightning(std::vector<EntityPlayerView>{{0,&scene.player,true,true}, {7,&guest,true,true}}, {0,1,0});
        require(scene.player.survivalStats().health() == 15 && guest.survivalStats().health() == 15,
                "one lightning strike damages each eligible participant once");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        Player guest(scene.world);
        guest.configureRules(GameMode::Survival, Difficulty::Normal);
        guest.setPosition(scene.player.getPosition());
        scene.mobs.spawnItem({.5, 1.2, .5}, {ItemId::DIAMOND, 1, 0});
        scene.mobs.update(std::vector<EntityPlayerView>{{9, &scene.player, true, true},
                          {2, &guest, true, true}}, .01f, false, false);
        require(guest.inventory().slot(0).id == ItemId::DIAMOND &&
                scene.player.inventory().slot(0).empty() && scene.mobs.entities().empty(),
                "contended pickup awards item once in stable identity order");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        Player guest(scene.world);
        guest.configureRules(GameMode::Survival, Difficulty::Normal);
        scene.player.setPosition({.5, 1, 5.5});
        guest.setPosition({.5, 1, 2.5});
        scene.mobs.spawnArrow({.5, 2, .5}, {0, 0, 20}, 4, false);
        scene.mobs.update(std::vector<EntityPlayerView>{{1, &scene.player, true, false},
                          {2, &guest, true, false}}, .3f, false, false);
        require(guest.survivalStats().health() < 20 && scene.player.survivalStats().health() == 20,
                "arrow contacts first participant in physical flight order");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        Player guest(scene.world); guest.configureRules(GameMode::Survival, Difficulty::Normal);
        scene.player.setPosition({.5, 1, .5}); guest.setPosition({.5, 1, 2.5});
        const auto views = std::vector<EntityPlayerView>{{0, &scene.player, true, false}, {7, &guest, true, false}};
        const MeleeAttackRequest attack{3, 4, false, false, false};
        require(!scene.mobs.attackRay(scene.player.getEyePosition(), {0, 0, 1}, attack, 0).foundTarget && guest.survivalStats().health() == 20,
                "PvP is disabled by default");
        scene.mobs.setPvpPlayers(true, views);
        require(scene.mobs.attackRay(scene.player.getEyePosition(), {0, 0, 1}, attack, 0).primaryDamaged && guest.survivalStats().health() == 16,
                "enabled PvP uses player AABB melee and survival damage");
        guest.survivalStats().resetAfterRespawn(); guest.resetDamageImmunity();
        scene.world.setBlock(0, 2, 1, BlockId::STONE);
        require(!scene.mobs.attackRay(scene.player.getEyePosition(), {0, 0, 1}, attack, 0).foundTarget && guest.survivalStats().health() == 20,
                "solid block occludes player melee");
        scene.world.setBlock(0, 2, 1, BlockId::AIR);
        scene.mobs.spawnArrow({.5, 2.2, .5}, {0, 0, 12}, 4, true, 0);
        scene.mobs.update(views, .2f, false, false);
        require(guest.survivalStats().health() < 20 && scene.player.survivalStats().health() == 20,
                "PvP arrow hits another player and excludes its identified shooter");
        guest.survivalStats().resetAfterRespawn(); guest.resetDamageImmunity();
        scene.mobs.setPvpPlayers(false, views);
        scene.mobs.spawnArrow({.5, 2.2, .5}, {0, 0, 12}, 4, true, 0);
        scene.mobs.update(views, .2f, false, false);
        require(guest.survivalStats().health() == 20, "disabling PvP excludes friendly projectile damage");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        Player guest(scene.world);
        guest.configureRules(GameMode::Survival, Difficulty::Normal);
        scene.player.setPosition({2.5, 1, .5});
        guest.setPosition({-2.5, 1, .5});
        scene.mobs.primeTnt({0, 1, 0}, .05f, false);
        scene.mobs.update(std::vector<EntityPlayerView>{{1, &scene.player, true, false},
                          {2, &guest, true, false}}, .1f, false, false);
        require(guest.survivalStats().health() < 20 && scene.player.survivalStats().health() < 20,
                "one TNT detonation damages all exposed participants");
        require(scene.mobs.takeExplosionEvents().size() == 1,
                "multiplayer dimension simulates a detonation exactly once");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        Player guest(scene.world);
        guest.configureRules(GameMode::Survival, Difficulty::Normal);
        scene.player.setPosition({.5, 1, 8.5});
        guest.setPosition({.5, 1, 3.5});
        const auto zombie = scene.add(EntityType::Zombie, {.5, 1, .5});
        auto views = std::vector<EntityPlayerView>{{1, &scene.player, true, false}, {2, &guest, true, false}};
        for (int frame = 0; frame < 10; ++frame) scene.mobs.update(views, .02f, false, false);
        require(scene.mobs.entityById(zombie)->ai.hasTarget &&
                scene.mobs.entityById(zombie)->ai.targetPlayerId == 2,
                "mob acquires nearest visible eligible player");
        scene.player.setPosition({.5, 1, 2.5});
        scene.mobs.update(views, .05f, false, false);
        require(scene.mobs.entityById(zombie)->ai.targetPlayerId == 2,
                "retained mob target cannot change during an attack or memory interval");
        scene.mobs.update(std::vector<EntityPlayerView>{{1, &scene.player, true, false}}, .1f, false, false);
        require(scene.mobs.entityById(zombie)->ai.targetPlayerId == 1,
                "departed target invalidates before reacquisition");
    }
    {
        EntityAiScenarios::Scene scene(assets);
        GameRuleSet rules;
        rules.set(GameRuleId::RandomTickSpeed, GameRuleValue::integer(1000));
        scene.world.setGameRules(rules);
        scene.world.setBlock(-1, 1, -1, farmlandForMoisture(7));
        scene.world.setBlock(-1, 2, -1, BlockId::WHEAT_0);
        scene.world.getChunk(-1, -1)->setSkyLight(15, 3, 15, 15);
        for (uint64_t tick = 1; tick <= 100; ++tick)
            scene.world.tickSurvival(std::vector<glm::dvec3>{{1000, 1, 1000}, {0, 1, 0}}, tick, true);
        require(scene.world.getBlock(-1, 2, -1) > BlockId::WHEAT_0,
                "guest vicinity participates in shared random ticks");
        rules.set(GameRuleId::FireSpreadRadiusAroundPlayer, GameRuleValue::integer(2));
        scene.world.setGameRules(rules);
        scene.world.setBlock(0, 1, 0, BlockId::FIRE);
        scene.world.setBlock(1, 1, 0, BlockId::PLANKS);
        WeatherSystem weather; weather.reset(42);
        for (uint64_t tick = 101; tick <= 500; ++tick)
            scene.world.tickWeather(weather, true, tick);
        require(scene.world.getBlock(0, 1, 0) != BlockId::FIRE ||
                scene.world.getBlock(1, 1, 0) != BlockId::PLANKS,
                "fire radius measures distance to any participant");
    }
    {
        World world;
        world.setLocalRenderEnabled(false);
        world.setAdditionalStreamingInterests({{100, -100, 2}});
        world.update({1600, 65, -1600}, 3);
        require(world.getActiveChunks().empty() && world.getSimulationChunks().size() == 3,
                "guest-only dimension allocates CPU terrain without a local render target");
    }
    {
        World replica;
        replica.setReplicaMode(true);
        replica.update({.5, 65, .5}, 1);
        replica.enqueueGeneration();
        replica.processCompletedGenerations();
        require(!replica.getChunk(0, 0)->generated.load(), "replica never generates authoritative near terrain");
        std::vector<uint16_t> blocks(Config::CHUNK_VOLUME, static_cast<uint16_t>(BlockId::AIR));
        blocks[0] = static_cast<uint16_t>(BlockId::STONE);
        require(!replica.installReplicaSnapshot(500, 500, blocks), "unsolicited snapshots cannot allocate remote terrain");
        require(replica.installReplicaSnapshot(0, 0, blocks), "requested snapshot installs authoritative blocks");
        replica.processCompletedGenerations();
        require(replica.getBlock(0, Config::WORLD_MIN_Y, 0) == BlockId::STONE &&
                replica.getChunk(0, 0)->lightingInitialized.load(), "snapshot derives local light before meshing");
        require(!replica.installReplicaEdits(0, 0, {{0, BlockId::GLASS}, {0, BlockId::STONE}}),
                "duplicate delta indices reject atomically");
        require(replica.getBlock(0, Config::WORLD_MIN_Y, 0) == BlockId::STONE,
                "rejected delta preserves terrain");
        require(replica.installReplicaEdits(0, 0, {{0, BlockId::GLASS}}) &&
                replica.getBlock(0, Config::WORLD_MIN_Y, 0) == BlockId::GLASS,
                "validated delta updates replica terrain");
        require(!replica.hasModifiedChunks(), "replica snapshots and deltas never create saved player edits");
    }
    std::cout << "Multiplayer simulation integration tests passed\n";
    return 0;
}
}
