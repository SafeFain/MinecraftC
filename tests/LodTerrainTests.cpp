#include "game/SaveStore.h"
#include "threading/ThreadPool.h"
#include "world/LodSettings.h"
#include "world/LodTerrainSystem.h"
#include "world/WorldGenerator.h"

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

bool sameTile(const LodTileData& a, const LodTileData& b) {
    for (size_t i = 0; i < a.columns.size(); ++i) {
        if (a.columns[i].exact != b.columns[i].exact ||
            a.columns[i].spans.size() != b.columns[i].spans.size()) return false;
        for (size_t j = 0; j < a.columns[i].spans.size(); ++j) {
            const LodSpan& x = a.columns[i].spans[j];
            const LodSpan& y = b.columns[i].spans[j];
            if (x.bottom != y.bottom || x.top != y.top || x.block != y.block)
                return false;
        }
    }
    return true;
}

int blockIndex(int x, int y, int z) {
    return x + z * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(y) * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
}
}

int main() {
    WorldGenerator normal(123456789ULL, WorldType::Normal, DimensionId::Overworld);
    const LodTileKey negative{-3, 2, 2};
    const LodTileData first = buildApproximateLodTile(normal, negative);
    const ChunkMesh compactApproximate = buildLodTileMesh(first, 4, 24);
    require(compactApproximate.uploadBytes() < 160u * 1024u,
            "ordinary LOD tiles retain enough GPU budget for outer rings");
    const LodTileData repeated = buildApproximateLodTile(normal, negative);
    require(sameTile(first, repeated),
            "LOD approximation is deterministic at negative coordinates");
    WorldGenerator other(987654321ULL, WorldType::Normal, DimensionId::Overworld);
    require(!sameTile(first, buildApproximateLodTile(other, negative)),
            "LOD approximation varies with the world seed");

    WorldGenerator flat(5, WorldType::Superflat, DimensionId::Overworld);
    const LodTileData flatTile = buildApproximateLodTile(flat, {0, 0, 1});
    const int flatTop = flatTile.columns.front().spans.front().top;
    for (const LodColumn& column : flatTile.columns)
        require(!column.spans.empty() && column.spans.front().top == flatTop,
                "superflat LOD retains a constant surface");

    WorldGenerator heaven(77, WorldType::Normal, DimensionId::Heaven);
    int maximumHeavenLayers = 0;
    for (int tz = -4; tz <= 4; ++tz) {
        for (int tx = -4; tx <= 4; ++tx) {
            const LodTileData tile = buildApproximateLodTile(heaven, {tx, tz, 1});
            for (const LodColumn& column : tile.columns)
                maximumHeavenLayers = std::max(
                    maximumHeavenLayers, static_cast<int>(column.spans.size()));
        }
    }
    require(maximumHeavenLayers >= 2 && maximumHeavenLayers <= 5,
            "Heaven LOD preserves multiple independently sampled island layers");

    std::vector<uint8_t> blocks(Config::CHUNK_VOLUME, 0);
    for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
        for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
            for (int y = 0; y <= 10; ++y)
                blocks[blockIndex(x, y, z)] = static_cast<uint8_t>(BlockId::STONE);
        }
    }
    for (int y = 20; y <= 23; ++y)
        blocks[blockIndex(0, y, 0)] = static_cast<uint8_t>(BlockId::WOOD);
    blocks[blockIndex(0, 30, 0)] = static_cast<uint8_t>(BlockId::WATER);
    const LodTileData exact = extractExactLodChunk(blocks);
    require(exact.at(0, 0).exact && exact.at(0, 0).spans.size() == 3 &&
            exact.at(0, 0).spans[1].bottom == 20 &&
            exact.at(0, 0).spans[2].block == BlockId::WATER,
            "exact LOD extraction retains separated solid and translucent spans");
    const ChunkMesh exactMesh = buildLodTileMesh(exact, 1, 24);
    require(exactMesh.opaqueIndexCount > 0 &&
            exactMesh.translucentIndexCount > 0 &&
            exactMesh.translucentIndexOffset == exactMesh.opaqueIndexCount,
            "LOD mesh hands off opaque and translucent index ranges");
    require(exactMesh.vertices.size() < exactMesh.indices.size(),
            "LOD faces share vertices instead of duplicating triangle corners");

    require(lodHorizontalQuality(LodPrecision::Low) == 64 &&
            lodHorizontalQuality(LodPrecision::Ultra) == 144 &&
            lodVerticalSpanLimit(LodPrecision::Medium) >= 5,
            "precision presets map to fixed horizontal and vertical quality");
    require(lodWorkBudget(LodAggressiveness::PowerSaver).maxInFlight == 1 &&
            lodWorkBudget(LodAggressiveness::Extreme).maxInFlight == 8 &&
            lodWorkBudget(LodAggressiveness::Balanced).completionMs == 1.5,
            "aggressiveness presets map to bounded work budgets");

    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-lod-terrain-tests";
    std::filesystem::remove_all(root);
    SaveStore store(root);
    {
        ThreadPool pool(2);
        LodTerrainSystem system;
        system.setThreadPool(&pool);
        system.setSaveStore(&store);
        system.reset(&normal);
        system.configure({true, 32, LodAggressiveness::PowerSaver,
                          LodPrecision::Low});
        system.update({0.5, 80.0, 0.5}, 8, {});
        pool.waitIdle();
        system.processCompleted(nullptr);
        require(system.residentCpuBytes() > 0,
                "asynchronous LOD completion publishes bounded CPU data");
    }
    bool cacheFound = false;
    const auto tileDirectory = root / "lod" / "r1" / "d_0" / "tiles";
    for (const auto& entry : std::filesystem::directory_iterator(tileDirectory)) {
        cacheFound = entry.is_regular_file();
        if (cacheFound) {
            std::ofstream corrupt(entry.path(), std::ios::binary | std::ios::trunc);
            corrupt << "bad";
            break;
        }
    }
    require(cacheFound, "LOD tiles persist in the per-world derived cache");
    {
        ThreadPool pool(1);
        LodTerrainSystem recovered;
        recovered.setThreadPool(&pool);
        recovered.setSaveStore(&store);
        recovered.reset(&normal);
        recovered.configure({true, 32, LodAggressiveness::PowerSaver,
                             LodPrecision::Low});
        recovered.update({0.5, 80.0, 0.5}, 8, {});
        pool.waitIdle();
        recovered.processCompleted(nullptr);
        require(recovered.residentCpuBytes() > 0,
                "corrupt LOD cache data is ignored and regenerated");
    }
    std::filesystem::remove_all(root);

    std::cout << "LOD terrain tests passed\n";
    return 0;
}
