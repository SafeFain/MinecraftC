#include "game/Command.h"

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
    const auto teleport = parseTeleportCommand("/tp -12.5 80 44.25");
    require(teleport && teleport->x == -12.5f && teleport->y == 80.0f &&
            teleport->z == 44.25f,
            "teleport coordinates were not parsed");
    require(!parseTeleportCommand("/tp 1 2"),
            "incomplete teleport was accepted");
    require(!parseTeleportCommand("/tp 1 2 3 extra"),
            "teleport with trailing input was accepted");
    require(!parseTeleportCommand("/tp inf 2 3"),
            "non-finite teleport coordinate was accepted");
    require(!parseTeleportCommand("/tp 30000001 2 3"),
            "out-of-range teleport coordinate was accepted");
    std::cout << "Command parsing tests passed\n";
}
