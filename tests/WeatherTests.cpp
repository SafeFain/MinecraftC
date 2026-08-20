#include "game/Weather.h"
#include "world/BiomeMap.h"

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
    WeatherSystem weather;
    weather.reset(1234, {false, false, 1, 1, 0});
    weather.tick();
    require(weather.raining(), "rain timer did not toggle precipitation");
    require(weather.saveState().rainTicks >= WeatherSystem::RAIN_MIN_TICKS &&
            weather.saveState().rainTicks <= WeatherSystem::RAIN_MAX_TICKS,
            "rain duration was outside the vanilla range");
    require(weather.saveState().thunderTicks >= WeatherSystem::THUNDER_MIN_TICKS &&
            weather.saveState().thunderTicks <= WeatherSystem::THUNDER_MAX_TICKS,
            "thunder duration was outside the vanilla range");

    weather.setWeather(WeatherType::Clear);
    require(weather.type() == WeatherType::Clear,
            "clear command did not clear both weather flags");
    weather.setWeather(WeatherType::Rain);
    require(weather.type() == WeatherType::Rain && !weather.thundering(),
            "rain command unexpectedly enabled thunder");
    for (int i = 0; i < 100; ++i) weather.tick();
    require(weather.rainGradient() > 0.99f,
            "rain gradient did not reach full strength in 100 ticks");
    weather.setWeather(WeatherType::Thunder);
    require(weather.type() == WeatherType::Thunder,
            "thunder command did not enable rain and thunder");

    WeatherSystem replay;
    replay.reset(9999);
    WeatherSystem replayCopy;
    replayCopy.reset(9999);
    require(replay.saveState().rainTicks == replayCopy.saveState().rainTicks &&
            replay.saveState().thunderTicks == replayCopy.saveState().thunderTicks,
            "weather initialization was not deterministic for a world seed");

    require(precipitationFor(Biome::DESERT, 80) == PrecipitationType::None &&
            precipitationFor(Biome::SAVANNA, 80) == PrecipitationType::None &&
            precipitationFor(Biome::BADLANDS, 80) == PrecipitationType::None,
            "dry biomes accepted precipitation");
    require(precipitationFor(Biome::VOLCANIC_HIGHLANDS, 180) ==
                PrecipitationType::None &&
            precipitationFor(Biome::RED_CANYON, 90) == PrecipitationType::None,
            "v7 dry terrain biomes accepted precipitation");
    require(precipitationFor(Biome::SNOW_TUNDRA, -20) ==
                PrecipitationType::Snow,
            "snow tundra did not snow at every altitude");
    require(precipitationFor(Biome::GLACIAL_PEAKS, 70) ==
                PrecipitationType::Snow &&
            precipitationFor(Biome::ALPINE_TUNDRA, 70) ==
                PrecipitationType::Snow,
            "v7 frozen terrain biomes did not produce snow");
    require(precipitationFor(Biome::TAIGA, 127) == PrecipitationType::Rain &&
            precipitationFor(Biome::TAIGA, 128) == PrecipitationType::Snow,
            "taiga snow-line boundary was incorrect");
    require(precipitationFor(Biome::PLAINS, 90) == PrecipitationType::Rain,
            "temperate biome did not receive rain");

    std::cout << "Weather logic tests passed\n";
}
