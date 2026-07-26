#pragma once

#include <cmath>
#include <optional>
#include <sstream>
#include <string>

#include "game/GameRules.h"

inline std::optional<GameMode> parseGamemodeCommand(const std::string& input) {
    std::istringstream stream(input);
    std::string command;
    std::string trailing;
    int mode = -1;
    if (!(stream >> command >> mode) || command != "/gamemode" ||
        (stream >> trailing)) {
        return std::nullopt;
    }
    switch (mode) {
        case 0: return GameMode::Survival;
        case 1: return GameMode::Creative;
        case 3: return GameMode::Spectator;
        default: return std::nullopt;
    }
}

enum class TimePreset {
    Day,
    Night
};

inline std::optional<TimePreset> parseTimeSetCommand(const std::string& input) {
    std::istringstream stream(input);
    std::string command;
    std::string operation;
    std::string value;
    std::string trailing;
    if (!(stream >> command >> operation >> value) || command != "/time" ||
        operation != "set" || (stream >> trailing)) {
        return std::nullopt;
    }
    if (value == "day") return TimePreset::Day;
    if (value == "night") return TimePreset::Night;
    return std::nullopt;
}

struct TeleportTarget {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline std::optional<TeleportTarget> parseTeleportCommand(
    const std::string& input) {
    std::istringstream stream(input);
    std::string command;
    std::string trailing;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!(stream >> command >> x >> y >> z) ||
        command != "/tp" || (stream >> trailing)) {
        return std::nullopt;
    }
    constexpr double horizontalLimit = 3000000000.0;
    constexpr double verticalLimit = 2048.0;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::abs(x) > horizontalLimit || std::abs(z) > horizontalLimit ||
        std::abs(y) > verticalLimit) {
        return std::nullopt;
    }
    return TeleportTarget{x, y, z};
}
