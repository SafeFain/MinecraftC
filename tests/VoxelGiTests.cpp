#include "renderer/VoxelGi.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void checkUpdatePolicyAndRegions() {
    EnhancedVisualSettings settings;
    settings.enabled = true;
    settings.gi.enabled = true;
    settings.gi.distance = 128;
    const auto original = voxelGiConfig(VisualQuality::VeryHigh, settings);
    settings.bloomStrength = 25;
    settings.gi.temporalStability = 100;
    auto change = voxelGiConfigChange(original, voxelGiConfig(VisualQuality::VeryHigh, settings));
    require(!change.resetCache && !change.resetHistory && !change.rebuildResources,
            "unrelated effects or stability rebuilt spatial GI");
    settings.gi.strength = 50;
    change = voxelGiConfigChange(original, voxelGiConfig(VisualQuality::VeryHigh, settings));
    require(!change.resetCache && change.resetHistory && !change.rebuildResources,
            "strength change rebuilt geometry or retained old-scale history");
    settings.gi.distance = 256;
    change = voxelGiConfigChange(original, voxelGiConfig(VisualQuality::VeryHigh, settings));
    require(change.resetCache && change.resetHistory && !change.rebuildResources,
            "distance change did not invalidate spatial mapping");
    settings.gi.strength = 0;
    require(voxelGiConfigChange(original, voxelGiConfig(VisualQuality::VeryHigh, settings)).
                rebuildResources, "zero strength did not retire GI resources");

    // Retention over the same wall time must not depend on frame rate or the
    // number of swapchain images between uses of one history image.
    for (int rate : {30, 60, 120}) for (int images : {1, 2, 3}) {
        const float weight = voxelGiHistoryWeight(0.95f, double(images) / rate);
        const double retained = std::pow(weight, double(rate) / images);
        require(std::abs(retained - std::pow(0.95, 60.0)) < 0.00001,
                "GI history response depends on FPS or swapchain image count");
    }
    require(voxelGiHistoryWeight(0.95f, 1.0) == 0 &&
            voxelGiHistoryWeight(0.95f, 0.0) == 0 &&
            voxelGiHistoryWeight(0.0f, 0.02) == 0,
            "paused, uninitialized or disabled history accumulated stale GI");

    for (int resolution : {32, 48, 64}) {
        const size_t volume = static_cast<size_t>(resolution) * resolution * resolution;
        std::vector<uint8_t> source(volume * 4);
        for (size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<uint8_t>((i * 37 + i / 251) % 255);
        for (int fixture = 0; fixture < 12; ++fixture) {
            VoxelGiDirtyRegions dirty;
            std::array<std::array<bool, 64>, 3> selected{};
            for (int axis = 0; axis < 3; ++axis) {
                if (fixture < 3 && fixture != axis) continue;
                for (int i = 0; i < (fixture < 3 ? 1 : fixture - 1); ++i) {
                    const int layer = (i * 13 + fixture * 7 + axis * 3) % resolution;
                    dirty.mark(axis, layer);
                    selected[axis][layer] = true;
                }
            }
            const auto regions = dirty.regions(resolution);
            std::vector<uint8_t> output(volume * 4, 255);
            std::vector<bool> touched(volume);
            size_t bytes = 0;
            for (const auto& box : regions) {
                std::vector<uint8_t> packed;
                packVoxelGiRegion(source, resolution, box, packed);
                bytes += packed.size();
                size_t read = 0;
                for (int z = 0; z < box.extent.z; ++z)
                    for (int y = 0; y < box.extent.y; ++y)
                        for (int x = 0; x < box.extent.x; ++x) {
                            const size_t index = static_cast<size_t>(box.offset.x + x + resolution *
                                (box.offset.y + y + resolution * (box.offset.z + z)));
                            require(!touched[index], "partial injection boxes overlap");
                            touched[index] = true;
                            std::copy_n(packed.data() + read, 4, output.data() + index * 4);
                            read += 4;
                        }
            }
            const bool full = regions.size() == 1 && regions[0].voxelCount() == volume;
            for (int z = 0; z < resolution; ++z)
                for (int y = 0; y < resolution; ++y)
                    for (int x = 0; x < resolution; ++x) {
                        const size_t index = x + resolution * (y + resolution * z);
                        const bool expected = full || selected[0][x] || selected[1][y] || selected[2][z];
                        require(touched[index] == expected,
                                "plane union missed dirty cells or expanded across an unrelated axis");
                        if (expected) for (int channel = 0; channel < 4; ++channel)
                            require(output[index * 4 + channel] == source[index * 4 + channel],
                                    "packed GPU region has incorrect axis/row/depth order");
                    }
            if (fixture < 3) require(bytes * 2 == static_cast<size_t>(resolution) * resolution * 8,
                                    "single-axis upload is not two actual attribute planes");
        }
        VoxelGiDirtyRegions empty;
        require(empty.regions(resolution).empty(), "unchanged volume has GPU work");
        empty.markFull();
        require(empty.regions(resolution).size() == 1 &&
                empty.regions(resolution)[0].voxelCount() == volume,
                "full initialization or relighting missed volume cells");
    }
}
}

int main() {
    checkUpdatePolicyAndRegions();
    VoxelGiDeviceSupport supported{64, 8, 8, 1, 1, true, true, true};
    require(voxelGiAvailability(supported) == VoxelGiAvailability::Available,
            "valid Vulkan 1.0 GI capability profile was rejected");
    supported.maximum3dDimension = 32;
    require(voxelGiAvailability(supported) ==
                VoxelGiAvailability::UnsupportedLimits,
            "undersized 3D image limit did not disable GI locally");
    supported.maximum3dDimension = 64;
    supported.irradianceStorage = false;
    require(voxelGiAvailability(supported) ==
                VoxelGiAvailability::UnsupportedFormat,
            "missing storage-image format support did not disable GI locally");
    supported.irradianceStorage = true;
    supported.sampledImagesPerSet = 7;
    require(voxelGiAvailability(supported) == VoxelGiAvailability::UnsupportedLimits,
            "seven samplers accepted an eight-sampler GI screen shader");
    require(voxelGiFloorDiv(-1, 16) == -1 &&
            voxelGiFloorDiv(-16, 16) == -1 &&
            voxelGiFloorDiv(-17, 16) == -2 &&
            voxelGiPositiveMod(-1, 32) == 31,
            "negative GI clipmap coordinates do not use floor division");

    const VoxelGiPacked stone = packVoxelGi(BlockId::STONE, 0xf0);
    const VoxelGiPacked leaves = packVoxelGi(BlockId::LEAVES, 0x00);
    const VoxelGiPacked water = packVoxelGi(BlockId::WATER, 0x00);
    const VoxelGiPacked flower = packVoxelGi(BlockId::FLOWER, 0x00);
    const VoxelGiPacked slab = packVoxelGi(BlockId::PLANKS_SLAB_BOTTOM, 0x00);
    const VoxelGiPacked torch = packVoxelGi(BlockId::TORCH, 0x0f);
    require(stone.opacity == 255 && stone.skyLight == 255 && stone.valid == 255 &&
            leaves.opacity > water.opacity && leaves.opacity < stone.opacity &&
            flower.opacity < leaves.opacity && slab.opacity < stone.opacity &&
            torch.emission > 0 && torch.blockLight == 255,
            "voxel attributes do not preserve material, partial occlusion, or light");

    EnhancedVisualSettings settings;
    settings.enabled = true;
    settings.gi.enabled = true;
    settings.gi.distance = 32;
    VoxelGiConfig config = voxelGiConfig(VisualQuality::Low, settings);
    config.updateSlicesPerFrame = 128;
    VoxelGiSceneCache cache;
    cache.configure(config);
    Chunk chunk(-1, -1);
    chunk.setBlock(15, 64, 15, BlockId::TORCH);
    chunk.setBlockLight(15, 64, 15, 15);
    cache.beginFrame(glm::dvec3(-0.5, 64.0, -0.5), 7);
    require(cache.submit(chunk), "new GI chunk snapshot was not accepted");
    require(!cache.submit(chunk), "unchanged GI chunk revision copied twice");
    cache.endFrame();
    const VoxelGiLevelMapping mapping = cache.levelMapping(0);
    require(mapping.cellSize == 1 && mapping.minimumCell.x == -17 &&
            mapping.minimumCell.z == -17,
            "camera-centered clipmap mapping is unstable at negative coordinates");
    const auto initial = cache.takeUpdates(128);
    bool foundTorch = false;
    bool foundInvalid = false;
    for (const VoxelGiSliceUpdate& update : initial) {
        if (update.level != 0 || update.axis != 2 || update.layer != 31 || update.voxels.empty()) continue;
        const VoxelGiPacked& voxel = update.voxels[31];
        foundTorch = voxel.emission > 0 && voxel.valid == 255;
        foundInvalid = update.voxels.front().valid == 0;
    }
    require(foundTorch && foundInvalid,
            "clipmap slices do not distinguish loaded voxels from invalid edges");

    cache.beginFrame(glm::dvec3(-0.5, 64.0, 0.5), 7);
    cache.submit(chunk);
    cache.endFrame();
    require(cache.pendingSlices() <= static_cast<size_t>(config.clipmapLevels),
            "one-cell movement rebuilt the complete clipmap instead of exposed slices");
    cache.takeUpdates(128);
    chunk.setBlock(15, 64, 15, BlockId::AIR);
    cache.beginFrame(glm::dvec3(-0.5, 64.0, 0.5), 7);
    require(cache.submit(chunk), "block revision did not dirty its intersecting slices");
    cache.endFrame();
    require(cache.pendingSlices() > 0,
            "block revision did not schedule an incremental clipmap update");

    cache.beginFrame(glm::dvec3(-0.5, 64.0, 0.5), 8);
    require(cache.pendingSlices() == static_cast<size_t>(
                config.clipmapResolution * config.clipmapLevels),
            "world or dimension change did not invalidate GI clipmaps");

    // A source set larger than a single frame's copy budget must converge,
    // retain unchanged snapshots, and make the full 3D domain valid.
    settings.gi.distance = 32;
    config = voxelGiConfig(VisualQuality::Low, settings);
    cache.configure(config);
    std::vector<std::unique_ptr<Chunk>> sources;
    for (int z = -2; z <= 2; ++z)
        for (int x = -2; x <= 2; ++x)
            sources.push_back(std::make_unique<Chunk>(x, z));
    for (int frame = 0; frame < 32; ++frame) {
        cache.beginFrame(glm::dvec3(0.5, 64.0, 0.5), 12);
        int accepted = 0;
        for (const auto& source : sources) if (cache.submit(*source)) ++accepted;
        require(accepted <= config.updateSlicesPerFrame * 2,
                "source snapshots exceeded their bounded copy budget");
        if (frame > 3) require(accepted == 0, "unchanged active chunks were evicted and recopied");
        cache.endFrame();
        auto updates = cache.takeUpdates(config.updateSlicesPerFrame);
        cache.recycleUpdates(updates);
    }
    require(cache.cachedChunks() > 8 && cache.pendingSlices() == 0,
            "multi-frame source submission did not converge");
    const auto mappingBefore = cache.levelMapping(0);
    cache.beginFrame(glm::dvec3(0.5, 65.0, 0.5), 12);
    for (const auto& source : sources) cache.submit(*source);
    cache.endFrame();
    for (const auto& update : cache.takeUpdates(16)) {
        if (update.voxels.empty()) continue;
        require(update.axis == 1 && update.voxels.size() == 32 * 32,
                "vertical movement did not return its exposed XY plane");
        for (const auto& voxel : update.voxels)
            require(voxel.valid == 255 && voxel.opacity == 0,
                    "complete loaded XY plane left invalid voxels");
    }
    require(cache.levelMapping(0).minimumCell.y == mappingBefore.minimumCell.y + 1,
            "camera Y movement did not advance the three-dimensional origin");

    // Coverage is at least twice the requested radius for every quality/distance.
    for (VisualQuality quality : VISUAL_QUALITY_ORDER) {
        for (uint16_t distance : {32, 64, 128, 256}) {
            settings.gi.distance = distance;
            const auto tier = voxelGiConfig(quality, settings);
            const int coarse = voxelGiCellSize(tier, tier.clipmapLevels - 1);
            require(coarse * tier.clipmapResolution >= distance * 2 &&
                    coarse * tier.clipmapResolution < distance * 4,
                    "coarse clipmap does not cover its requested radius");
            for (int level = 1; level < tier.clipmapLevels; ++level)
                require(voxelGiCellSize(tier, level) >= voxelGiCellSize(tier, level - 1),
                        "clipmap cell sizes do not increase toward coarse levels");
        }
    }
    settings.gi.strength = 0;
    require(!voxelGiConfig(VisualQuality::Ultra, settings).enabled,
            "zero GI strength still requests GPU work");
    settings.gi.strength = 100;
    settings.gi.distance = 128;
    config = voxelGiConfig(VisualQuality::High, settings); // non-power-of-two 48³
    config.updateSlicesPerFrame = 128;
    cache.configure(config);
    std::array<std::vector<VoxelGiPacked>, 4> volumes;
    for (auto& volume : volumes)
        volume.resize(static_cast<size_t>(config.clipmapResolution *
            config.clipmapResolution * config.clipmapResolution));
    const auto apply = [&](const std::vector<VoxelGiSliceUpdate>& updates) {
        const int resolution = config.clipmapResolution;
        for (const auto& update : updates) {
            const int first = update.axis == 0 ? 1 : 0;
            const int second = update.axis == 2 ? 1 : 2;
            for (int v = 0; v < resolution; ++v)
                for (int u = 0; u < resolution; ++u) {
                    glm::ivec3 ring(0);
                    ring[update.axis] = update.layer;
                    ring[first] = u;
                    ring[second] = v;
                    volumes[update.level][static_cast<size_t>(ring.x + resolution *
                        (ring.y + resolution * ring.z))] = update.voxels.empty()
                        ? VoxelGiPacked{} : update.voxels[u + v * resolution];
                }
        }
    };
    const auto verify = [&] {
        const int resolution = config.clipmapResolution;
        for (int level = 0; level < config.clipmapLevels; ++level) {
            const auto map = cache.levelMapping(level);
            for (int z = 0; z < resolution; ++z)
                for (int y = 0; y < resolution; ++y)
                    for (int x = 0; x < resolution; ++x) {
                        const glm::ivec3 cell = map.minimumCell + glm::ivec3(x, y, z);
                        const glm::ivec3 world = cell * map.cellSize +
                            glm::ivec3(map.cellSize / 2);
                        glm::ivec3 ring;
                        for (int axis = 0; axis < 3; ++axis)
                            ring[axis] = voxelGiPositiveMod(cell[axis], resolution);
                        const auto& actual = volumes[level][static_cast<size_t>(ring.x +
                            resolution * (ring.y + resolution * ring.z))];
                        const bool loaded = Config::isValidWorldY(world.y) &&
                            voxelGiFloorDiv(world.x, 16) == chunk.cx &&
                            voxelGiFloorDiv(world.z, 16) == chunk.cz;
                        VoxelGiPacked expected;
                        if (loaded) {
                            expected = packVoxelGi(BlockId::AIR, 0);
                            // The fixture contains a single stone block. Conservative
                            // coarse occupancy must keep it even away from the center.
                            const glm::ivec3 minimum = cell * map.cellSize;
                            if (minimum.x <= -1 && minimum.x + map.cellSize > -1 &&
                                minimum.z <= -1 && minimum.z + map.cellSize > -1 &&
                                minimum.y <= 64 && minimum.y + map.cellSize > 64)
                                expected = packVoxelGi(BlockId::STONE, 0);
                        }
                        require(actual.valid == expected.valid &&
                                actual.opacity == expected.opacity &&
                                actual.red == expected.red,
                                "three-dimensional ring contents differ from world sampling");
                    }
        }
    };
    chunk.setBlock(15, 64, 15, BlockId::STONE);
    glm::dvec3 camera(-0.5, 64.0, -0.5);
    cache.beginFrame(camera, 9);
    cache.submit(chunk);
    cache.endFrame();
    apply(cache.takeUpdates(1024));
    verify();
    // All three axes update only the entering plane and preserve the overlap.
    for (int axis = 0; axis < 3; ++axis) {
        camera[axis] += 1.0;
        cache.beginFrame(camera, 9);
        cache.submit(chunk);
        cache.endFrame();
        require(cache.pendingSlices() <= static_cast<size_t>(config.clipmapLevels),
                "one-cell X/Y/Z movement rebuilt complete volumes");
        apply(cache.takeUpdates(1024));
        verify();
    }
    // Sub-cell motion in coarse levels must not move their ring mapping.
    const auto coarseBefore = cache.levelMapping(config.clipmapLevels - 1);
    camera.z += 0.1;
    cache.beginFrame(camera, 9);
    cache.submit(chunk);
    cache.endFrame();
    require(cache.levelMapping(config.clipmapLevels - 1).minimumCell ==
                coarseBefore.minimumCell && cache.pendingSlices() == 0,
            "coarse rings move within a world cell");
    Chunk unrelated(100, -1);
    require(!cache.submit(unrelated) && cache.pendingSlices() == 0,
            "unrelated X chunk consumed snapshot budget or dirtied Z planes");
    // Large positive/negative moves cancel obsolete pending work and invalidate
    // stale ring contents even when the resampling budget is exhausted.
    for (int frame = 0; frame < 100; ++frame) {
        camera.x = frame % 2 ? -10.5 : 500.5;
        camera.y = frame % 2 ? 100.0 : -20.0;
        cache.beginFrame(camera, 9);
        cache.submit(chunk);
        cache.endFrame();
        apply(cache.takeUpdates(1));
        require(cache.pendingSlices() <= static_cast<size_t>(
                    3 * config.clipmapResolution * config.clipmapLevels),
                "rapid movement accumulated obsolete clipmap work");
    }
    apply(cache.takeUpdates(1024));
    verify();
    for (int frame = 0; frame < 4; ++frame) {
        cache.beginFrame(camera, 9);
        cache.endFrame(); // chunk disappeared from the active set
        apply(cache.takeUpdates(1024));
    }
    require(cache.cachedChunks() == 0, "retired source chunk remained cached");
    for (const auto& volume : volumes)
        for (const auto& voxel : volume)
            require(voxel.valid == 0, "retired chunk left stale valid irradiance input");

    std::cout << "Voxel GI tests passed\n";
    return 0;
}
