#include "app/GameSession.h"
#include "Config.h"
#include "core/RuntimeClock.h"
#include "game/Command.h"
#include "game/Localization.h"

#include <glm/glm.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

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
        session.world.processCompletedGenerations();
        if (session.threadPool.idle()) return;
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

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-session-flow-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    Localization localization;
    RuntimeClock clock;

    {
        GameSession session(root / "saves");
        const std::string id = session.worldCatalog.create(
            "Flow Test", 42, GameMode::Survival, Difficulty::Normal, true);
        require(!id.empty(), "world creation returns an id");
        require(std::filesystem::exists(root / "saves" / id),
                "world directory exists on disk");

        // New-world start enters the loading state with matching rules.
        const GameMode mode = session.startWorld(id, true, clock.now());
        require(mode == GameMode::Survival,
                "new world starts in survival mode");
        require(session.loadingNewWorld && !session.terrainGenerated,
                "new world enters the loading state");
        require(session.worldMetadata.seed == 42,
                "metadata carries the requested seed");
        require(session.player.gameMode() == GameMode::Survival,
                "player rules match the world mode");
        const glm::dvec3 routedSpawn = session.world.findSafeSpawn();
        require(session.player.getPosition() == routedSpawn &&
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
        require(session.player.gameMode() == GameMode::Creative &&
                    session.worldMetadata.gameMode == GameMode::Creative,
                "player and metadata switch to creative together");
        result = runCommand(session, localization, "/gamemode 3");
        require(result.gameModeChanged == GameMode::Spectator &&
                    session.player.gameMode() == GameMode::Spectator,
                "player switches to spectator");
        result = runCommand(session, localization, "/gamemode 0");
        require(result.gameModeChanged == GameMode::Survival &&
                    session.player.gameMode() == GameMode::Survival,
                "player switches back to survival");

        // Teleport moves the player exactly and reports a message.
        result = runCommand(session, localization, "/tp 100 64 -200");
        const glm::dvec3 position = session.player.getPosition();
        require(position.x == 100.0 && position.y == 64.0 &&
                    position.z == -200.0,
                "teleport moves the player exactly");
        require(result.messages.size() == 1 &&
                    result.messages[0] == "message.teleported",
                "teleport reports its message");

        // Time presets drive the day/night cycle.
        runCommand(session, localization, "/time set night");
        require(session.dayNightCycle.isNight(), "night preset applies");
        runCommand(session, localization, "/time set day");
        require(!session.dayNightCycle.isNight(), "day preset applies");

        // Weather presets drive the weather state.
        runCommand(session, localization, "/weather thunder");
        require(session.weather.thundering(), "thunder preset applies");
        runCommand(session, localization, "/weather clear");
        require(!session.weather.raining() && !session.weather.thundering(),
                "clear weather preset applies");

        // Biome locate routes to a message without requiring generation.
        result = runCommand(session, localization, "/locate biome plains");
        require(!result.messages.empty(),
                "biome locate reports a found or not-found message");
        result = runCommand(
            session, localization, "/locate structure traveler_hut");
        require(!result.messages.empty(),
                "structure locate reports a found or not-found message");

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
        session.player.setPosition({100000.0, 64.0, 100000.0});
        session.safeSpawn();
        require(session.player.getPosition().y ==
                    Config::SEA_LEVEL + 1.01f,
                "safe spawn creates a platform without ground");

        // Autosave begin/process round trips without error.
        bool autosaveError = false;
        session.beginAutosave([&autosaveError] { autosaveError = true; });
        require(!autosaveError, "autosave begins without error");
        session.processAutosave([&autosaveError] { autosaveError = true; });
        require(!autosaveError, "autosave processes without error");

        // Explicit save persists session metadata.
        bool saveError = false;
        session.terrainGenerated = true;
        session.saveNow([&saveError] { saveError = true; });
        require(!saveError, "save completes without error");
        require(session.worldMetadata.playerPosition ==
                    session.player.getPosition(),
                "metadata reflects the current player position");

        // The catalog sees the persisted world.
        const auto worlds = session.worldCatalog.list();
        require(worlds.size() == 1 && worlds[0].id == id &&
                    worlds[0].seed == 42,
                "catalog lists the saved world with its seed");
        require(worlds[0].mode == GameMode::Survival,
                "catalog preserves the saved game mode");

        session.leaveWorld();
        require(!session.saveStore, "leaving the world detaches the store");
        drainGeneration(session);
    }

    {
        // Reopening the world restores its mode and saved position.
        GameSession reopened(root / "saves");
        const auto worlds = reopened.worldCatalog.list();
        require(worlds.size() == 1, "reopened catalog lists the world");
        const GameMode mode =
            reopened.startWorld(worlds[0].id, false, clock.now());
        require(mode == GameMode::Survival,
                "existing world restores its saved mode");
        require(!reopened.loadingNewWorld,
                "existing world loads without the new-world flag");
        const glm::dvec3 position = reopened.player.getPosition();
        require(position.x == 100000.0 && position.z == 100000.0 &&
                    position.y == Config::SEA_LEVEL + 1.01f,
                "existing world restores the saved player position");
        drainGeneration(reopened);
    }

    {
        GameSession flat(root / "flat-saves");
        const std::string id = flat.worldCatalog.create(
            "Flat Test", 123, GameMode::Creative, Difficulty::Normal,
            false, WorldType::Superflat);
        flat.startWorld(id, true, clock.now());
        require(flat.worldMetadata.worldType == WorldType::Superflat,
                "session carries the superflat type into world metadata");
        require(flat.player.getPosition().y ==
                    static_cast<double>(Config::WORLD_MIN_Y + 3) + 1.01,
                "superflat spawn is directly above the grass layer");
        drainGeneration(flat);
        flat.leaveWorld();
    }

    {
        GameSession dimensions(root / "dimension-saves");
        const std::string id = dimensions.worldCatalog.create(
            "Dimension Test", 999, GameMode::Creative, Difficulty::Normal,
            true);
        dimensions.startWorld(id, true, clock.now());
        dimensions.terrainGenerated = true;
        dimensions.dayNightCycle.setNight();
        require(dimensions.switchDimension(DimensionId::Heaven, clock.now()),
                "session switches into heaven");
        require(dimensions.activeDimension() == DimensionId::Heaven &&
                    dimensions.world.isHeaven() && dimensions.dimensionSaveStore,
                "heaven switch installs its generator and data store");
        require(!dimensions.dayNightCycle.isNight(),
                "heaven starts with its independent day phase");
        require(dimensions.entities.entities().empty(),
                "heaven starts without natural entities");
        dimensions.terrainGenerated = true;
        dimensions.dayNightCycle.setDay();
        dimensions.worldMetadata.heaven.safePosition = {8, 128, -4};
        dimensions.worldMetadata.heaven.hasSafePosition = true;
        dimensions.player.setPosition(
            {24.5, static_cast<double>(Config::WORLD_MIN_Y - 3), 24.5});
        require(dimensions.handleVoidFall(clock.now(), {}),
                "heaven void fall switches back to overworld");
        require(dimensions.activeDimension() == DimensionId::Overworld &&
                    !dimensions.world.isHeaven() && dimensions.dayNightCycle.isNight(),
                "return switch restores the overworld generator");
        require(dimensions.worldMetadata.heaven.playerPosition ==
                    glm::dvec3(8.5, 128.01, -3.5),
                "void return preserves the last grounded heaven position");
        require(dimensions.switchDimension(DimensionId::Heaven, clock.now()),
                "session can return to heaven after a void fall");
        require(dimensions.player.getPosition() ==
                    glm::dvec3(8.5, 128.01, -3.5),
                "heaven re-entry does not restore the void position");
        dimensions.leaveWorld();
        drainGeneration(dimensions);
    }

    std::filesystem::remove_all(root);
    std::cout << "Game session flow tests passed\n";
}
