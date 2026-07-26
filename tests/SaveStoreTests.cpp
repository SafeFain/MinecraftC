#include "game/SaveStore.h"
#include "Config.h"
#include "world/WorldGenContext.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    const auto root = std::filesystem::temp_directory_path() /
        ("minecraftc-save-test-" + std::to_string(
            static_cast<unsigned long long>(std::rand())));
    std::filesystem::remove_all(root);

    try {
        SaveStore store(root / "negative-coordinates");
        WorldMetadata source;
        source.displayName = "Survival Test";
        source.seed = 0xFEDCBA9876543210ULL;
        source.generationVersion = WorldGenContext::GENERATION_VERSION;
        source.gameMode = GameMode::Survival;
        source.difficulty = Difficulty::Hard;
        source.cheatsEnabled = true;
        source.worldTicks = 123456;
        source.weather = {true, true, 18000, 7200, 42};
        source.playerPosition = {-1000000.125, 63.0, 1000000.375};
        source.worldSpawn = {-3, 70, 9};
        source.bedSpawn = glm::ivec3{-33, 66, -18};
        source.health = 7.5f;
        source.hunger = 11;
        source.saturation = 2.5f;
        source.exhaustion = 3.25f;
        source.inventory.slot(0) = {ItemId::IRON_PICKAXE, 1, 42};
        source.inventory.slot(9) = {ItemId::COAL, 37, 0};
        source.inventory.armor()[1] = {ItemId::IRON_CHESTPLATE, 1, 12};
        source.inventory.offhand() = {ItemId::SHIELD, 1, 4};
        source.entities.push_back({
            5, {-20.0f, 64.0f, 8.0f}, {0.1f, 0.0f, 0.2f},
            12.0f, 34.0f, {}, 98765
        });

        store.saveMetadata(source);
        require(store.exists(), "metadata file is created");
        const auto loaded = store.loadMetadata();
        require(loaded.displayName == source.displayName, "world name round trips");
        require(loaded.seed == source.seed, "64-bit seed round trips");
        require(loaded.gameMode == GameMode::Survival &&
                loaded.difficulty == Difficulty::Hard,
                "game rules round trip");
        require(loaded.cheatsEnabled, "cheat option round trips");
        require(loaded.weather.raining && loaded.weather.thundering &&
                loaded.weather.rainTicks == 18000 &&
                loaded.weather.thunderTicks == 7200 &&
                loaded.weather.sequence == 42,
                "weather state round trips");
        require(loaded.playerPosition == source.playerPosition,
                "negative player position round trips");
        require(loaded.bedSpawn == source.bedSpawn, "bed spawn round trips");
        require(loaded.inventory.slot(0).id == ItemId::IRON_PICKAXE &&
                loaded.inventory.slot(0).damage == 42,
                "durable inventory item round trips");
        require(loaded.inventory.offhand().id == ItemId::SHIELD,
                "offhand round trips");
        require(loaded.entities.size() == 1 &&
                loaded.entities[0].position == source.entities[0].position,
                "persistent entities round trip");

        const std::vector<BlockOverride> overrides = {
            {0, BlockId::AIR},
            {static_cast<uint32_t>(15 + 15 * 16 +
                Config::worldYToStorageY(319) * 256), BlockId::DIAMOND_ORE},
            {513, BlockId::FARMLAND_7},
            {514, BlockId::ACACIA_SAPLING}
        };
        store.saveChunkOverrides(-2, -7, overrides);
        const auto loadedOverrides = store.loadChunkOverrides(-2, -7);
        require(loadedOverrides.size() == 4, "chunk overrides round trip");
        require(loadedOverrides[0].block == BlockId::AIR,
                "explicit AIR override is preserved");
        require(loadedOverrides[1].localIndex == overrides[1].localIndex,
                "highest local block index is valid");
        require(loadedOverrides[2].block == BlockId::FARMLAND_7 &&
                loadedOverrides[3].block == BlockId::ACACIA_SAPLING,
                "new farming block ids round trip in save format 5");
        require(store.loadChunkOverrides(4, 9).empty(),
                "unmodified chunks have no overrides");

        std::vector<uint8_t> generated(Config::CHUNK_VOLUME,
                                       static_cast<uint8_t>(BlockId::STONE));
        generated.front() = static_cast<uint8_t>(BlockId::BEDROCK);
        generated.back() = static_cast<uint8_t>(BlockId::AIR);
        store.saveGeneratedChunk(-2, -7, generated,
                                 WorldGenContext::GENERATION_VERSION);
        const auto loadedGenerated = store.loadGeneratedChunk(
            -2, -7, WorldGenContext::GENERATION_VERSION);
        require(loadedGenerated && *loadedGenerated == generated,
                "pregenerated chunk cache round trips");
        require(!store.loadGeneratedChunk(
                    -2, -7, WorldGenContext::GENERATION_VERSION + 1),
                "pregenerated chunk cache rejects a different generation version");
        {
            std::fstream file(root / "negative-coordinates" / "generated" /
                                  "g.-2.-7.bin",
                              std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(-1, std::ios::end);
            const char corrupt = '\x7f';
            file.write(&corrupt, 1);
        }
        require(!store.loadGeneratedChunk(
                    -2, -7, WorldGenContext::GENERATION_VERSION),
                "corrupt pregenerated chunk cache falls back to generation");

        PersistedBlockEntity chest;
        chest.localIndex = 513;
        chest.value.type = BlockEntityType::Chest;
        chest.value.chest[0] = {ItemId::DIAMOND, 3, 0};
        PersistedBlockEntity furnace;
        furnace.localIndex = 1027;
        furnace.value.type = BlockEntityType::Furnace;
        furnace.value.input = {ItemId::RAW_IRON, 4, 0};
        furnace.value.fuel = {ItemId::COAL, 2, 0};
        furnace.value.output = {ItemId::IRON_INGOT, 1, 0};
        furnace.value.burnRemaining = 900;
        furnace.value.burnTotal = 1600;
        furnace.value.cookProgress = 73;
        store.saveBlockEntities(-2, -7, {chest, furnace});
        const auto blockEntities = store.loadBlockEntities(-2, -7);
        require(blockEntities.size() == 2 &&
                blockEntities[0].value.chest[0].id == ItemId::DIAMOND,
                "negative-coordinate chest state round trips");
        require(blockEntities[1].value.cookProgress == 73 &&
                blockEntities[1].value.output.id == ItemId::IRON_INGOT,
                "furnace timers and slots round trip");

        WorldMetadata::PersistedEntity arrow;
        arrow.type = 9;
        arrow.position = {-1000000.25, 70.5, 1000000.75};
        arrow.velocity = {12.0f, -1.0f, 2.0f};
        arrow.health = 1.0f;
        arrow.ageSeconds = 4.5f;
        arrow.flags = 3;
        arrow.projectileDamage = 6.0f;
        store.saveChunkEntities(-62501, 62500, {arrow});
        const auto chunkEntities = store.loadChunkEntities(-62501, 62500);
        require(chunkEntities.size() == 1 &&
                chunkEntities[0].position == arrow.position &&
                chunkEntities[0].flags == 3 &&
                chunkEntities[0].projectileDamage == 6.0f,
                "partitioned arrow state preserves double precision");
        store.saveChunkEntities(-62501, 62500, {});
        require(store.loadChunkEntities(-62501, 62500).empty(),
                "empty entity partition clears stale records");

        {
            std::fstream file(root / "negative-coordinates" / "level.bin",
                              std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(-1, std::ios::end);
            const char corrupt = '\x7f';
            file.write(&corrupt, 1);
        }
        bool rejected = false;
        try {
            (void)store.loadMetadata();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "checksum rejects corrupt metadata");
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }

    std::filesystem::remove_all(root);
    std::cout << "Save store tests passed\n";
    return 0;
}
