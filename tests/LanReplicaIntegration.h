#pragma once
#include "LodMeshRecorder.h"
#include <set>

namespace LanReplicaIntegration {
inline int run(const std::filesystem::path& assets) {
    {
        Lan::AuthorityState state;
        state.inventory.slot(0)={ItemId::DIAMOND,3};
        state.self.mainhand=state.inventory.slot(0);
        state.cursor={ItemId::IRON_PICKAXE,1,17};
        state.cursor.components.enchantments.set(EnchantmentId::Mending,1);
        state.cursor.components.setCustomName("Wire tool"); state.cursor.components.repairCost=7;
        state.crafting[0]=state.cursor;
        state.window.kind=InventoryWindowKind::Container; state.window.container.emplace();
        state.window.container->type=BlockEntityType::Anvil;
        state.window.container->chest[0]=state.cursor; state.window.container->output=state.cursor;
        state.window.container->workCost=30; state.window.container->workFailure=3;
        state.window.container->rename="Wire name";
        const auto decodedState=Lan::decodeAuthorityState(Lan::encodeAuthorityState(state));
        require(decodedState.inventory.slot(0)==state.inventory.slot(0) && decodedState.self.mainhand==state.self.mainhand &&
                decodedState.cursor==state.cursor && decodedState.crafting==state.crafting &&
                decodedState.window.container->output==state.cursor && decodedState.window.container->rename==state.window.container->rename,
                "Authority state preserves nonempty equipment, cursor/crafting and station previews");
    }

    Lan::EntityBatch sample;
    EntitySnapshot appearance; appearance.id = 19; appearance.type = EntityType::Villager;
    appearance.item={ItemId::EMERALD,2};
    appearance.health = 20; appearance.position = {-16.5, 64, 2};
    appearance.villager.profession = VillagerProfession::Fletcher; appearance.villager.level = 3;
    appearance.villager.reputation = -7; appearance.villager.uses[0] = 4; appearance.villager.demand[0] = 2;
    sample.entities.push_back(appearance); sample.deaths.push_back({20, EntityType::Cow, {0, 1, 0}, {}, {0, 0, 1}, 7, .4f});
    const auto bytes = Lan::encodeEntityBatch(sample);
    const auto decoded = Lan::decodeEntityBatch(bytes);
    require(decoded.entities.front().villager.reputation == -7 && decoded.entities.front().position.x == -16.5 && decoded.deaths.size() == 1,
            "entity codec preserves appearance, quote and death data");
    require(decoded.entities.front().item==appearance.item,"Entity wire codec preserves nonempty item field order");
    auto rejected = [](auto operation) { try { operation(); } catch (const Lan::ProtocolError&) { return true; } return false; };
    auto duplicate = sample; duplicate.deaths.front().id = appearance.id;
    require(rejected([&] { Lan::encodeEntityBatch(duplicate); }), "entity live/death IDs cannot collide");
    auto invalid = sample; invalid.entities.front().velocity.x = std::numeric_limits<float>::infinity();
    require(rejected([&] { Lan::encodeEntityBatch(invalid); }), "non-finite entity motion is rejected");
    for (size_t length = 0; length < bytes.size(); ++length) {
        Lan::Bytes truncated(bytes.begin(), bytes.begin() + length);
        require(rejected([&] { Lan::decodeEntityBatch(truncated); }), "truncated entity payload is rejected");
    }

    const auto root = std::filesystem::temp_directory_path() / "minecraftc-lan-replica-test";
    std::filesystem::remove_all(root);
    Config::RENDER_DISTANCE = 2;
    LodMeshRecorder renderer;
    ScenarioSession host(root / "host" / "saves"), client(root / "client" / "saves");
    host.initializeEntityModels(assets, renderer);
    client.initializeEntityModels(assets, renderer);
    SessionScenario::world(host).setThreadPool(nullptr);
    const auto id = host.createWorld("LAN replica", 42, GameMode::Survival, Difficulty::Normal, true, WorldType::Superflat);
    host.startWorld(id, true, 0); SessionScenario::prepareLanScene(host);
    require(host.openLanRoom(0, 2, true), "authoritative world starts replica integration room");
    client.configureLod({false, 16, LodAggressiveness::PowerSaver, LodPrecision::Low});
    require(client.joinLanRoom("::1", host.lanPort(), 0), "client GameSession starts a room connection");
    double now = 0;
    auto pump = [&]() {
        now += .005;
        host.pollLan(now); client.pollLan(now);
        SessionScenario::world(client).processCompletedGenerations();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    for (int i = 0; i < 500 && (!client.lanWorldReady() || !client.worldState().isGeneratedAt(0, 0)); ++i) pump();
    require(!client.lanConnectionFailed() && client.lanWorldReady(), "client validates bootstrap and owner state");
    require(client.worldState().replicaMode() && !client.hasWorldStore() &&
            client.worldState().getBlock(0, 0, 0) == BlockId::STONE,
            "client receives authoritative near terrain without a local save or generator");
    require(client.remotePlayers().size() == 1 && client.remotePlayers().front().id == 0,
            "client receives host presentation separately from its private inventory");
    SessionScenario::world(host).setBlock(0, 1, 1, BlockId::DIAMOND_ORE);
    for (int i = 0; i < 100 && client.worldState().getBlock(0, 1, 1) != BlockId::DIAMOND_ORE; ++i) pump();
    require(client.worldState().getBlock(0, 1, 1) == BlockId::DIAMOND_ORE,
            "server block edit travels as a revisioned terrain update");
    SessionScenario::corruptReplicaRevision(client);
    SessionScenario::world(host).setBlock(0, 1, 1, BlockId::GLASS);
    for (int i = 0; i < 200 && client.worldState().getBlock(0, 1, 1) != BlockId::GLASS; ++i) pump();
    require(client.worldState().getBlock(0, 1, 1) == BlockId::GLASS,
            "revision mismatch requests and installs authoritative recovery snapshot");
    const auto joined = host.takeLanChat();
    require(joined.size() == 1 && joined.front().kind == Lan::ChatKind::Joined, "admission produces one room join notification");
    client.takeLanChat();
    require(client.sendLanChat("合作建造"), "guest queues valid UTF-8 room chat");
    for (int i = 0; i < 20; ++i) pump();
    const auto receivedChat = host.takeLanChat(); const auto echoedChat = client.takeLanChat();
    require(receivedChat.size() == 1 && echoedChat.size() == 1 && receivedChat.front().text == "合作建造" &&
            receivedChat.front().nickname == client.lanNickname(), "server supplies the guest identity and broadcasts one chat echo");
    require(!client.sendLanChat(std::string(385, 'x')) && !client.sendLanChat("bad\nmessage"), "oversized and control-character chat is rejected locally");
    require(host.sendLanChat("Hello"), "host shares chat through the same room channel");
    for (int i = 0; i < 20; ++i) pump();
    require(client.takeLanChat().size() == 1, "host chat reaches guests"); host.takeLanChat();
    int queuedChat = 0;
    for (int i = 0; i < 20; ++i) if (client.sendLanChat("burst")) ++queuedChat;
    require(queuedChat == 4, "chat burst uses a bounded per-player token allowance");
    for (int i = 0; i < 20; ++i) pump();
    require(host.takeLanChat().size() == 4, "authority rate limit preserves bounded chat without disconnecting");
    client.takeLanChat();
    auto& guest = SessionScenario::guest(host, SessionScenario::replicaPeer(client));
    SessionScenario::tickLanPlayers(host, .01f);
    guest.player.inventory().slot(0) = {ItemId::DIAMOND, 3, 0};
    guest.lastStateSent = -1;
    for (int i = 0; i < 20; ++i) pump();
    client.openInventoryWindow(InventoryWindowKind::Player);
    InventoryAction take; take.operation = InventoryOperation::Click;
    client.submitInventoryAction(take);
    require(client.inventory().slot(0).count == 3, "inventory click does not predict a client stack mutation");
    for (int i = 0; i < 100 && client.inventoryWindowPending(); ++i) pump();
    require(guest.window.cursor().count == 3 && client.inventoryWindow().cursor.count == 3 && client.inventory().slot(0).empty(),
            "queued open and click are serialized against acknowledged window revisions");
    InventoryAction grant; grant.operation = InventoryOperation::CreativeGrant; grant.argument = static_cast<uint16_t>(ItemId::STONE);
    client.submitInventoryAction(grant);
    for (int i = 0; i < 100 && client.inventoryWindowPending(); ++i) pump();
    require(guest.window.cursor().count == 3 && guest.player.inventory().count(ItemId::STONE) == 0,
            "survival guest cannot forge a creative grant");
    InventoryAction place; place.operation = InventoryOperation::Click; place.slot.index = 1;
    client.submitInventoryAction(place);
    client.closeInventoryWindow();
    for (int i = 0; i < 100 && client.inventoryWindowPending(); ++i) pump();
    require(guest.window.cursor().empty() && guest.player.inventory().slot(1).count == 3 && client.inventory().slot(1).count == 3,
            "close follows the confirmed place without losing or duplicating held contents");
    guest.player.setPosition({.5, 1.01, .5}); guest.player.setOrientation(0, 0);
    SessionScenario::world(host).setBlock(0, 2, 2, BlockId::CHEST);
    auto* chest = SessionScenario::world(host).getBlockEntity({0, 2, 2});
    require(chest != nullptr, "shared chest fixture exists");
    chest->chest[0] = {ItemId::EMERALD, 2, 0};
    client.openInventoryWindow(InventoryWindowKind::Container, {0, 2, 2});
    for (int i = 0; i < 100 && client.inventoryWindowPending(); ++i) pump();
    require(client.inventoryWindow().container && client.inventoryWindow().container->chest[0].count == 2,
            "server validates reachable chest and replicates its contents");
    const auto stale = client.inventoryWindow();
    chest->chest[0].count = 1;
    Lan::GameAction raced; raced.sequence = guest.actionAcknowledged + 1; raced.epoch = guest.chunkEpoch; raced.window = stale.id;
    raced.inventory.operation = InventoryOperation::Click; raced.inventory.slot.area = InventoryArea::Container;
    raced.inventory.revision = stale.revision; raced.inventory.containerRevision = stale.containerRevision;
    SessionScenario::sendRawAction(client,raced);
    for(int i=0;i<40;++i)pump();
    // A duplicate-sequence close advances the normal client queue past the
    // deliberately forged packet without writing its private sequence counter.
    client.closeInventoryWindow();for(int i=0;i<40;++i)pump();
    require(guest.window.cursor().empty() && chest->chest[0].count == 1,
            "shared container change invalidates the old transaction atomically");
    guest.player.setPosition({100.5, 1.01, .5}); guest.lastStateSent = -1;
    for (int i = 0; i < 20; ++i) pump();
    require(client.inventoryWindow().kind == InventoryWindowKind::Closed, "distant container closes on the authority");
    guest.player.setPosition({.5, 1.01, .5}); guest.lastStateSent = -1;
    for (int i = 0; i < 20; ++i) pump();
    {
        auto& stationWorld = SessionScenario::world(host);
        stationWorld.setBlock(0,2,2,BlockId::ANVIL);
        guest.player.experience().setLevel(10);
        ItemStack tool{ItemId::IRON_PICKAXE,1,100};
        tool.components.enchantments.set(EnchantmentId::Mending,1);
        guest.player.inventory().slot(0) = tool;
        guest.player.inventory().slot(1) = {ItemId::IRON_INGOT,1};
        guest.lastStateSent = -1;
        for (int i=0;i<20;++i) pump();
        client.openInventoryWindow(InventoryWindowKind::Container,{0,2,2});
        for (int i=0;i<100&&client.inventoryWindowPending();++i) pump();
        const auto move = [&](InventoryArea area,uint8_t index) {
            InventoryAction action; action.slot={area,index}; client.submitInventoryAction(action);
            for (int i=0;i<40;++i) pump();
        };
        move(InventoryArea::Storage,0); move(InventoryArea::Container,0);
        move(InventoryArea::Storage,1); move(InventoryArea::Container,1);
        require(client.inventoryWindow().container && client.inventoryWindow().container->workCost==1,
                "LAN guest receives authority-computed anvil preview");
        move(InventoryArea::Container,2);
        require(guest.player.experience().level()==9 && guest.window.cursor().damage<100 &&
                guest.window.cursor().enchantment(EnchantmentId::Mending)==1,
                "LAN output commits repair, components, material and experience together");
        client.closeInventoryWindow(); for (int i=0;i<40;++i) pump();
        require(guest.window.cursor().empty(),"LAN station close returns its repaired cursor");
    }
    SessionScenario::world(host).setBlock(2, 1, 0, bedBlock(BedPart::Foot, BedDirection::South));
    SessionScenario::world(host).setBlock(2, 1, 1, bedBlock(BedPart::Head, BedDirection::South));
    SessionScenario::setNight(host);
    guest.loading = false;
    require(SessionScenario::beginGuestSleep(host, SessionScenario::replicaPeer(client), {2, 1, 0}), "guest can enter a valid unoccupied bed");
    guest.wantsMorning = true;
    require(!SessionScenario::skipLanNight(host, DimensionId::Overworld) && host.isNight(),
            "one guest cannot skip the night while the awake host counts toward 100 percent");
    auto rules = host.metadata().gameRules; rules.set(GameRuleId::PlayersSleepingPercentage, {GameRuleType::Integer, 50});
    SessionScenario::setRules(host, rules);
    require(SessionScenario::skipLanNight(host, DimensionId::Overworld) && !host.isNight() && guest.sleep == 3,
            "sleep percentage considers eligible players in the dimension");
    SessionScenario::tickLanSleep(host, .4f);
    require(guest.sleep == 0 && guest.profile.bedSpawn == glm::ivec3(2, 1, 0), "guest wake and personal bed spawn are independent");
    guest.player.setPosition({.5, 1.01, .5}); guest.lastStateSent = -1;
    for (int i = 0; i < 20; ++i) pump();
    require(client.metadata().gameRules.integer(GameRuleId::PlayersSleepingPercentage) == 50,
            "authority rule changes reach replicas after bootstrap");
    SessionScenario::world(host).setBlock(0, 2, 2, BlockId::AIR);
    WorldMetadata::PersistedEntity savedVillager;
    savedVillager.type = static_cast<uint8_t>(EntityType::Villager); savedVillager.position = {.5, 1.01, 2.5}; savedVillager.health = 20;
    savedVillager.villager.profession = VillagerProfession::Fletcher;
    SessionScenario::entities(host).loadEntities({savedVillager});
    const auto villagerId = host.entityState().entities().back().id;
    for (int i = 0; i < 100 && !client.tradeEntity(villagerId); ++i) pump();
    require(client.tradeEntity(villagerId) && client.tradeEntity(villagerId)->villager.profession == VillagerProfession::Fletcher,
            "server entity spawn and trade appearance reach the replica");
    const auto quote = villagerQuote(savedVillager.villager, 0);
    guest.player.inventory().slot(0) = {quote.input.id, quote.input.count, 0}; guest.lastStateSent = -1;
    guest.player.setOrientation(0, 0); SessionScenario::player(client).setOrientation(0, 0);
    for (int i = 0; i < 20; ++i) pump();
    client.executeTrade(villagerId, 0, client.inventory());
    require(client.inventory().count(quote.output.id) == 0, "client trade never predicts inventory or villager stock");
    for (int i = 0; i < 100 && client.inventoryWindowPending(); ++i) pump();
    require(guest.player.inventory().count(quote.output.id) == quote.output.count &&
            client.inventory().count(quote.output.id) == quote.output.count &&
            host.tradeEntity(villagerId)->villager.uses[0] == 1,
            "reachable trade commits on the server and confirms inventory and stock");
    const auto experiencePoints=[](const EntityManager& entities) {
        uint64_t points=0;for(const auto& entity:entities.entities())if(entity.type==EntityType::ExperienceOrb)
            points+=uint64_t(entity.experienceOrb.value)*entity.experienceOrb.count;
        return points;
    };
    const auto tradeExperience=experiencePoints(host.entityState());
    require(tradeExperience>=3&&tradeExperience<=6,"accepted LAN trade produces player experience orbs");

    guest.player.inventory().slot(0) = {quote.input.id, quote.input.count, 0}; guest.lastStateSent = -1;
    for (int i = 0; i < 20; ++i) pump();
    auto* changedVillager = const_cast<Entity*>(host.tradeEntity(villagerId));
    ++changedVillager->villager.uses[0];
    client.executeTrade(villagerId, 0, client.inventory());
    for (int i = 0; i < 100 && client.inventoryWindowPending(); ++i) pump();
    require(guest.player.inventory().count(quote.input.id) == quote.input.count && changedVillager->villager.uses[0] == 2,
            "changed quote rejects an old trade without consuming input");
    SessionScenario::entities(host).spawnItem({12.5, 1, .5}, {ItemId::EMERALD, 2, 0});
    const auto itemId = host.entityState().entities().back().id;
    for (int i = 0; i < 100 && !client.entityState().entityById(itemId); ++i) pump();
    require(client.entityState().entityById(itemId) && client.entityState().entityById(itemId)->item.count == 2,
            "dropped item snapshot preserves the server stack");
    client.updatePlaying(.01f, nullptr, {}, false);
    SessionScenario::entities(host).spawnArrow({10.5, 2, .5}, {0,0,2}, 2, true);
    SessionScenario::entities(host).primeTnt({9,1,0}, 4, false);
    for (int i = 0; i < 40; ++i) pump();
    client.updatePlaying(.01f, nullptr, {}, false);
    require(std::count_if(client.entityState().entities().begin(),client.entityState().entities().end(),
                [](const Entity& entity){return entity.type!=EntityType::ExperienceOrb;}) == 4 &&
            client.entityState().entities().size()==host.entityState().entities().size() &&
            client.entityState().entityById(itemId)->ageSeconds > 0,
            "replica advances mob animation and item/arrow/TNT presentation without requiring models for transient entities");
    require(experiencePoints(host.entityState())==tradeExperience && experiencePoints(client.entityState())==tradeExperience,
            "rejected LAN trade adds no XP and replica preserves the accepted trade reward");
    for(const auto& entity:host.entityState().entities())if(entity.type==EntityType::ExperienceOrb) {
        const auto* replica=client.entityState().entityById(entity.id);
        require(replica && replica->type==EntityType::ExperienceOrb && replica->experienceOrb.value==entity.experienceOrb.value &&
                replica->experienceOrb.count==entity.experienceOrb.count && replica->ageSeconds>0,
                "experience orb value/count and model-free presentation reach the replica");
    }
    SessionScenario::entities(host).clear();
    for (int i = 0; i < 100 && !client.entityState().entities().empty(); ++i) pump();
    require(client.entityState().entities().empty(), "removed server entities leave the replica");
    client.setLocalControl(true);
    InputState movement; movement.setVirtual(InputAction::MoveForward, 1); movement.update({});
    SessionScenario::player(client).setOrientation(90, 0);
    const auto before = client.playerState().getPosition();
    const auto inventory = client.inventory().count(ItemId::DIAMOND);
    client.handleMovement(movement, .05f);
    require(client.playerState().getPosition().x > before.x, "local client movement predicts before server response");
    client.handleMouseButton(MouseButton::Left, ButtonAction::Press);
    require(client.inventory().count(ItemId::DIAMOND) == inventory && !client.worldState().hasModifiedChunks(),
            "client interaction does not author world edits or inventory");
    for (int i = 0; i < 20; ++i) pump();
    const auto clientFiles=[&] {
        std::set<std::filesystem::path> files;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root/"client"))
            if(entry.is_regular_file())files.insert(std::filesystem::relative(entry.path(),root/"client"));
        return files;
    };
    const auto filesBeforeLod=clientFiles();
    client.configureLod({true, 16, LodAggressiveness::PowerSaver, LodPrecision::Low});
    client.updatePlaying(0,&renderer,{},false);
    const auto pumpLod=[&]{pump();client.updatePlaying(0,&renderer,{},false);};
    const auto waitLod=[&](auto predicate,const char* message) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!predicate()&&std::chrono::steady_clock::now()<deadline)pumpLod();
        require(predicate(),message);
    };
    const auto hasMaterial=[&](const LodTileKey& key,BlockTexture material,int y) {
        return renderer.hasMaterial(client.worldState().lodSubmissions(),key,
            client.playerState().getPosition(),material,float(y),float(y+1));
    };
    const auto saveColdEdit=[&](int x,int z,int y,BlockId block,int lx=0,int lz=0) {
        SaveStore(root/"host/saves"/id).saveChunkOverrides(x,z,
            {{uint32_t((y-Config::WORLD_MIN_Y)*256+lx+lz*16),block}});
    };
    LodTileKey farKey{-4, 0, 0};
    const auto selectedLod = client.worldState().selectedLodTiles();
    for (const auto& key : selectedLod) if (key.level == 0 && key.x < -2 && key.z == 0) { farKey = key; break; }
    require(client.worldState().selectedLodTiles().size() <= Lan::MAX_LOD_SUBSCRIPTIONS,
            "replica LOD selection fits its bounded subscriptions");
    saveColdEdit(farKey.x, 0, 100, BlockId::DIAMOND_ORE);
    const auto coarse = std::find_if(selectedLod.begin(), selectedLod.end(), [](const LodTileKey& key) {
        return key.level == 1 && key.x < -2;
    });
    require(coarse != selectedLod.end(), "fixture selects a negative coarse tile beyond near terrain");
    const int coarseX = coarse->x * 32 + 1, coarseZ = coarse->z * 32 + 1;
    saveColdEdit(coarse->x * 2, coarse->z * 2, 101, BlockId::DIAMOND_ORE, 1, 1);
    waitLod([&]{return hasMaterial(farKey,BlockTexture::DiamondOre,100);},
            "saved cold negative-coordinate edit must reach rendered distant terrain");
    require(!client.worldState().isGeneratedAt(farKey.x*16,0) && !client.hasWorldStore() &&
            !client.worldState().hasModifiedChunks(),
            "replica distant edits must not generate near chunks or author persistent world changes");
    SessionScenario::world(host).setBlock(farKey.x * 16, 100, 0, BlockId::GLASS);
    waitLod([&]{return hasMaterial(farKey,BlockTexture::Glass,100)&&!hasMaterial(farKey,BlockTexture::DiamondOre,100);},
            "persistent edit must replace the older rendered distant material");
    // Subscription cancellation races have a separate real-wire/provider-barrier
    // scenario; this case protects complete GameSession -> renderer delivery.
    waitLod([&]{return hasMaterial(*coarse,BlockTexture::DiamondOre,101);},
            "coarse cold edit must reach the rendered representative column");
    const auto* oldCoarse=renderer.tile(client.worldState().lodSubmissions(),*coarse,client.playerState().getPosition());
    require(oldCoarse!=nullptr,"coarse correction fixture has visible geometry");
    const auto previousCoarseHandle=oldCoarse->renderHandle;
    // A gameplay edit occurs after the host loads the saved column, not on an
    // ungenerated placeholder that still reads AIR before its overrides apply.
    auto* loadedCoarse = SessionScenario::world(host).getChunk(coarse->x * 2, coarse->z * 2);
    loadedCoarse->setBlock(1,101,1,BlockId::DIAMOND_ORE);
    loadedCoarse->generated = true; loadedCoarse->lifecycle = Chunk::LifecycleState::Renderable;
    SessionScenario::world(host).setBlock(coarseX, 101, coarseZ, BlockId::AIR);
    waitLod([&]{
        const auto* current=renderer.tile(client.worldState().lodSubmissions(),*coarse,client.playerState().getPosition());
        return current&&!(current->renderHandle==previousCoarseHandle)&&!hasMaterial(*coarse,BlockTexture::DiamondOre,101);
    },"removing a coarse representative edit must replace its old visible geometry");
    int bites = 0, explosions = 0, interactions = 0; float damage = 0;
    client.setDamageCallback([&](float amount) { damage += amount; });
    guest.player.takeDamage(2); guest.lastStateSent = -1;
    Lan::GameEvent event; event.owner = SessionScenario::replicaPeer(client); event.position = guest.player.getPosition();
    event.kind = Lan::EventKind::Fishing; event.flags = static_cast<uint8_t>(FishingEventKind::Bite);
    SessionScenario::emitLanEvent(host, event);
    event.kind = Lan::EventKind::Explosion; event.flags = 0; SessionScenario::emitLanEvent(host, event);
    event.kind = Lan::EventKind::Interaction; event.flags = 7; SessionScenario::emitLanEvent(host, event);
    for (int i = 0; i < 20; ++i) pump();
    GameSession::Feedback effect;
    effect.playFishing = [&](FishingEventKind kind) { if (kind == FishingEventKind::Bite) ++bites; };
    effect.playExplosion = [&](float,float) { ++explosions; };
    effect.playBlockInteraction = [&](bool metal,bool opening,bool button) { if (metal && opening && button) ++interactions; };
    client.updatePlaying(.01f, nullptr, effect, false);
    require(bites == 1 && explosions == 1 && interactions == 1 && damage == 2,
            "validated gameplay events reach owner feedback once without client simulation");
    const auto oldEpoch = client.lanStreamEpoch();
    SessionScenario::travelLan(host, guest, DimensionId::Heaven);
    const auto heavenPosition = guest.player.getPosition();
    for (int i = 0; i < 40 && client.lanStreamEpoch() == oldEpoch; ++i) pump();
    require(client.lanStreamEpoch() > oldEpoch && client.metadata().activeDimension == DimensionId::Heaven &&
            host.metadata().activeDimension == DimensionId::Overworld && client.roomRoster().size() == 2 &&
            client.roomRoster().front().dimension == DimensionId::Overworld && client.remotePlayers().empty(),
            "independent travel advances terrain epoch and keeps the global roster across dimensions");
    client.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
    require(!client.advanceLoading(&renderer, RuntimeClock{}.now()) && guest.loading,
            "new dimension cannot satisfy its loading gate with previous-dimension chunks");
    bool heavenReady = false;
    for (int i = 0; i < 4000 && !heavenReady; ++i) {
        host.updatePlaying(.01f, nullptr, {}, false); pump();
        heavenReady = client.advanceLoading(&renderer, RuntimeClock{}.now());
    }
    require(heavenReady && !guest.loading && client.worldState().isGeneratedAt(
                static_cast<int>(heavenPosition.x), static_cast<int>(heavenPosition.z)),
            "guest-only Heaven generation supplies fresh terrain through the complete client loading gate");
    const auto savedHeavenPosition = guest.player.getPosition();
    host.closeLanRoom();
    for (int i = 0; i < 500 && !client.lanConnectionFailed(); ++i) pump();
    require(client.lanConnectionFailed(), "room closure ends the guest session");
    client.leaveWorld();
    require(host.openLanRoom(0, 2, true) && client.joinLanRoom("::1", host.lanPort(), now), "guest manually rejoins the retained world");
    for (int i = 0; i < 200 && !client.lanWorldReady(); ++i) pump();
    require(client.lanWorldReady() && client.metadata().activeDimension == DimensionId::Heaven &&
            glm::distance(client.playerState().getPosition(), savedHeavenPosition) < .01,
            "persistent identity restores its independent dimension and position after reconnect");
    auto& rejoined = SessionScenario::guest(host, SessionScenario::replicaPeer(client));
    rejoined.player.takeDamage(100); host.updatePlaying(.01f, nullptr, {}, false);
    for (int i = 0; i < 30 && !client.isPlayerDead(); ++i) pump();
    require(client.isPlayerDead(), "independent guest death reaches the replica");
    SessionScenario::world(host).setBlock(2,1,0,BlockId::AIR);
    SessionScenario::world(host).setBlock(2,1,1,BlockId::AIR);
    const auto deathEpoch = client.lanStreamEpoch(); client.respawn(RuntimeClock{}.now());
    for (int i = 0; i < 40 && client.lanStreamEpoch() == deathEpoch; ++i) pump();
    require(client.lanStreamEpoch() > deathEpoch && !client.isPlayerDead() &&
            client.metadata().activeDimension == DimensionId::Overworld, "guest respawn returns to Overworld with a fresh stream epoch");
    // The retained fixture uses no generator; supply its full requested near target.
    for (int z = -2; z <= 2; ++z) for (int x = -2; x <= 2; ++x) {
        auto* chunk = SessionScenario::world(host).getChunk(x,z);
        if (!chunk->generated.load()) {
            for (int lz = 0; lz < 16; ++lz) for (int lx = 0; lx < 16; ++lx) chunk->setBlock(lx,0,lz,BlockId::STONE);
            chunk->generated = true; chunk->lifecycle = Chunk::LifecycleState::Renderable;
        }
    }
    bool respawnReady = false;
    for (int i = 0; i < 1000 && !respawnReady; ++i) {
        SessionScenario::tickLanPlayers(host, .01f); pump();
        respawnReady = client.advanceLoading(&renderer, RuntimeClock{}.now());
    }
    if (!respawnReady) std::cerr << SessionScenario::replicaLoadingDiagnostic(client) << '\n';
    require(respawnReady, "guest respawn waits for fresh complete terrain through the client loading gate");
    require(!rejoined.loading && !rejoined.profile.bedSpawn, "destroyed guest bed clears its spawn and loading status");
    require(rejoined.player.survivalStats().health() == 20, "guest respawn restores health");
    require(glm::distance(rejoined.player.getPosition(), glm::dvec3(.5,1.01,.5)) < .05, "destroyed guest bed falls back to world spawn");
    auto immediateRules = host.metadata().gameRules;
    immediateRules.set(GameRuleId::ImmediateRespawn, {GameRuleType::Boolean,1});
    immediateRules.set(GameRuleId::KeepInventory, {GameRuleType::Boolean,1});
    SessionScenario::setRules(host, immediateRules);
    rejoined.player.inventory().slot(0) = {ItemId::DIAMOND,3,0};
    const auto immediateEpoch = client.lanStreamEpoch();
    rejoined.player.takeDamage(100); host.updatePlaying(.01f, nullptr, {}, false);
    for (int i = 0; i < 40 && client.lanStreamEpoch() == immediateEpoch; ++i) pump();
    require(!rejoined.dead && rejoined.player.survivalStats().health() == 20 && client.lanStreamEpoch() > immediateEpoch &&
            !client.isPlayerDead() && client.inventory().count(ItemId::DIAMOND) == 3,
            "ImmediateRespawn and KeepInventory apply to guests through the same authority and fresh epoch");
    client.leaveWorld(); host.closeLanRoom(); host.leaveWorld();
    require(clientFiles()==filesBeforeLod,"client departure removes transient files without touching pre-existing identity data");
    std::filesystem::remove_all(root);
    std::cout << "LAN game replica integration tests passed\n";
    return 0;
}
}
