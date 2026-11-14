#include "TestSupport.h"
#include "HeavenContentFixture.h"
#include "app/GameSession.h"
#include "EntityAiScenarios.h"
#include "EntityAiIntegration.h"
#include "EntityCombatTimingIntegration.h"
#include "NetherMobIntegration.h"
#include "VillageLifeIntegration.h"
#include "ProjectileIntegration.h"
#include "Config.h"
#include "core/RuntimeClock.h"
#include "game/Command.h"
#include "game/Localization.h"
#include "entity/EntityManager.h"
#include "entity/EntityPresentation.h"
#include "network/EntityProtocol.h"
#include "world/Chunk.h"

#include <glm/glm.hpp>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>

#include "SessionScenario.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

GameSession::CommandResult runCommand(GameSession& session,
                                      const Localization& localization,
                                      const std::string& command) {
    const CommandParseResult parsed = parseCommand(command);
    require(parsed.command.has_value(),
            ("command parses: " + command).c_str());
    return session.executeCommand(*parsed.command, localization);
}

// Background generation tasks hold raw world pointers and must finish before
// the session is destroyed; drain them the same way the loading gate does.
void drainGeneration(GameSession& session) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline) {
        SessionScenario::processCompletedGenerations(session);
        if (SessionScenario::backgroundWorkIdle(session)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(false, "generation drains before session teardown");
}
}

// Session command execution reads localization strings; returning keys
// verbatim keeps this flow test free of font and asset loading.
std::string Localization::itemName(ItemId item) const {
    return getItemProps(item).name;
}
std::string Localization::text(std::string_view key) const {
    return std::string(key);
}
std::string Localization::format(
    std::string_view key,
    std::initializer_list<std::string> /*arguments*/) const {
    return std::string(key);
}

#include "NetherSessionIntegration.h"
#include "GameRuleIntegration.h"
#include "PluginSessionIntegration.h"
#include "PluginExtensionIntegration.h"
#include "WorldgenV2Integration.h"
#include "GenerationPolicyV2Integration.h"
#include "DimensionIntegration.h"
#include "BlockShapesIntegration.h"
#include "PluginCapabilitiesIntegration.h"
#include "HeavenRemainingP0Integration.h"
#include "MultiplayerSimulationIntegration.h"
#include "LanAuthorityIntegration.h"
#include "LanReplicaIntegration.h"
#include "PluginLanIntegration.h"
#include "HeavenParticleMigration.h"
#include "HeavenP1Integration.h"

#include "ExperienceIntegration.h"
#include "EnchantmentIntegration.h"
#include "RedstoneEntityIntegration.h"

#include "RedstoneLanIntegration.h"

int main(int argc, char** argv) try {
    if(argc==3 && std::string(argv[1])=="--redstone-mob-tests") {
        installHeavenFixture();return RedstoneEntityIntegration::run(argv[2]);
    }
    if(argc==3&&std::string(argv[1])=="--enchantment-tests") { try { return EnchantmentIntegration::run(argv[2]); } catch(const std::exception& error) { std::cerr<<"Equipment progression exception: "<<error.what()<<'\n'; return 1; } }
    if(argc==3&&std::string(argv[1])=="--experience-tests") {
        try{return ExperienceIntegration::run(argv[2]);}
        catch(const std::exception& error){std::cerr<<"Experience test exception: "<<error.what()<<'\n';return 1;}
    }
    if(argc==3 && (std::string(argv[1])=="--nether-mob-tests"||std::string(argv[1])=="--nether-mob-tests-no-plugins")) {
        try {
            const bool withoutPlugins = std::string(argv[1])=="--nether-mob-tests-no-plugins";
            const auto checkNoPlugins = [] {
                if(!Plugins::content().requirements.empty() ||
                   !Plugins::content().blocks.empty() || !Plugins::content().items.empty() ||
                   Plugins::validDimension(static_cast<uint32_t>(DimensionId::Heaven)))
                    throw std::runtime_error("native Nether test unexpectedly depends on loaded plugin content");
            };
            if(withoutPlugins)checkNoPlugins();else installHeavenFixture();
            const int result = NetherMobIntegration::run(argv[2]);
            if(withoutPlugins)checkNoPlugins();
            return result;
        }
        catch(const std::exception& error){std::cerr<<"Nether integration exception: "<<error.what()<<'\n';return 1;}
    }
    if(argc==2&&std::string(argv[1])=="--nether-session-tests") {
        try{return NetherSessionIntegration::run();}
        catch(const std::exception& error){std::cerr<<"Native session exception: "<<error.what()<<'\n';return 1;}
    }
    installHeavenFixture();
    if(argc==6&&std::string(argv[1])=="--heaven-p1-tests") {
        try{return HeavenP1Integration::run(argv[2],{{argv[3],argv[4],argv[5]}});}
        catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if(argc==2&&std::string(argv[1])=="--heaven-particle-migration") {
        try{return HeavenParticleMigration::run();}
        catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if(argc==4&&std::string(argv[1])=="--remaining-p0-tests"){
        try{return HeavenRemainingP0::run(argv[2],argv[3]);}
        catch(const std::exception& error){std::cerr<<"Remaining P0 regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[1])=="--policy-v2-tests") {
        try{return GenerationPolicyV2Integration::run(argv[2],argv[3],argv[4]);}
        catch(const std::exception& error){std::cerr<<"Generation policy V2 failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==4&&std::string(argv[1])=="--capability-tests") {
        try{return PluginCapabilitiesIntegration::run(argv[2],argv[3]);}
        catch(const std::exception& error){std::cerr<<"Capability regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==4&&std::string(argv[1])=="--shape-tests") {
        try{return BlockShapesIntegration::run(argv[2],argv[3]);}
        catch(const std::exception& error){std::cerr<<"Shape regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==4&&std::string(argv[1])=="--dimension-tests") {
        try{return DimensionIntegration::run(argv[2],argv[3]);}
        catch(const std::exception& error){std::cerr<<"Dimension regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[1])=="--worldgen-v2-tests") {
        try{return WorldgenV2Integration::run(argv[2],argv[3],argv[4]);}
        catch(const std::exception& error){std::cerr<<"Worldgen V2 regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[1])=="--extension-tests") {
        try{return PluginExtensionIntegration::run(argv[2],argv[3],argv[4]);}
        catch(const std::exception& error){std::cerr<<"Plugin extension regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==5&&std::string(argv[1])=="--plugin-lan-tests") {
        try{return PluginLanIntegration::run(argv[2],argv[3],argv[4]);}
        catch(const std::exception& error){std::cerr<<"Plugin LAN regression failed: "<<error.what()<<'\n';return 1;}
    }
    if(argc==2 && std::string(argv[1])=="--redstone-lan-tests")return RedstoneLanIntegration::run();
    if (argc > 2 && std::string(argv[1]) == "--lan-replica-tests")
        return LanReplicaIntegration::run(argv[2]);
    if (argc > 1 && std::string(argv[1]) == "--lan-authority-tests")
        return LanAuthorityIntegration::run();
    if (argc > 2 && std::string(argv[1]) == "--lan-simulation-tests")
        return MultiplayerSimulationIntegration::run(argv[2]);
    if(argc>2&&std::string(argv[1])=="--plugin-tests")return PluginSessionIntegration::run(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--gamerule-tests") return GameRuleIntegration::run(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--projectile-tests")
        return ProjectileIntegration::run(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--ai-demo")
        return EntityAiScenarios::writeDemo(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--ai-tests")
        { EntityAiScenarios::villageLife(argv[2]); return EntityAiScenarios::integration(argv[2]); }
    if (argc > 2 && std::string(argv[1]) == "--combat-timing-tests")
        return EntityCombatTimingTests::run(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--ai-benchmark")
        return EntityAiScenarios::benchmark(argv[2]);
    if(argc!=1){std::cerr<<"Unknown session test arguments\n";return 2;}
    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-session-flow-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    Localization localization;
    RuntimeClock clock;

    {
        const int distance=Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE=0;
        ScenarioSession fishingSession(root/"fishing");
        SessionScenario::fishingPond(fishingSession,GameMode::Survival);
        for(size_t i=1;i<InventoryModel::STORAGE_SIZE;++i)
            fishingSession.inventory().slot(i)={ItemId::STONE,64,0};
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        require(fishingSession.fishingState().active(),"Use casts rod through session");
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Release);
        require(fishingSession.fishingState().active(),"Use release does not reel");
        for(int i=0;i<1000 && fishingSession.fishingState().phase!=FishingPhase::Bite;++i)
            SessionScenario::tickFishing(fishingSession,.01f);
        require(fishingSession.fishingState().phase==FishingPhase::Bite,"session pond reaches bite");
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        require(!fishingSession.fishingState().active() && fishingSession.inventory().slot(0).empty(),
            "successful catch breaks worn survival rod");
        const auto& drops=fishingSession.entityState().entities();
        require(std::count_if(drops.begin(),drops.end(),[](const Entity& e){return e.type==EntityType::Item&&e.item.id==ItemId::RAW_COD;})==1 &&
            fishingSession.inventory().count(ItemId::RAW_COD)==0,
            "full inventory keeps catch as normal world drop");
        require(ExperienceIntegration::orbPoints(fishingSession.entityState())==1,"successful fishing produces XP without needing inventory space");
        for(int i=0;i<150;++i)SessionScenario::tickFishingDrops(fishingSession,.01f);
        require(fishingSession.inventory().count(ItemId::RAW_COD)==1,
            "reeled catch follows ordinary entity physics and inventory pickup");
        SessionScenario::fishingPond(fishingSession,GameMode::Creative);
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        for(int i=0;i<1000 && fishingSession.fishingState().phase!=FishingPhase::Bite;++i)
            SessionScenario::tickFishing(fishingSession,.01f);
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        require(fishingSession.inventory().slot(0).damage==63,"creative reel has no rod wear");
        SessionScenario::tickFishing(fishingSession,.3f);
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        fishingSession.setSelectedSlot(1);
        require(!fishingSession.fishingState().active(),"hotbar switch cancels fishing immediately");
        fishingSession.setSelectedSlot(0);
        SessionScenario::tickFishing(fishingSession,.3f);
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        fishingSession.dropSelectedItem(0,true);
        require(!fishingSession.fishingState().active(),"dropping rod cancels fishing immediately");
        SessionScenario::fishingPond(fishingSession,GameMode::Survival);
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        SessionScenario::retireFishingPond(fishingSession);
        SessionScenario::tickFishing(fishingSession,.01f);
        require(!fishingSession.fishingState().active() &&
                !fishingSession.worldState().getLoadedBlock(8,64,8).has_value(),
            "retired warm chunks cannot keep fishing active");
        require(!fishingSession.worldState().getLoadedBlock(-1,64,-1).has_value() &&
                !fishingSession.worldState().getLoadedBlock(8,320,8).has_value(),
            "loaded-block query distinguishes missing negative chunks and build limits");
        SessionScenario::fishingPond(fishingSession,GameMode::Survival);
        fishingSession.handleMouseButton(MouseButton::Right,ButtonAction::Press);
        SessionScenario::die(fishingSession);
        require(!fishingSession.fishingState().active(),"death removes bobber");
        fishingSession.leaveWorld();
        require(!fishingSession.fishingState().active(),"leaving world clears transient fishing");
        Config::RENDER_DISTANCE=distance;
    }

    {
        const int oldRenderDistance = Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE = 0;
        const auto emptyRoot = root / "empty-entity-stream";
        SaveStore store(emptyRoot);
        World world;
        EntityManager entities(world);
        entities.setSaveStore(&store);
        world.update({0.5, 64.0, 0.5}, 1);
        Chunk* chunk = world.getChunk(0, 0);
        chunk->generated = true;
        entities.syncChunks();
        world.update({16.5, 64.0, 0.5}, 1);
        entities.syncChunks();
        require(!std::filesystem::exists(
                    emptyRoot / "entities" / "e.0.0.bin") &&
                    !std::filesystem::exists(
                        emptyRoot / "entities" / "p.0.0.bin"),
                "empty streamed chunk performed unnecessary entity writes");
        Config::RENDER_DISTANCE = oldRenderDistance;
    }

    {
        ScenarioSession session(root / "saves");
        const std::string id = session.createWorld(
            "Flow Test", 42, GameMode::Survival, Difficulty::Normal, true);
        require(!id.empty(), "world creation returns an id");
        require(std::filesystem::exists(root / "saves" / id),
                "world directory exists on disk");

        // New-world start enters the loading state with matching rules.
        const GameMode mode = session.startWorld(id, true, clock.now());
        require(mode == GameMode::Survival,
                "new world starts in survival mode");
        require(SessionScenario::newWorldLoading(session) && !SessionScenario::terrainReady(session),
                "new world enters the loading state");
        require(session.metadata().seed == 42,
                "metadata carries the requested seed");
        require(session.playerState().gameMode() == GameMode::Survival,
                "player rules match the world mode");
        const glm::dvec3 routedSpawn = session.worldState().findSafeSpawn();
        require(session.playerState().getPosition() == routedSpawn &&
                    routedSpawn.y > Config::SEA_LEVEL,
                "new world loading is centered on a dry routed spawn");

        // Help routes without changing state.
        auto result = runCommand(session, localization, "/help");
        require(!result.messages.empty() &&
                    result.messages[0] == "message.help_header",
                "help emits the header message");
        require(!result.gameModeChanged, "help does not change the mode");

        session.inventory().clear();
        SessionScenario::setCheats(session, false);
        result = runCommand(session, localization, "/give stone_bricks 64");
        require(session.inventory().count(ItemId::STONE_BRICKS) == 0 &&
                result.messages[0] == "message.cheats_disabled",
                "give respects the world cheats setting");
        SessionScenario::setCheats(session, true);
        result = runCommand(session, localization, "/give stone_bricks 64");
        require(session.inventory().count(ItemId::STONE_BRICKS) == 64,
                "give adds the requested stack to real inventory");
        for (size_t slot = 0; slot < InventoryModel::STORAGE_SIZE; ++slot)
            session.inventory().slot(slot) = {ItemId::STONE,64,0};
        result = runCommand(session, localization, "/give bone_meal 64");
        require(session.inventory().count(ItemId::BONE_MEAL) == 0,
                "give never overwrites a full inventory");
        session.inventory().clear();

        // Gamemode transitions update player rules and metadata together.
        result = runCommand(session, localization, "/gamemode 1");
        require(result.gameModeChanged == GameMode::Creative,
                "gamemode reports the creative change");
        require(session.playerState().gameMode() == GameMode::Creative &&
                    session.metadata().gameMode == GameMode::Creative,
                "player and metadata switch to creative together");
        result = runCommand(session, localization, "/gamemode 3");
        require(result.gameModeChanged == GameMode::Spectator &&
                    session.playerState().gameMode() == GameMode::Spectator,
                "player switches to spectator");
        result = runCommand(session, localization, "/gamemode 0");
        require(result.gameModeChanged == GameMode::Survival &&
                    session.playerState().gameMode() == GameMode::Survival,
                "player switches back to survival");

        // Teleport moves the player exactly and reports a message.
        result = runCommand(session, localization, "/tp 100 64 -200");
        const glm::dvec3 position = session.playerState().getPosition();
        require(position.x == 100.0 && position.y == 64.0 &&
                    position.z == -200.0,
                "teleport moves the player exactly");
        require(result.messages.size() == 1 &&
                    result.messages[0] == "message.teleported",
                "teleport reports its message");

        // Time presets drive the day/night cycle.
        runCommand(session, localization, "/time set night");
        require(session.daylightState().isNight(), "night preset applies");
        runCommand(session, localization, "/time set day");
        require(!session.daylightState().isNight(), "day preset applies");

        require(session.metadata().dayNightDurationSeconds == 1200,
                "new world defaults to a twenty-minute day and night");
        const float phase = session.daylightState().phase();
        session.updateDaylight(0.1f, false);
        require(session.daylightState().phase() == phase, "paused world time does not advance");
        session.updateDaylight(0.1f, true);
        require(std::abs(session.daylightState().phase() - phase - 0.1f / 1200.0f) < 0.000001f,
                "default world cycle advances in seconds");
        SessionScenario::setCheats(session, false);
        result = runCommand(session, localization, "/gamerule DayNightDuration 180");
        require(session.metadata().dayNightDurationSeconds == 1200 &&
                result.messages[0] == "message.cheats_disabled", "duration requires cheats");
        SessionScenario::setCheats(session, true);
        const float beforeRule = session.daylightState().phase();
        result = runCommand(session, localization, "/gamerule DayNightDuration 180");
        require(session.metadata().dayNightDurationSeconds == 180 &&
                session.daylightState().phase() == beforeRule &&
                result.messages[0] == "message.gamerule_set",
                "duration updates world rule and preserves phase");
        session.updateDaylight(0.1f, true);
        require(std::abs(session.daylightState().phase() - beforeRule - 0.1f / 180.0f) < 0.000001f,
                "world duration controls live cycle speed");

        // Weather presets drive the weather state.
        runCommand(session, localization, "/weather thunder");
        require(session.weatherState().thundering(), "thunder preset applies");
        runCommand(session, localization, "/weather clear");
        require(!session.weatherState().raining() && !session.weatherState().thundering(),
                "clear weather preset applies");

        // Biome locate routes to a message without requiring generation.
        result = runCommand(session, localization, "/locate biome plains");
        require(!result.messages.empty(),
                "biome locate reports a found or not-found message");
        result = runCommand(
            session, localization, "/locate structure traveler_hut");
        require(!result.messages.empty(),
                "structure locate reports a found or not-found message");
        result = runCommand(
            session, localization, "/locate structure cloudspire_tower");
        require(result.messages.size() == 1 &&
                    result.messages[0] == "message.locate_structure_not_found",
                "overworld locate rejects a Heaven-only structure");

        // Parse failures surface structured errors.
        const CommandParseResult unknown = parseCommand("/bogus");
        require(unknown.error &&
                    unknown.error->kind == CommandErrorKind::UnknownCommand,
                "unknown command reports an unknown-command error");
        const CommandParseResult shortTeleport = parseCommand("/tp 1 2");
        require(shortTeleport.error &&
                    shortTeleport.error->kind == CommandErrorKind::Expected,
                "short teleport reports an expected-argument error");

        // Safe spawn on untouched terrain falls back to a platform.
        SessionScenario::setPlayerPosition(session, {100000.0, 64.0, 100000.0});
        SessionScenario::safeSpawn(session);
        require(session.playerState().getPosition().y ==
                    Config::SEA_LEVEL + 1.01f,
                "safe spawn creates a platform without ground");

        // Autosave begin/process round trips without error.
        bool autosaveError = false;
        SessionScenario::beginAutosave(session,
            [&autosaveError] { autosaveError = true; });
        require(!autosaveError, "autosave begins without error");
        SessionScenario::processAutosave(session,
            [&autosaveError] { autosaveError = true; });
        require(!autosaveError, "autosave processes without error");

        SessionScenario::player(session).experience().addPoints(1537);
        // Explicit save persists session metadata.
        bool saveError = false;
        SessionScenario::markTerrainReady(session);
        session.saveNow([&saveError] { saveError = true; });
        require(!saveError, "save completes without error");
        require(session.metadata().playerPosition ==
                    session.playerState().getPosition(),
                "metadata reflects the current player position");

        // The catalog sees the persisted world.
        const auto worlds = session.listWorlds();
        require(worlds.size() == 1 && worlds[0].id == id &&
                    worlds[0].seed == 42,
                "catalog lists the saved world with its seed");
        require(worlds[0].mode == GameMode::Survival,
                "catalog preserves the saved game mode");

        session.leaveWorld();
        require(!session.hasWorldStore(), "leaving the world detaches the store");
        drainGeneration(session);
    }

    {
        WorldCatalog currentCatalog(root / "saves");
        auto currentStore = currentCatalog.open(currentCatalog.list().front().id);
        auto currentMetadata = currentStore.loadMetadata();
        currentMetadata.inventory.slot(8) = {ItemId::CAVE_GLOWSHROOM,23,0};
        currentMetadata.heaven.worldTicks = 6789;
        currentStore.saveMetadata(currentMetadata);
        for (uint32_t version : {14u,15u,16u,17u,18u,19u,20u,21u,22u,WorldGenContext::GENERATION_VERSION+1u}) {
            auto incompatible = currentMetadata;
            incompatible.generationVersion = version;
            currentStore.saveMetadata(incompatible);
            std::ifstream beforeFile(currentStore.worldDirectory()/"level.bin",std::ios::binary);
            const std::string before((std::istreambuf_iterator<char>(beforeFile)),{});
            ScenarioSession refused(root / "saves");
            bool rejected = false;
            try { refused.startWorld(currentCatalog.list().front().id,false,clock.now()); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected && !refused.hasWorldStore(),
                    "incompatible generation never attaches a world store");
            refused.saveNow([] {});
            refused.leaveWorld();
            std::ifstream afterFile(currentStore.worldDirectory()/"level.bin",std::ios::binary);
            const std::string after((std::istreambuf_iterator<char>(afterFile)),{});
            require(before == after,"refused legacy/future save bytes remain untouched");
        }
        currentStore.saveMetadata(currentMetadata);
        // Reopening the world restores its mode and saved position.
        ScenarioSession reopened(root / "saves");
        const auto worlds = reopened.listWorlds();
        require(worlds.size() == 1, "reopened catalog lists the world");
        const GameMode mode =
            reopened.startWorld(worlds[0].id, false, clock.now());
        require(mode == GameMode::Survival,
                "existing world restores its saved mode");
        require(reopened.playerState().experience().level()==31&&reopened.playerState().experience().points()==30,"session save/reopen restores host experience");
        require(reopened.metadata().dayNightDurationSeconds == 180,
                "world duration survives save and reopen");
        require(!SessionScenario::newWorldLoading(reopened),
                "existing world loads without the new-world flag");
        const glm::dvec3 position = reopened.playerState().getPosition();
        require(position.x == 100000.0 && position.z == 100000.0 &&
                    position.y == Config::SEA_LEVEL + 1.01f,
                "existing world restores the saved player position");
        require(reopened.metadata().generationVersion == WorldGenContext::GENERATION_VERSION &&
                reopened.playerState().inventory().slot(8).id == ItemId::CAVE_GLOWSHROOM &&
                reopened.metadata().heaven.worldTicks == 6789,
                "v16 restores new items and independent Heaven state");
        drainGeneration(reopened);
        SessionScenario::markTerrainReady(reopened);
        bool saveError = false;
        reopened.saveNow([&] { saveError = true; });
        require(!saveError && currentStore.loadMetadata().generationVersion == WorldGenContext::GENERATION_VERSION &&
                currentStore.loadMetadata().inventory.slot(8).count == 23,
                "v16 saves new items and current generation version");
        reopened.leaveWorld();
        reopened.startWorld(worlds[0].id,false,clock.now());
        require(reopened.metadata().generationVersion == WorldGenContext::GENERATION_VERSION,
                "v16 world reopens normally");
        drainGeneration(reopened);
    }

    {
        ScenarioSession flat(root / "flat-saves");
        const std::string id = flat.createWorld(
            "Flat Test", 123, GameMode::Creative, Difficulty::Normal,
            false, WorldType::Superflat);
        flat.startWorld(id, true, clock.now());
        require(flat.metadata().worldType == WorldType::Superflat,
                "session carries the superflat type into world metadata");
        require(flat.playerState().getPosition().y ==
                    static_cast<double>(Config::WORLD_MIN_Y + 3) + 1.01,
                "superflat spawn is directly above the grass layer");
        drainGeneration(flat);
        flat.leaveWorld();
    }

    {
        ScenarioSession dimensions(root / "dimension-saves");
        const std::string id = dimensions.createWorld(
            "Dimension Test", 999, GameMode::Creative, Difficulty::Normal,
            true);
        dimensions.startWorld(id, true, clock.now());
        require(dimensions.metadata().dayNightDurationSeconds == 1200,
                "independent world retains default duration");
        runCommand(dimensions, localization, "/gamerule DayNightDuration 73");
        SessionScenario::markTerrainReady(dimensions);
        SessionScenario::setNight(dimensions);
        World* retainedOverworld = &SessionScenario::world(dimensions);
        retainedOverworld->setBlock(0, 200, 0, BlockId::DIAMOND_ORE);
        SessionScenario::entities(dimensions).spawnItem({.5, 202, .5}, {ItemId::DIAMOND, 1, 0});
        const uint64_t retainedItem = dimensions.entityState().entities().back().id;
        require(dimensions.switchDimension(DimensionId::Heaven, clock.now()),
                "session switches into heaven");
        require(dimensions.activeDimension() == DimensionId::Heaven &&
                    dimensions.worldState().isHeaven() && SessionScenario::hasDimensionStore(dimensions),
                "heaven switch installs its generator and data store");
        require(&dimensions.worldState() != retainedOverworld &&
                    retainedOverworld->getBlock(0, 200, 0) == BlockId::DIAMOND_ORE,
                "dimension travel retains original world and edits while inactive");
        require(!dimensions.daylightState().isNight(),
                "heaven starts with its independent day phase");
        require(dimensions.metadata().dayNightDurationSeconds == 73,
                "duration rule is shared across dimensions");
        require(dimensions.entityState().entities().empty(),
                "heaven starts without natural entities");
        auto locateResult = runCommand(
            dimensions, localization, "/locate structure xiguang_ruin");
        require(locateResult.messages.size() == 1 &&
                    locateResult.messages[0] == "message.locate_found",
                "Heaven command locates a dimension structure");
        locateResult = runCommand(
            dimensions, localization, "/locate structure village");
        require(locateResult.messages.size() == 1 &&
                    locateResult.messages[0] ==
                        "message.locate_structure_not_found",
                "Heaven locate rejects an overworld-only structure");
        SessionScenario::markTerrainReady(dimensions);
        SessionScenario::setDay(dimensions);
        auto& heavenWorld=SessionScenario::world(dimensions);
        heavenWorld.update({8.5,128.01,-3.5});heavenWorld.enqueueGeneration();heavenWorld.waitForInitialGeneration(1500);heavenWorld.processCompletedGenerations(false);
        heavenWorld.setBlock(8,127,-4,BlockId::STONE);
        heavenWorld.setBlock(8,128,-4,BlockId::AIR);heavenWorld.setBlock(8,129,-4,BlockId::AIR);
        const auto groundedHeaven=SessionScenario::setHeavenSafePosition(dimensions, {8, 128, -4});
        require(groundedHeaven.x==8.5&&groundedHeaven.z==-3.5&&pluginSafeLanding(heavenWorld,groundedHeaven),
                "Heaven fixture records an actual supported position");
        SessionScenario::setPlayerPosition(dimensions,
            {24.5, static_cast<double>(Config::WORLD_MIN_Y - 3), 24.5});
        require(dimensions.handleVoidFall(clock.now(), {}),
                "heaven void fall switches back to overworld");
        require(dimensions.activeDimension() == DimensionId::Overworld &&
                    !dimensions.worldState().isHeaven() && dimensions.daylightState().isNight(),
                "return switch restores the overworld generator");
        require(&dimensions.worldState() == retainedOverworld &&
                    dimensions.entityState().entityById(retainedItem) != nullptr,
                "return travel retains original world and live entity IDs");
        require(dimensions.metadata().heaven.playerPosition ==
                    groundedHeaven,
                "void return preserves the last grounded heaven position");
        require(dimensions.switchDimension(DimensionId::Heaven, clock.now()),
                "session can return to heaven after a void fall");
        require(dimensions.playerState().getPosition() ==
                    groundedHeaven,
                "heaven re-entry does not restore the void position");
        dimensions.leaveWorld();
        drainGeneration(dimensions);
    }

    {
        // Milestone B: the entity simulation owns no renderer or model stack.
        // A no-op presentation must still support update, persistence and copy.
        World world;
        Player noRenderPlayer(world);
        EntityManager noRenderEntities(world);
        NullEntityPresentation presentation;
        noRenderEntities.setPresentation(&presentation);
        noRenderEntities.setNaturalSpawningEnabled(false);
        require(noRenderEntities.spawnMob(EntityType::Cow, {.5, 1.0, .5}),
                "headless entity simulation spawns a mob");
        noRenderEntities.spawnItem({.5, 3.0, .5}, {ItemId::DIAMOND, 1, 0});
        noRenderPlayer.setPosition({.5, 1.0, 3.5});
        for (int frame = 0; frame < 30; ++frame)
            noRenderEntities.update(noRenderPlayer, 1.0f / 20.0f, true,
                                    false, false, false);
        require(noRenderEntities.entities().size() == 2,
                "headless entity simulation retains live entities");
        const auto saved = noRenderEntities.saveEntities();
        noRenderEntities.clear();
        noRenderEntities.loadEntities(saved);
        require(noRenderEntities.entities().size() == 2,
                "headless entity simulation round-trips records");
    }

    std::filesystem::remove_all(root);
    std::cout << "Game session flow tests passed\n";
} catch (...) {
    return TestSupport::reportUnhandledException(__FILE__);
}
