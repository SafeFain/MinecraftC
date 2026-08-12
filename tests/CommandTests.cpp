#include "game/Command.h"
#include "world/BiomeLocator.h"
#include "game/TextWrap.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const auto englishLines = wrapTextPixels("alpha beta gamma", 10.0f,
        [](const std::string& text) { return static_cast<float>(text.size()); });
    require(englishLines.size() == 2 && englishLines[0] == "alpha beta" &&
            englishLines[1] == "gamma", "English chat did not wrap at a word boundary");
    const auto chineseLines = wrapTextPixels("沼泽丛林恶地", 6.0f,
        [](const std::string& text) {
            return static_cast<float>(utf8CodepointCount(text) * 2);
        });
    require(chineseLines.size() == 2 && chineseLines[0] == "沼泽丛" &&
            chineseLines[1] == "林恶地", "CJK chat was not wrapped on UTF-8 boundaries");

    const auto help = parseCommand("/help");
    require(help.command && help.command->type == CommandType::Help,
            "help command was not parsed");
    const auto locate = parseCommand("/locate biome plain");
    require(locate.command && locate.command->type == CommandType::LocateBiome &&
            locate.command->biome == Biome::PLAINS,
            "plain biome locate command was not parsed");
    const auto jungle = parseCommand("/locate biome jungle");
    require(jungle.command && jungle.command->biome == Biome::JUNGLE,
            "jungle biome locate command was not parsed");
    const auto deepOcean = parseCommand("/locate biome deep_ocean");
    require(deepOcean.command && deepOcean.command->biome == Biome::DEEP_OCEAN,
            "underscore biome locate command was not parsed");
    for (int i = 0; i < BIOME_COUNT; ++i) {
        const Biome biome = static_cast<Biome>(i);
        const auto parsed = parseCommand(
            "/locate biome " + std::string(biomeCommandName(biome)));
        require(parsed.command && parsed.command->biome == biome,
                "a registered biome name was not accepted by locate");
    }
    const auto locateError = parseCommand("/locate biome volcano");
    require(locateError.error && locateError.error->position == 14 &&
            locateError.error->expected == "a valid biome (see /help)",
            "locate error did not identify the unsupported biome argument");
    const auto timeError = parseCommand("/time noon");
    require(timeError.error && timeError.error->position == 6 &&
            timeError.error->expected == "set",
            "time syntax error did not preserve its source position");
    const auto unknown = parseCommand("/unknown");
    require(unknown.error && unknown.error->kind == CommandErrorKind::UnknownCommand &&
            unknown.error->position == 1,
            "unknown command did not preserve its source position");

    const auto nearest = locateNearestBiome(glm::ivec2(0, 0), Biome::PLAINS,
        [](int x, int z) { return x >= 48 && z >= -16 && z <= 16
            ? Biome::PLAINS : Biome::FOREST; }, 128, 32);
    require(nearest && nearest->x == 48 && nearest->y == 0,
            "biome locator did not refine the nearest sampled biome");

    require(parseGamemodeCommand("/gamemode 0") == GameMode::Survival,
            "gamemode 0 did not select Survival");
    require(parseGamemodeCommand("/gamemode 1") == GameMode::Creative,
            "gamemode 1 did not select Creative");
    require(parseGamemodeCommand("/gamemode 3") == GameMode::Spectator,
            "gamemode 3 did not select Spectator");
    require(!parseGamemodeCommand("/gamemode 2"),
            "unsupported game mode was accepted");
    require(!parseGamemodeCommand("/gamemode 1 extra"),
            "trailing command input was accepted");
    require(!parseGamemodeCommand("/give 1"),
            "unknown command was accepted");
    require(parseTimeSetCommand("/time set day") == TimePreset::Day,
            "time set day was not parsed");
    require(parseTimeSetCommand("/time set night") == TimePreset::Night,
            "time set night was not parsed");
    require(!parseTimeSetCommand("/time day") &&
            !parseTimeSetCommand("/time set noon") &&
            !parseTimeSetCommand("/time set day extra"),
            "invalid time command was accepted");
    require(parseWeatherCommand("/weather clear") == WeatherType::Clear &&
            parseWeatherCommand("/weather rain") == WeatherType::Rain &&
            parseWeatherCommand("/weather thunder") == WeatherType::Thunder,
            "weather command was not parsed");
    require(!parseWeatherCommand("/weather snow") &&
            !parseWeatherCommand("/weather rain 600") &&
            !parseWeatherCommand("/weather"),
            "invalid weather command was accepted");
    const auto teleport = parseTeleportCommand("/tp -12.5 80 44.25");
    require(teleport && teleport->x == -12.5 && teleport->y == 80.0 &&
            teleport->z == 44.25,
            "teleport coordinates were not parsed");
    const auto border = parseTeleportCommand("/tp -3000000000 319 3000000000");
    require(border && border->x == -3000000000.0 &&
            border->z == 3000000000.0,
            "teleport horizontal limit was not accepted precisely");
    require(!parseTeleportCommand("/tp 1 2"),
            "incomplete teleport was accepted");
    require(!parseTeleportCommand("/tp 1 2 3 extra"),
            "teleport with trailing input was accepted");
    require(!parseTeleportCommand("/tp inf 2 3"),
            "non-finite teleport coordinate was accepted");
    require(!parseTeleportCommand("/tp 3000000001 2 3"),
            "out-of-range teleport coordinate was accepted");
    std::cout << "Command parsing tests passed\n";
}
