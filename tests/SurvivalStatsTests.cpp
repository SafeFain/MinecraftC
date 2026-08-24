#include "game/SurvivalStats.h"

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
    SurvivalStats stats;
    stats.set(20.0f, 20, 1.0f, 0.0f);
    stats.addExhaustion(4.01f);
    stats.tick(Difficulty::Normal, 1);
    require(stats.hunger() == 20 && stats.saturation() == 0.0f,
            "exhaustion consumes saturation first");
    stats.addExhaustion(4.0f);
    stats.tick(Difficulty::Normal, 1);
    require(stats.hunger() == 19, "exhaustion consumes hunger after saturation");

    stats.set(10.0f, 10, 0.0f, 0.0f);
    require(stats.eat(ItemId::STEAK), "food can be eaten below full hunger");
    require(stats.hunger() == 18 && stats.saturation() == 12.8f,
            "steak restores hunger and saturation");
    require(!stats.eat(ItemId::COAL), "non-food cannot be eaten");

    stats.set(5.0f, 20, 5.0f, 0.0f);
    stats.tick(Difficulty::Normal, 10);
    require(stats.health() > 5.83f && stats.health() < 5.84f,
            "saturated natural regeneration is fractional and fast");
    require(stats.foodTickTimer() == 0, "fast regeneration resets food timer");

    stats.set(20.0f, 0, 0.0f, 0.0f);
    stats.tick(Difficulty::Easy, 2000);
    require(stats.health() == 10.0f, "easy starvation stops at ten health");
    stats.set(20.0f, 0, 0.0f, 0.0f);
    stats.tick(Difficulty::Normal, 2000);
    require(stats.health() == 1.0f, "normal starvation stops at one health");
    stats.set(20.0f, 0, 0.0f, 0.0f);
    stats.tick(Difficulty::Hard, 2000);
    require(stats.dead(), "hard starvation can kill");

    stats.resetAfterRespawn();
    require(stats.health() == 20.0f && stats.hunger() == 20,
            "respawn restores core survival state");
    stats.set(10.0f, 10, 0.0f, 0.0f);
    stats.tick(Difficulty::Peaceful, 9);
    require(stats.hunger() == 10 && stats.health() == 10.0f,
            "peaceful recovery respects tick intervals");
    stats.tick(Difficulty::Peaceful, 11);
    require(stats.hunger() == 12 && stats.health() == 11.0f,
            "peaceful recovery advances on persistent counters");

    SurvivalStats fps30;
    SurvivalStats fps60;
    SurvivalStats fps120;
    fps30.set(8.0f, 20, 12.0f, 0.0f);
    fps60.set(8.0f, 20, 12.0f, 0.0f);
    fps120.set(8.0f, 20, 12.0f, 0.0f);
    for (int frame = 0; frame < 30; ++frame) fps30.tick(Difficulty::Normal, 4);
    for (int frame = 0; frame < 60; ++frame) fps60.tick(Difficulty::Normal, 2);
    for (int frame = 0; frame < 120; ++frame) fps120.tick(Difficulty::Normal, 1);
    require(fps30.health() == fps60.health() &&
            fps60.health() == fps120.health() &&
            fps30.hunger() == fps120.hunger() &&
            fps30.saturation() == fps120.saturation(),
            "survival outcomes depend on game ticks rather than frame rate");
    std::cout << "Survival stats tests passed\n";
    return 0;
}
