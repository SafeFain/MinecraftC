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
    stats.addExhaustion(4.0f);
    require(stats.hunger() == 20 && stats.saturation() == 0.0f,
            "exhaustion consumes saturation first");
    stats.addExhaustion(4.0f);
    require(stats.hunger() == 19, "exhaustion consumes hunger after saturation");

    stats.set(10.0f, 10, 0.0f, 0.0f);
    require(stats.eat(ItemId::STEAK), "food can be eaten below full hunger");
    require(stats.hunger() == 18 && stats.saturation() == 12.8f,
            "steak restores hunger and saturation");
    require(!stats.eat(ItemId::COAL), "non-food cannot be eaten");

    stats.set(5.0f, 20, 5.0f, 0.0f);
    stats.tick(Difficulty::Normal, 10);
    require(stats.health() == 6.0f, "saturated natural regeneration is fast");

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
    std::cout << "Survival stats tests passed\n";
    return 0;
}
