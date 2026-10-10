#pragma once

#include "entity/EntityManager.h"
#include "game/SaveStore.h"
#include "game/Weather.h"
#include "world/World.h"
#include "renderer/RenderEnvironment.h"
#include <memory>

// Each dimension retains its terrain, entity IDs, weather and tick clocks while
// players travel. Store declaration order keeps streaming I/O dependencies alive.
struct DimensionSimulation {
    std::unique_ptr<SaveStore> store;
    World world;
    EntityManager entities{world};
    DayNightCycle daylight;
    WeatherSystem weather;
    uint64_t ticks = 0;
    float tickRemainder = 0;
    bool initialized = false;
};
