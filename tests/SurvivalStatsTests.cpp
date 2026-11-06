#include "game/Experience.h"
#include <limits>
#include <utility>
#include <cmath>
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
    Experience xp;
    const std::pair<int,int64_t> boundaries[]={{0,0},{1,7},{2,16},{15,315},{16,352},{17,394},{30,1395},{31,1507},{32,1628},{40,2920}};
    for(const auto& expected:boundaries) {
        xp.clear();xp.addPoints(static_cast<int32_t>(expected.second));
        require(xp.level()==expected.first && xp.points()==0,"Java experience thresholds");
        xp.addPoints(-1);
        require(expected.first==0 ? xp.level()==0 : xp.level()==expected.first-1,"threshold minus one");
    }
    xp.clear();for(int i=0;i<2920;++i)xp.addPoints(1);
    require(xp.level()==40&&xp.points()==0,"incremental experience matches bulk rewards");
    xp.addPoints(-2920);require(xp.level()==0&&xp.progress()==0,"point subtraction returns to zero");
    xp.setLevel(16);require(xp.setPoints(21),"set in-level points");xp.addLevels(4);
    require(xp.level()==20&&std::abs(xp.progress()-.5)<1e-9&&xp.points()==31,"level changes preserve bar fraction");
    require(!xp.setPoints(62)&&xp.level()==20&&xp.points()==31,"set points cannot cross a level");
    require(xp.deathDrop()==100,"death experience capped at 100");xp.setLevel(3);require(xp.deathDrop()==21,"death experience is seven per level");
    xp.addLevels(INT32_MIN);require(xp.level()==0&&xp.progress()==0,"negative levels clamp without residual progress");
    xp.addLevels(INT32_MAX);xp.addPoints(INT32_MAX);require(xp.level()==Experience::MAX_LEVEL&&xp.progress()==0,"experience overflow saturates");
    for(double invalid:{-1.,1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        require(!Experience::valid(1,invalid),"invalid saved progress rejected");

    std::cout << "Survival stats tests passed\n";
    return 0;
}
