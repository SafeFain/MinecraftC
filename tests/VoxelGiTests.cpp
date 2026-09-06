#include "renderer/VoxelGi.h"

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
    VoxelGiDeviceSupport supported{64, 7, 7, 1, 1, true, true, true, true};
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
        if (update.level != 0 || update.zLayer != 31) continue;
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

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    require(!voxelGiRejectHistory(10.0f, 10.1f, normal, normal, true, true) &&
            voxelGiRejectHistory(10.0f, 12.0f, normal, normal, true, true) &&
            voxelGiRejectHistory(10.0f, 10.0f, normal, -normal, true, true) &&
            voxelGiRejectHistory(10.0f, 10.0f, normal, normal, false, true) &&
            voxelGiNeighborhoodClamp(glm::vec3(2.0f, -1.0f, 0.5f),
                glm::vec3(0.0f), glm::vec3(1.0f)) == glm::vec3(1.0f, 0.0f, 0.5f),
            "temporal rejection or neighborhood clamping contract changed");

    std::cout << "Voxel GI tests passed\n";
    return 0;
}
