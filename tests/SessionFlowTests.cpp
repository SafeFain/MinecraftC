#include "app/GameSession.h"
#include "EntityAiScenarios.h"
#include "EntityAiIntegration.h"
#include "Config.h"
#include "core/RuntimeClock.h"
#include "game/Command.h"
#include "game/Localization.h"
#include "entity/EntityManager.h"
#include "world/Chunk.h"

#include <glm/glm.hpp>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

struct GameSessionTestAccess {
    static void processCompletedGenerations(GameSession& session) {
        session.world.processCompletedGenerations();
    }
    static bool backgroundWorkIdle(const GameSession& session) {
        return session.threadPool.idle();
    }
    static bool terrainReady(const GameSession& session) {
        return session.terrainGenerated;
    }
    static bool newWorldLoading(const GameSession& session) {
        return session.loadingNewWorld;
    }
    static bool hasDimensionStore(const GameSession& session) {
        return session.dimensionSaveStore != nullptr;
    }
    static void markTerrainReady(GameSession& session) {
        session.terrainGenerated = true;
    }
    static void setPlayerPosition(GameSession& session, const glm::dvec3& position) {
        session.player.setPosition(position);
    }
    static void safeSpawn(GameSession& session) { session.safeSpawn(); }
    static void beginAutosave(GameSession& session,
                              const std::function<void()>& onError) {
        session.beginAutosave(onError);
    }
    static void processAutosave(GameSession& session,
                                const std::function<void()>& onError) {
        session.processAutosave(onError);
    }
    static void setDay(GameSession& session) { session.dayNightCycle.setDay(); }
    static void setNight(GameSession& session) { session.dayNightCycle.setNight(); }
    static void setHeavenSafePosition(GameSession& session,
                                      const glm::ivec3& position) {
        session.worldMetadata.heaven.safePosition = position;
        session.worldMetadata.heaven.hasSafePosition = true;
    }
};

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
    for (int i = 0; i < 4000; ++i) {
        GameSessionTestAccess::processCompletedGenerations(session);
        if (GameSessionTestAccess::backgroundWorkIdle(session)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(false, "generation drains before session teardown");
}
}

// Session command execution reads localization strings; returning keys
// verbatim keeps this flow test free of font and asset loading.
std::string Localization::text(std::string_view key) const {
    return std::string(key);
}
std::string Localization::format(
    std::string_view key,
    std::initializer_list<std::string> /*arguments*/) const {
    return std::string(key);
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--ai-demo")
        return EntityAiScenarios::writeDemo(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--ai-tests")
        return EntityAiScenarios::integration(argv[2]);
    if (argc > 2 && std::string(argv[1]) == "--ai-benchmark")
        return EntityAiScenarios::benchmark(argv[2]);
    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-session-flow-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    Localization localization;
    RuntimeClock clock;

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
        GameSession session(root / "saves");
        const std::string id = session.createWorld(
            "Flow Test", 42, GameMode::Survival, Difficulty::Normal, true);
        require(!id.empty(), "world creation returns an id");
        require(std::filesystem::exists(root / "saves" / id),
                "world directory exists on disk");

        // New-world start enters the loading state with matching rules.
        const GameMode mode = session.startWorld(id, true, clock.now());
        require(mode == GameMode::Survival,
                "new world starts in survival mode");
        require(GameSessionTestAccess::newWorldLoading(session) && !GameSessionTestAccess::terrainReady(session),
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
        GameSessionTestAccess::setPlayerPosition(session, {100000.0, 64.0, 100000.0});
        GameSessionTestAccess::safeSpawn(session);
        require(session.playerState().getPosition().y ==
                    Config::SEA_LEVEL + 1.01f,
                "safe spawn creates a platform without ground");

        // Autosave begin/process round trips without error.
        bool autosaveError = false;
        GameSessionTestAccess::beginAutosave(session,
            [&autosaveError] { autosaveError = true; });
        require(!autosaveError, "autosave begins without error");
        GameSessionTestAccess::processAutosave(session,
            [&autosaveError] { autosaveError = true; });
        require(!autosaveError, "autosave processes without error");

        // Explicit save persists session metadata.
        bool saveError = false;
        GameSessionTestAccess::markTerrainReady(session);
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
        // Reopening the world restores its mode and saved position.
        GameSession reopened(root / "saves");
        const auto worlds = reopened.listWorlds();
        require(worlds.size() == 1, "reopened catalog lists the world");
        const GameMode mode =
            reopened.startWorld(worlds[0].id, false, clock.now());
        require(mode == GameMode::Survival,
                "existing world restores its saved mode");
        require(!GameSessionTestAccess::newWorldLoading(reopened),
                "existing world loads without the new-world flag");
        const glm::dvec3 position = reopened.playerState().getPosition();
        require(position.x == 100000.0 && position.z == 100000.0 &&
                    position.y == Config::SEA_LEVEL + 1.01f,
                "existing world restores the saved player position");
        drainGeneration(reopened);
    }

    {
        GameSession flat(root / "flat-saves");
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
        GameSession dimensions(root / "dimension-saves");
        const std::string id = dimensions.createWorld(
            "Dimension Test", 999, GameMode::Creative, Difficulty::Normal,
            true);
        dimensions.startWorld(id, true, clock.now());
        GameSessionTestAccess::markTerrainReady(dimensions);
        GameSessionTestAccess::setNight(dimensions);
        require(dimensions.switchDimension(DimensionId::Heaven, clock.now()),
                "session switches into heaven");
        require(dimensions.activeDimension() == DimensionId::Heaven &&
                    dimensions.worldState().isHeaven() && GameSessionTestAccess::hasDimensionStore(dimensions),
                "heaven switch installs its generator and data store");
        require(!dimensions.daylightState().isNight(),
                "heaven starts with its independent day phase");
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
        GameSessionTestAccess::markTerrainReady(dimensions);
        GameSessionTestAccess::setDay(dimensions);
        GameSessionTestAccess::setHeavenSafePosition(dimensions, {8, 128, -4});
        GameSessionTestAccess::setPlayerPosition(dimensions,
            {24.5, static_cast<double>(Config::WORLD_MIN_Y - 3), 24.5});
        require(dimensions.handleVoidFall(clock.now(), {}),
                "heaven void fall switches back to overworld");
        require(dimensions.activeDimension() == DimensionId::Overworld &&
                    !dimensions.worldState().isHeaven() && dimensions.daylightState().isNight(),
                "return switch restores the overworld generator");
        require(dimensions.metadata().heaven.playerPosition ==
                    glm::dvec3(8.5, 128.01, -3.5),
                "void return preserves the last grounded heaven position");
        require(dimensions.switchDimension(DimensionId::Heaven, clock.now()),
                "session can return to heaven after a void fall");
        require(dimensions.playerState().getPosition() ==
                    glm::dvec3(8.5, 128.01, -3.5),
                "heaven re-entry does not restore the void position");
        dimensions.leaveWorld();
        drainGeneration(dimensions);
    }

    std::filesystem::remove_all(root);
    std::cout << "Game session flow tests passed\n";
}
