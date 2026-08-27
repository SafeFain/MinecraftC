#include "game/SaveStore.h"
#include "Config.h"
#include "world/WorldGenContext.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

uint64_t payloadChecksum(const std::vector<uint8_t>& bytes, size_t offset) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = offset; i < bytes.size(); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

void writeLittleEndian(std::vector<uint8_t>& bytes, size_t offset,
                       uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
        bytes[offset + i] = static_cast<uint8_t>(value & 0xffU);
        value >>= 8;
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
        source.worldType = WorldType::Superflat;
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
        source.foodTickTimer = 73;
        source.inventory.slot(0) = {ItemId::IRON_PICKAXE, 1, 42};
        source.inventory.slot(9) = {ItemId::COAL, 37, 0};
        source.inventory.slot(10) = {ItemId::LIMESTONE, 23, 0};
        source.inventory.armor()[1] = {ItemId::IRON_CHESTPLATE, 1, 12};
        source.inventory.offhand() = {ItemId::SHIELD, 1, 4};
        source.entities.push_back({
            5, {-20.0f, 64.0f, 8.0f}, {0.1f, 0.0f, 0.2f},
            12.0f, 34.0f, {}, 98765, 0, 0.0f, {}
        });
        // PrimedTnt (EntityType value 10) must load without tripping the
        // entity-type validation in readEntity().
        source.entities.push_back({
            10, {3.0f, 64.0f, -7.0f}, {0.0f, 0.2f, 0.0f},
            1.0f, 2.5f, {}, 424242, 0, 0.0f, {}
        });
        WorldMetadata::PersistedEntity villager;
        villager.type = 11;
        villager.position = {8.5, 70.0, -4.5};
        villager.health = 20.0f;
        villager.villager.profession = VillagerProfession::Toolsmith;
        villager.villager.level = 4;
        villager.villager.experience = 188;
        villager.villager.offerSeed = 998877;
        villager.villager.uses = {{1, 2, 3, 4, 5}};
        villager.villager.hasBed = true;
        villager.villager.claimedBed = {7, 70, -4};
        villager.villager.hasWorkstation = true;
        villager.villager.claimedWorkstation = {9, 70, -4};
        villager.villager.professionLocked = true;
        villager.villager.lastRestockDay = 12;
        villager.villager.restocksToday = 2;
        source.entities.push_back(villager);
        source.activeDimension = DimensionId::Heaven;
        source.overworldDayPhase = 0.37f;
        source.heaven.playerPosition = {12.5, 144.0, -8.5};
        source.heaven.safePosition = {12, 144, -9};
        source.heaven.hasSafePosition = true;
        source.heaven.worldTicks = 987654;
        source.heaven.dayPhase = 0.81f;

        store.saveMetadata(source);
        require(store.exists(), "metadata file is created");
        const auto loaded = store.loadMetadata();
        require(loaded.displayName == source.displayName, "world name round trips");
        require(loaded.seed == source.seed, "64-bit seed round trips");
        require(loaded.gameMode == GameMode::Survival &&
                loaded.difficulty == Difficulty::Hard,
                "game rules round trip");
        require(loaded.worldType == WorldType::Superflat,
                "world type round trips");
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
        require(loaded.foodTickTimer == source.foodTickTimer,
                "food tick timer round trips in v11");
        require(loaded.inventory.slot(10).id == ItemId::LIMESTONE &&
                loaded.inventory.slot(10).count == 23,
                "appended natural material item round trips");
        require(loaded.entities.size() == 3 &&
                loaded.entities[0].position == source.entities[0].position &&
                loaded.entities[1].type == 10 &&
                loaded.entities[1].position == source.entities[1].position &&
                loaded.entities[2].villager.profession ==
                    VillagerProfession::Toolsmith &&
                loaded.entities[2].villager.uses[4] == 5 &&
                loaded.entities[2].villager.claimedBed == glm::ivec3(7,70,-4) &&
                loaded.entities[2].villager.professionLocked &&
                loaded.entities[2].villager.restocksToday == 2,
                "persistent entities round trip");
        store.saveChunkEntityPopulationVersion(-3, 9, 1);
        require(store.loadChunkEntityPopulationVersion(-3, 9) == 1 &&
                store.loadChunkEntityPopulationVersion(-3, 8) == 0,
                "chunk entity population version did not round trip safely");
        const ChunkLoadBundle populationBundle =
            store.loadChunkLoadBundle(-3, 9, 12345);
        require(populationBundle.entityPopulationVersion == 1,
                "async chunk bundle omitted the entity population version");
        require(loaded.activeDimension == DimensionId::Heaven &&
                std::abs(loaded.overworldDayPhase - 0.37f) < 0.0001f &&
                loaded.heaven.playerPosition == source.heaven.playerPosition &&
                loaded.heaven.safePosition == source.heaven.safePosition &&
                loaded.heaven.hasSafePosition &&
                loaded.heaven.worldTicks == source.heaven.worldTicks &&
                std::abs(loaded.heaven.dayPhase - source.heaven.dayPhase) < 0.0001f,
                "independent dimension state round trips");

        WorldMetadata replacement = source;
        replacement.worldTicks += 1;
        store.saveMetadata(replacement);
        require(store.loadMetadata().worldTicks == replacement.worldTicks,
                "saving again atomically replaces an existing metadata file");

        const auto metadataPath = root / "negative-coordinates" / "level.bin";
        std::array<uint8_t, 47> prefix{};
        {
            std::ifstream file(metadataPath, std::ios::binary);
            file.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
            require(file.gcount() == static_cast<std::streamsize>(prefix.size()),
                    "save prefix is readable");
        }
        require(prefix[8] == SAVE_FORMAT_VERSION && prefix[9] == 0 &&
                prefix[10] == 0 && prefix[11] == 0,
                "save version is encoded as little-endian uint32");
        constexpr std::array<uint8_t, 8> expectedSeed =
            {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};
        require(std::equal(expectedSeed.begin(), expectedSeed.end(), prefix.begin() + 39),
                "64-bit seed is encoded in canonical little-endian order");

        const auto legacyDirectory = root / "legacy-v7";
        std::filesystem::create_directories(legacyDirectory);
        const auto legacyPath = legacyDirectory / "level.bin";
        WorldMetadata legacySource = source;
        legacySource.entities.clear();
        SaveStore(legacyDirectory).saveMetadata(legacySource);
        {
            std::ifstream input(legacyPath, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            require(bytes.size() > 24, "legacy fixture has a complete save header");
            bytes.pop_back(); // v7 has no appended world-type byte.
            writeLittleEndian(bytes, 8, 7, 4);
            writeLittleEndian(bytes, 12, bytes.size() - 24, 4);
            writeLittleEndian(bytes, 16, payloadChecksum(bytes, 24), 8);
            std::ofstream output(legacyPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        require(SaveStore(legacyDirectory).loadMetadata().seed == source.seed,
                "existing little-endian v7 metadata remains readable");
        require(SaveStore(legacyDirectory).loadMetadata().worldType == WorldType::Normal,
                "legacy metadata defaults to normal terrain");

        const auto v10Directory = root / "legacy-v10";
        std::filesystem::create_directories(v10Directory);
        const auto v10Path = v10Directory / "level.bin";
        SaveStore(v10Directory).saveMetadata(legacySource);
        {
            std::vector<uint8_t> bytes = readBytes(v10Path);
            require(bytes.size() > 28, "v10 fixture has a food timer tail");
            bytes.resize(bytes.size() - sizeof(uint32_t));
            writeLittleEndian(bytes, 8, 10, 4);
            writeLittleEndian(bytes, 12, bytes.size() - 24, 4);
            writeLittleEndian(bytes, 16, payloadChecksum(bytes, 24), 8);
            std::ofstream output(v10Path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        const auto migratedV10 = SaveStore(v10Directory).loadMetadata();
        require(migratedV10.foodTickTimer == 0,
                "v10 metadata defaults the Java food timer to zero");
        require(migratedV10.activeDimension == source.activeDimension &&
                migratedV10.heaven.worldTicks == source.heaven.worldTicks,
                "v10 dimension state remains readable after migration");

        const std::vector<BlockOverride> overrides = {
            {0, BlockId::AIR},
            {static_cast<uint32_t>(15 + 15 * 16 +
                Config::worldYToStorageY(319) * 256), BlockId::DIAMOND_ORE},
            {513, BlockId::FARMLAND_7},
            {514, BlockId::ACACIA_SAPLING},
            {515, BlockId::GRANITE},
            {516, BlockId::WHITE_BED_HEAD_EAST}
        };
        store.saveChunkOverrides(-2, -7, overrides);
        const auto loadedOverrides = store.loadChunkOverrides(-2, -7);
        require(loadedOverrides.size() == 6, "chunk overrides round trip");
        require(loadedOverrides[0].block == BlockId::AIR,
                "explicit AIR override is preserved");
        require(loadedOverrides[1].localIndex == overrides[1].localIndex,
                "highest local block index is valid");
        require(loadedOverrides[2].block == BlockId::FARMLAND_7 &&
                loadedOverrides[3].block == BlockId::ACACIA_SAPLING,
                "new farming block ids round trip in save format 5");
        require(loadedOverrides[4].block == BlockId::GRANITE,
                "v7 appended natural block id round trips in the current save format");
        require(loadedOverrides[5].block == BlockId::WHITE_BED_HEAD_EAST,
                "appended directional bed state round trips in the current save format");
        require(store.loadChunkOverrides(4, 9).empty(),
                "unmodified chunks have no overrides");

        std::vector<uint8_t> generated(Config::CHUNK_VOLUME,
                                       static_cast<uint8_t>(BlockId::STONE));
        generated.front() = static_cast<uint8_t>(BlockId::BEDROCK);
        generated.back() = static_cast<uint8_t>(BlockId::AIR);
        generated[513] = static_cast<uint8_t>(BlockId::BASALT);
        store.saveGeneratedChunk(-2, -7, generated,
                                 WorldGenContext::GENERATION_VERSION);
        const auto loadedGenerated = store.loadGeneratedChunk(
            -2, -7, WorldGenContext::GENERATION_VERSION);
        require(loadedGenerated && *loadedGenerated == generated,
                "pregenerated chunk cache round trips");
        const auto generatedPath = root / "negative-coordinates" /
            "generated" / "g.-2.-7.bin";
        const auto compressedBytes = readBytes(generatedPath);
        require(compressedBytes.size() > 41 && compressedBytes[40] == 1,
                "compressible generated cache selects the RLE codec");
        require(!store.loadGeneratedChunk(
                    -2, -7, WorldGenContext::GENERATION_VERSION + 1),
                "pregenerated chunk cache rejects a different generation version");

        // Rebuild the same file using the pre-codec payload layout.  This is
        // the format shipped by existing worlds and must remain readable.
        std::vector<uint8_t> legacyPayload;
        legacyPayload.resize(16 + generated.size());
        writeLittleEndian(legacyPayload, 0,
                          static_cast<uint32_t>(static_cast<int32_t>(-2)), 4);
        writeLittleEndian(legacyPayload, 4,
                          static_cast<uint32_t>(static_cast<int32_t>(-7)), 4);
        writeLittleEndian(legacyPayload, 8,
                          WorldGenContext::GENERATION_VERSION, 4);
        writeLittleEndian(legacyPayload, 12,
                          static_cast<uint32_t>(generated.size()), 4);
        std::copy(generated.begin(), generated.end(), legacyPayload.begin() + 16);
        auto legacyBytes = compressedBytes;
        legacyBytes.resize(24 + legacyPayload.size());
        std::copy(legacyPayload.begin(), legacyPayload.end(), legacyBytes.begin() + 24);
        writeLittleEndian(legacyBytes, 12,
                          static_cast<uint32_t>(legacyPayload.size()), 4);
        writeLittleEndian(legacyBytes, 16, payloadChecksum(legacyBytes, 24), 8);
        {
            std::ofstream output(generatedPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(legacyBytes.data()),
                         static_cast<std::streamsize>(legacyBytes.size()));
        }
        const auto loadedLegacy = store.loadGeneratedChunk(
            -2, -7, WorldGenContext::GENERATION_VERSION);
        require(loadedLegacy && *loadedLegacy == generated,
                "pre-codec raw generated cache remains readable");

        // An incompressible block stream uses the raw fallback marker rather
        // than growing by the codec header and run table.
        std::vector<uint8_t> incompressible(Config::CHUNK_VOLUME);
        for (size_t i = 0; i < incompressible.size(); ++i)
            incompressible[i] = static_cast<uint8_t>(i %
                static_cast<size_t>(BlockId::COUNT));
        store.saveGeneratedChunk(-3, 4, incompressible,
                                 WorldGenContext::GENERATION_VERSION);
        const auto rawFallback = readBytes(root / "negative-coordinates" /
            "generated" / "g.-3.4.bin");
        require(rawFallback.size() > 40 && rawFallback[40] == 0,
                "incompressible generated cache selects raw fallback");
        const auto loadedRawFallback = store.loadGeneratedChunk(
            -3, 4, WorldGenContext::GENERATION_VERSION);
        require(loadedRawFallback && *loadedRawFallback == incompressible,
                "raw fallback cache round trips exactly");
        const auto bundle = store.loadChunkLoadBundle(
            -3, 4, WorldGenContext::GENERATION_VERSION);
        require(bundle.generated && *bundle.generated == incompressible &&
                    bundle.overrides.empty() && bundle.blockEntities.empty() &&
                    bundle.entities.empty(),
                "chunk load bundle publishes all cache partitions together");
        {
            std::fstream file(generatedPath,
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
