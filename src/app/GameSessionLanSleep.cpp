#include "app/GameSession.h"
#include <algorithm>
#include <cmath>

bool GameSession::beginLanSleep(uint64_t id, glm::ivec3 bed) {
    const auto found = guests.find(id); if (found == guests.end()) return false;
    auto& guest = *found->second;
    auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
    const auto foot = runtime.world.validBedFoot(bed);
    if (!foot || guest.dead || guest.loading || guest.player.isSpectator()) return false;
    if (guest.profile.dimension == DimensionId::Overworld) guest.profile.bedSpawn = *foot;
    if (guest.sleep || !runtime.daylight.isNight() || runtime.entities.hasHostileNear(glm::vec3(*foot), 8)) return false;
    if (dimension == guest.profile.dimension && isSleeping() && sleepBed == *foot) return false;
    for (const auto& other : guests) if (other.first != id && other.second->profile.dimension == guest.profile.dimension &&
        other.second->sleep && other.second->bed == *foot) return false;
    closeAuthorityWindow(guest.player, runtime, guest.window, guest.windowView);
    guest.fishing.cancel(); guest.player.cancelBowCharge(); guest.wantsMorning = false;
    BedPart part = BedPart::Foot; BedDirection direction = BedDirection::North;
    decodeBed(runtime.world.getBlock(foot->x, foot->y, foot->z), part, direction);
    guest.bed = *foot; guest.sleepFacing = glm::vec3(bedDirectionOffset(direction));
    const double height = blockCollisionHeight(runtime.world.getBlock(foot->x, foot->y, foot->z)) + .01;
    guest.player.setPosition(glm::dvec3(*foot) + glm::dvec3(.5, height, .5));
    guest.sleep = 1; guest.sleepProgress = 0; guest.player.setSleepingVisual(true, 0); guest.lastStateSent = -1;
    return true;
}
void GameSession::finishLanSleep(LanPlayerRuntime& guest) {
    if (!guest.sleep) return;
    guest.sleep = 3; guest.sleepProgress = 0; guest.wantsMorning = false;
    guest.player.setSleepingVisual(true, 1); guest.player.cancelBowCharge(); guest.lastStateSent = -1;
}
void GameSession::updateLanSleep(float dt) {
    for (auto& entry : guests) {
        auto& guest = *entry.second;
        if (guest.sleep == 1) {
            guest.sleepProgress = std::min(1.0f, guest.sleepProgress + dt / .6f);
            guest.player.setSleepingVisual(true, guest.sleepProgress);
            if (guest.sleepProgress >= 1) guest.sleep = 2;
        } else if (guest.sleep == 3) {
            guest.sleepProgress = std::min(1.0f, guest.sleepProgress + dt / .35f);
            guest.player.setSleepingVisual(true, 1 - guest.sleepProgress);
            if (guest.sleepProgress >= 1) { guest.sleep = 0; guest.player.setSleepingVisual(false, 0); }
        }
        auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
        if ((guest.sleep == 1 || guest.sleep == 2) && (!runtime.world.validBedFoot(guest.bed) || guest.dead)) finishLanSleep(guest);
    }
    for (size_t index = 0; index < simulations.size(); ++index) if (simulations[index]) (void)trySkipLanNight(static_cast<DimensionId>(index));
}
bool GameSession::trySkipLanNight(DimensionId target) {
    if (!hostingLan()) return false;
    size_t eligible = 0, asleep = 0; bool requested = false;
    if (dimension == target && !playerDead && !player.isSpectator()) {
        ++eligible;
        if (sleepState == SleepVisualState::Entering || sleepState == SleepVisualState::Choosing) { ++asleep; requested = hostWantsMorning; }
    }
    for (const auto& entry : guests) {
        const auto& guest = *entry.second;
        if (guest.profile.dimension != target || guest.dead || guest.player.isSpectator()) continue;
        ++eligible;
        if (guest.sleep == 1 || guest.sleep == 2) { ++asleep; requested = requested || guest.wantsMorning; }
    }
    const auto percent = worldMetadata.gameRules.integer(GameRuleId::PlayersSleepingPercentage);
    const size_t required = static_cast<size_t>(std::max<int64_t>(1, (static_cast<int64_t>(eligible) * percent + 99) / 100));
    if (!requested || asleep < required) return false;
    auto& runtime = *simulations[static_cast<size_t>(target)];
    if (worldMetadata.gameRules.boolean(GameRuleId::AdvanceTime)) runtime.daylight.resetMorning();
    if (target == DimensionId::Overworld && worldMetadata.gameRules.boolean(GameRuleId::AdvanceWeather)) runtime.weather.setWeather(WeatherType::Clear);
    if (dimension == target) finishSleep({});
    for (auto& entry : guests) if (entry.second->profile.dimension == target) finishLanSleep(*entry.second);
    return true;
}
void GameSession::travelLanPlayer(LanPlayerRuntime& guest, DimensionId target, bool respawn) {
    auto& old = *simulations[static_cast<size_t>(guest.profile.dimension)];
    closeAuthorityWindow(guest.player, old, guest.window, guest.windowView);
    auto position = guest.player.getPosition();
    if (guest.profile.dimension == DimensionId::Heaven && position.y < Config::WORLD_MIN_Y) position = old.world.findSafeSpawn();
    guest.profile.positions[static_cast<size_t>(guest.profile.dimension)] = position;
    guest.profile.positioned[static_cast<size_t>(guest.profile.dimension)] = true;
    auto& runtime = ensureDimension(target);
    guest.profile.dimension = target;
    if (!guest.profile.positioned[static_cast<size_t>(target)]) {
        guest.profile.positions[static_cast<size_t>(target)] = target == DimensionId::Heaven ? runtime.world.findSafeSpawn() :
            glm::dvec3(worldMetadata.worldSpawn) + glm::dvec3(.5, 1.01, .5);
        guest.profile.positioned[static_cast<size_t>(target)] = true;
    }
    guest.player.bindWorld(runtime.world, &runtime.entities);
    guest.player.setPosition(guest.profile.positions[static_cast<size_t>(target)]);
    if (respawn) {
        const auto spawn = guest.profile.bedSpawn.value_or(worldMetadata.worldSpawn);
        guest.player.setPosition(glm::dvec3(spawn) + glm::dvec3(.5, 1.01, .5));
        guest.player.survivalStats().resetAfterRespawn(); guest.player.resetDamageImmunity(); guest.player.extinguish();
        guest.dead = false; guest.pendingRespawn = true;
    }
    guest.sleep = 0; guest.wantsMorning = false; guest.player.setSleepingVisual(false, 0);
    guest.fishing.cancel(); guest.input.clearVirtual(); guest.buttons = guest.appliedButtons = 0;
    guest.loading = true; ++guest.chunkEpoch; guest.sentChunkRevisions.clear(); guest.lodSubscriptions.clear(); guest.lastStateSent = -1;
    updateLanInterests();
}
