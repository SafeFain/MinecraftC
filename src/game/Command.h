#pragma once

#include <cerrno>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "game/GameRules.h"
#include "game/Weather.h"
#include "world/Biome.h"
#include "world/Structure.h"

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
    LocateBiome,
    LocateStructure
};

struct ParsedCommand {
    CommandType type = CommandType::Help;
    GameMode gameMode = GameMode::Survival;
    TeleportTarget teleport;
    TimePreset time = TimePreset::Day;
    WeatherType weather = WeatherType::Clear;
    Biome biome = Biome::PLAINS;
    StructureType structure = StructureType::Village;
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
        if (tokens.size() < 2 ||
            (tokens[1].text != "biome" && tokens[1].text != "structure"))
            return expected(input, tokens, 1, "biome|structure");
        if (tokens.size() < 3) return expected(
            input, tokens, 2,
            tokens[1].text == "biome" ? "<biome>" : "<structure>");
        if (tokens[1].text == "biome") {
            const auto biome = parseBiomeCommandName(tokens[2].text);
            if (!biome)
                return expected(input, tokens, 2,
                                "a valid biome (see /help)");
            parsed.type = CommandType::LocateBiome;
            parsed.biome = *biome;
        } else {
            const auto structure = parseStructureCommandName(tokens[2].text);
            if (!structure)
                return expected(input, tokens, 2,
                                "a valid structure (see /help)");
            parsed.type = CommandType::LocateStructure;
            parsed.structure = *structure;
        }
        if (tokens.size() > 3) return expected(input, tokens, 3, "<end>");
    } else {
        return {{}, CommandError{tokens[0].position + 1,
                                 CommandErrorKind::UnknownCommand, {}}};
    }
    return {parsed, {}};
}

struct CommandSuggestion {
    size_t start = 0;
    size_t end = 0;
    std::string text;
};

// Brigadier-style literal/argument suggestions for the complete command tree.
// The replacement range covers the token under the cursor, allowing Tab to
// work in the middle of a command while preserving later arguments.
inline std::vector<CommandSuggestion> commandSuggestions(
    const std::string& input, size_t cursor) {
    using namespace command_detail;
    cursor = std::min(cursor, input.size());
    size_t start = cursor;
    while (start > 0 && input[start - 1] != ' ' && input[start - 1] != '\t')
        --start;
    size_t end = cursor;
    while (end < input.size() && input[end] != ' ' && input[end] != '\t')
        ++end;

    const auto before = tokenize(input.substr(0, start));
    const size_t argument = before.size();
    const std::string_view prefix(input.data() + start, cursor - start);
    std::vector<std::string_view> candidates;
    auto add = [&](std::string_view candidate) {
        if (candidate.substr(0, prefix.size()) == prefix)
            candidates.push_back(candidate);
    };

    if (argument == 0) {
        constexpr std::array<std::string_view, 6> commands{
            "/gamemode", "/help", "/locate", "/time", "/tp", "/weather"};
        for (const auto command : commands) add(command);
    } else {
        const std::string& command = before[0].text;
        if (argument == 1 && command == "/gamemode") {
            add("0"); add("1"); add("3");
        } else if (argument == 1 && command == "/time") {
            add("set");
        } else if (argument == 2 && command == "/time" &&
                   before[1].text == "set") {
            add("day"); add("night");
        } else if (argument == 1 && command == "/weather") {
            add("clear"); add("rain"); add("thunder");
        } else if (argument == 1 && command == "/locate") {
            add("biome"); add("structure");
        } else if (argument == 2 && command == "/locate" &&
                   before[1].text == "biome") {
            for (int raw = 0; raw < BIOME_COUNT; ++raw)
                add(biomeCommandName(static_cast<Biome>(raw)));
        } else if (argument == 2 && command == "/locate" &&
                   before[1].text == "structure") {
            for (const StructureType type : STRUCTURE_TYPES)
                add(structureCommandName(type));
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    std::vector<CommandSuggestion> result;
    result.reserve(candidates.size());
    for (const auto candidate : candidates)
        result.push_back({start, end, std::string(candidate)});
    return result;
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
