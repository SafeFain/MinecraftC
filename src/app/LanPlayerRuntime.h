#pragma once

#include "network/PlayerProfile.h"
#include <algorithm>
#include "network/ChunkProtocol.h"
#include "network/LodProtocol.h"
#include "player/Player.h"
#include "game/InventoryCommands.h"
#include "game/FishingSystem.h"

// A remote player's inventory, transient actions and dimension positions do not
// alias the host player or another guest. World objects outlive these runtimes.
struct LanPlayerRuntime {
    LanPlayerRuntime(World& world, EntityManager& entities, Lan::PlayerProfile saved)
        : profile(std::move(saved)), player(world) {
        if (!profile.cursor.empty() || std::any_of(profile.crafting.begin(), profile.crafting.end(), [](ItemStack item) { return !item.empty(); }))
            window.restoreHeld(profile.cursor, profile.crafting);
        player.setEntityManager(&entities);
        player.configureRules(profile.mode, Difficulty::Normal);
        player.inventory() = profile.inventory;
        player.survivalStats().set(profile.health, profile.hunger, profile.saturation,
                                  profile.exhaustion, profile.foodTickTimer);
        player.setPosition(profile.positions[static_cast<size_t>(profile.dimension)]);
    }
    Lan::PlayerProfile profile;
    Player player;
    InputState input;
    InventoryTransaction window;
    InventoryWindowView windowView;
    uint64_t actionAcknowledged = 0;
    FishingSystem fishing;
    uint8_t sleep = 0;
    glm::ivec3 bed{0};
    glm::vec3 sleepFacing{0, 0, -1};
    float sleepProgress = 0;
    bool wantsMorning = false, pendingRespawn = false;
    bool dead = false;
    bool loading = true;
    uint8_t buttons = 0;
    uint8_t appliedButtons = 0;
    int fishingSlot = -1;
    uint16_t fishingRodDamage = 0;
    double lastInput = 0, chatLast = 0;
    float chatTokens = 5;
    double lastStateSent = -1, lastEntitiesSent = -1;
    uint64_t inputSequence = 0;
    uint64_t appliedInputSequence = 0;
    uint64_t chunkEpoch = 1;
    std::map<std::pair<int, int>, uint64_t> sentChunkRevisions;
    struct LodSubscription { uint64_t revision = 1, generation = 0; bool dirty = true, queued = false, notified = false; };
    uint64_t nextLodSubscription = 1;
    std::unordered_map<LodTileKey, LodSubscription, LodTileKeyHash> lodSubscriptions;
    int radius = 2, lodRadius = 0;
};
