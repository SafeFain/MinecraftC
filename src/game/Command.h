#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "game/GameRules.h"
#include "game/Weather.h"
#include "world/Biome.h"

enum class TimePreset { Day, Night };

struct TeleportTarget {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class CommandType {
    Help,
    Gamemode,
    Teleport,
    Time,
    Weather,
    LocateBiome
};

struct ParsedCommand {
    CommandType type = CommandType::Help;
    GameMode gameMode = GameMode::Survival;
    TeleportTarget teleport;
    TimePreset time = TimePreset::Day;
    WeatherType weather = WeatherType::Clear;
    Biome biome = Biome::PLAINS;
};

enum class CommandErrorKind { UnknownCommand, Expected };

struct CommandError {
    size_t position = 0;
    CommandErrorKind kind = CommandErrorKind::Expected;
    std::string expected;
};

struct CommandParseResult {
    std::optional<ParsedCommand> command;
    std::optional<CommandError> error;
};

namespace command_detail {
struct Token {
    std::string text;
    size_t position = 0;
};

inline std::vector<Token> tokenize(const std::string& input) {
    std::vector<Token> result;
    size_t cursor = 0;
    while (cursor < input.size()) {
        while (cursor < input.size() &&
               (input[cursor] == ' ' || input[cursor] == '\t')) ++cursor;
        if (cursor == input.size()) break;
        const size_t start = cursor;
        while (cursor < input.size() && input[cursor] != ' ' &&
               input[cursor] != '\t') ++cursor;
        result.push_back({input.substr(start, cursor - start), start});
    }
    return result;
}

inline CommandParseResult expected(const std::string& input,
                                   const std::vector<Token>& tokens,
                                   size_t index, std::string value) {
    const size_t position = index < tokens.size() ? tokens[index].position : input.size();
    return {{}, CommandError{position, CommandErrorKind::Expected, std::move(value)}};
}

inline bool parseNumber(const Token& token, double& value) {
    errno = 0;
    char* end = nullptr;
    value = std::strtod(token.text.c_str(), &end);
    return errno != ERANGE && end == token.text.c_str() + token.text.size() &&
           std::isfinite(value);
}
}

inline CommandParseResult parseCommand(const std::string& input) {
    using namespace command_detail;
    const auto tokens = tokenize(input);
    if (tokens.empty() || tokens[0].text.empty() || tokens[0].text[0] != '/')
        return expected(input, tokens, 0, "/<command>");

    const std::string& name = tokens[0].text;
    ParsedCommand parsed;
    if (name == "/help") {
        if (tokens.size() != 1) return expected(input, tokens, 1, "<end>");
        parsed.type = CommandType::Help;
    } else if (name == "/gamemode") {
        if (tokens.size() < 2) return expected(input, tokens, 1, "0|1|3");
        if (tokens[1].text == "0") parsed.gameMode = GameMode::Survival;
        else if (tokens[1].text == "1") parsed.gameMode = GameMode::Creative;
        else if (tokens[1].text == "3") parsed.gameMode = GameMode::Spectator;
        else return expected(input, tokens, 1, "0|1|3");
        if (tokens.size() > 2) return expected(input, tokens, 2, "<end>");
        parsed.type = CommandType::Gamemode;
    } else if (name == "/tp") {
        if (tokens.size() < 4) return expected(input, tokens, tokens.size(), "<x> <y> <z>");
        double coordinates[3]{};
        for (size_t i = 0; i < 3; ++i)
            if (!parseNumber(tokens[i + 1], coordinates[i]))
                return expected(input, tokens, i + 1, "<number>");
        constexpr double horizontalLimit = 3000000000.0;
        constexpr double verticalLimit = 2048.0;
        if (std::abs(coordinates[0]) > horizontalLimit)
            return expected(input, tokens, 1, "<valid x>");
        if (std::abs(coordinates[1]) > verticalLimit)
            return expected(input, tokens, 2, "<valid y>");
        if (std::abs(coordinates[2]) > horizontalLimit)
            return expected(input, tokens, 3, "<valid z>");
        if (tokens.size() > 4) return expected(input, tokens, 4, "<end>");
        parsed.type = CommandType::Teleport;
        parsed.teleport = {coordinates[0], coordinates[1], coordinates[2]};
    } else if (name == "/time") {
        if (tokens.size() < 2 || tokens[1].text != "set")
            return expected(input, tokens, 1, "set");
        if (tokens.size() < 3) return expected(input, tokens, 2, "day|night");
        if (tokens[2].text == "day") parsed.time = TimePreset::Day;
        else if (tokens[2].text == "night") parsed.time = TimePreset::Night;
        else return expected(input, tokens, 2, "day|night");
        if (tokens.size() > 3) return expected(input, tokens, 3, "<end>");
        parsed.type = CommandType::Time;
    } else if (name == "/weather") {
        if (tokens.size() < 2)
            return expected(input, tokens, 1, "clear|rain|thunder");
        if (tokens[1].text == "clear") parsed.weather = WeatherType::Clear;
        else if (tokens[1].text == "rain") parsed.weather = WeatherType::Rain;
        else if (tokens[1].text == "thunder") parsed.weather = WeatherType::Thunder;
        else return expected(input, tokens, 1, "clear|rain|thunder");
        if (tokens.size() > 2) return expected(input, tokens, 2, "<end>");
        parsed.type = CommandType::Weather;
    } else if (name == "/locate") {
        if (tokens.size() < 2 || tokens[1].text != "biome")
            return expected(input, tokens, 1, "biome");
        if (tokens.size() < 3)
            return expected(input, tokens, 2, "<biome>");
        const auto biome = parseBiomeCommandName(tokens[2].text);
        if (!biome) return expected(input, tokens, 2, "a valid biome (see /help)");
        if (tokens.size() > 3) return expected(input, tokens, 3, "<end>");
        parsed.type = CommandType::LocateBiome;
        parsed.biome = *biome;
    } else {
        return {{}, CommandError{tokens[0].position + 1,
                                 CommandErrorKind::UnknownCommand, {}}};
    }
    return {parsed, {}};
}

// Compatibility helpers retained for gameplay-independent callers.
inline std::optional<GameMode> parseGamemodeCommand(const std::string& input) {
    const auto result = parseCommand(input);
    if (!result.command || result.command->type != CommandType::Gamemode) return {};
    return result.command->gameMode;
}

inline std::optional<TimePreset> parseTimeSetCommand(const std::string& input) {
    const auto result = parseCommand(input);
    if (!result.command || result.command->type != CommandType::Time) return {};
    return result.command->time;
}

inline std::optional<WeatherType> parseWeatherCommand(const std::string& input) {
    const auto result = parseCommand(input);
    if (!result.command || result.command->type != CommandType::Weather) return {};
    return result.command->weather;
}

inline std::optional<TeleportTarget> parseTeleportCommand(const std::string& input) {
    const auto result = parseCommand(input);
    if (!result.command || result.command->type != CommandType::Teleport) return {};
    return result.command->teleport;
}
