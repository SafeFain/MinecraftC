#include "renderer/RenderEnvironment.h"

#include <cmath>
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
    DayNightCycle cycle;
    require(DayNightCycle::isDayPhase(0.0f), "sunrise was not day");
    require(DayNightCycle::isDayPhase(0.25f), "noon was not day");
    require(DayNightCycle::isDayPhase(0.5f - 0.000001f),
            "the phase immediately before sunset was not day");
    require(!DayNightCycle::isDayPhase(0.5f),
            "sunset did not enter night immediately");
    require(!DayNightCycle::isDayPhase(0.999999f),
            "the phase immediately before sunrise was not night");
    cycle.setDay();
    require(cycle.phase() == 0.0f && cycle.isDay(),
            "set day did not select sunrise");
    cycle.setNight();
    require(cycle.phase() == 0.5f && cycle.isNight(),
            "set night did not select sunset");
    cycle.update(0.1f, 0, true);
    require(cycle.phase() == 0.5f,
            "a manually selected time was lost in static-cycle mode");
    cycle.resetMorning();
    const float morning = cycle.phase();
    require(!cycle.isNight(), "morning was classified as night");

    cycle.update(1.0f, 20, false);
    require(cycle.phase() == morning, "paused cycle advanced");

    cycle.update(0.1f, 20, true);
    require(cycle.phase() > morning, "active cycle did not advance");

    DayNightCycle nightCycle;
    for (int i = 0; i < 900; ++i) nightCycle.update(0.1f, 1, true);
    require(nightCycle.isNight(), "night phase was not recognized");

    cycle.update(0.1f, 0, false);
    require(std::abs(cycle.phase() - DayNightCycle::STATIC_DAY_PHASE) < 0.0001f,
            "static day did not select noon");

    const RenderEnvironment noon = cycle.evaluate();
    require(noon.daylight > 0.95f, "static noon is not daylight");
    require(noon.ambientIntensity >= Config::NIGHT_AMBIENT_MIN &&
            noon.ambientIntensity <= 1.0f,
            "noon ambient is outside expected range");
    const RenderEnvironment rain = applyWeather(noon, 1.0f, 0.0f, 0.0f);
    const RenderEnvironment thunder = applyWeather(noon, 1.0f, 1.0f, 0.0f);
    const RenderEnvironment flash = applyWeather(noon, 1.0f, 1.0f, 1.0f);
    require(rain.directIntensity < noon.directIntensity &&
            thunder.directIntensity < rain.directIntensity,
            "weather did not progressively darken direct light");
    require(rain.starIntensity == 0.0f,
            "overcast weather did not hide stars");
    require(flash.ambientIntensity > thunder.ambientIntensity,
            "lightning flash did not brighten the environment");

    // Switching back to an automatic cycle resumes from noon. Advance half a
    // cycle in bounded frame-sized steps to reach midnight.
    for (int i = 0; i < 6000; ++i) cycle.update(0.1f, 20, true);
    const RenderEnvironment midnight = cycle.evaluate();
    require(midnight.daylight < 0.05f, "half cycle did not reach night");
    require(midnight.starIntensity > 0.9f, "night sky has no stars");
    require(midnight.ambientIntensity >= Config::NIGHT_AMBIENT_MIN,
            "night ambient fell below playable minimum");

    // One complete 10-minute cycle must wrap back to its starting phase.
    cycle.update(0.1f, 0, false);
    const float start = cycle.phase();
    for (int i = 0; i < 6000; ++i) cycle.update(0.1f, 10, true);
    require(std::abs(cycle.phase() - start) < 0.001f,
            "day cycle did not wrap deterministically");

    std::cout << "render environment logic passed\n";
}
