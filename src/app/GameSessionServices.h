#pragma once

#include "app/DimensionSimulation.h"
#include "app/LanPlayerRuntime.h"
#include "game/FishingSystem.h"
#include "threading/ThreadPool.h"
#include "network/Session.h"
#include <functional>
#include <memory>

// Composition dependencies. Factories transfer ownership to the session; an
// external worker pool and transports must outlive it. Empty services preserve the normal
// session-owned workers and standard dimension/remote-player implementations.
struct GameSessionServices {
    ThreadPool* workers = nullptr;
    Lan::Host* host = nullptr;
    Lan::Client* client = nullptr;
    std::function<std::unique_ptr<DimensionSimulation>(DimensionId)> dimension;
    std::function<std::unique_ptr<LanPlayerRuntime>(World&,EntityManager&,Lan::PlayerProfile)> guest;
    FishingSystem::Random fishingRandom;
};
