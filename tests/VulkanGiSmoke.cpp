#include "core/Window.h"
#include "renderer/BlockAtlasData.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"
#include "world/ChunkMesh.h"

#include <iostream>
#include <memory>

namespace {
void requireSmoke(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void runSmoke(const std::filesystem::path& assets, VisualQuality quality) {
    Window window(160, 96, "MinecraftC GI regression", Window::SurfaceMode::Vulkan, false, false);
    VulkanRenderer renderer;
    renderer.initialize(window, assets);
    renderer.setVisualQuality(quality);
    EnhancedVisualSettings settings;
    settings.enabled = true;
    settings.bloom = settings.lightShafts = settings.reflections = false;
    settings.atmosphere = settings.materialMotion = false;
    settings.gi.enabled = true;
    settings.gi.distance = quality == VisualQuality::Ultra ? 256 : 128;
    settings.gi.temporalStability = 0;
    renderer.setEnhancedVisualSettings(settings);
    std::vector<std::unique_ptr<Chunk>> chunks;
    for (int z = -2; z <= 2; ++z) for (int x = -2; x <= 2; ++x) {
        auto chunk = std::make_unique<Chunk>(x, z);
        for (int cz = 0; cz < 16; ++cz) for (int cx = 0; cx < 16; ++cx) {
            chunk->setBlock(cx, 64, cz, BlockId::STONE);
            chunk->setSkyLight(cx, 64, cz, 15);
            if (z == 1) for (int y = 65; y < 76; ++y) {
                chunk->setBlock(cx, y, cz, BlockId::PLANKS);
                chunk->setBlockLight(cx, y, cz, 14);
            }
        }
        chunks.push_back(std::move(chunk));
    }
    ChunkMesh mesh;
    const float stone = float(getAtlasTextureIndex(BlockTexture::Stone));
    const float planks = float(getAtlasTextureIndex(BlockTexture::Planks));
    mesh.vertices = {
        {-32,65,-32,1,1,0,1,0,0,stone,0}, {48,65,-32,1,1,0,1,8,0,stone,0},
        {48,65,48,1,1,0,1,8,8,stone,0}, {-32,65,48,1,1,0,1,0,8,stone,0},
        {-32,65,16,1,0,1,1,0,0,planks,2}, {48,65,16,1,0,1,1,8,0,planks,2},
        {48,76,16,1,0,1,1,8,1,planks,2}, {-32,76,16,1,0,1,1,0,1,planks,2}};
    mesh.indices = {0,2,1,0,3,2,4,6,5,4,7,6};
    mesh.opaqueIndexCount = mesh.indices.size();
    renderer.uploadChunkMesh(mesh);
    glm::vec3 camera(8,70,8);
    RenderEnvironment environment = DayNightCycle{}.evaluate();
    uint64_t sceneId = 17;
    const auto frame = [&] {
        const auto view = glm::lookAt(camera, camera + glm::vec3(0,-0.25f,1), glm::vec3(0,1,0));
        const auto vp = glm::perspective(glm::radians(70.0f), window.aspectRatio(), 0.1f, 512.0f)*view;
        renderer.beginFrame();
        renderer.setEnvironment(environment, camera);
        renderer.setViewProjection(vp);
        renderer.renderSky(environment, glm::inverse(vp), camera, false);
        renderer.beginVoxelGiFrame(glm::dvec3(camera), sceneId);
        for (const auto& chunk : chunks) renderer.submitVoxelGiChunk(*chunk);
        renderer.endVoxelGiFrame();
        renderer.renderChunk(mesh, glm::mat4(1), vp);
        PostProcessState post;
        post.environment = environment;
        post.inverseViewProjection = glm::inverse(vp);
        post.cameraPosition = camera;
        post.sceneId = sceneId;
        renderer.finishScene(post);
        renderer.endFrame();
        requireSmoke(renderer.voxelGiStatus().active, "GI did not activate");
    };
    const auto settle = [&] {
        for (int i = 0; i < 40; ++i) frame();
        requireSmoke(renderer.voxelGiStatus().pendingSlices == 0, "GI queue did not converge");
    };
    settle();
    const auto initial = VulkanGiSmokeProbe::readEffects(renderer);
    float peak = 0;
    float minimumAo = 1;
    for (size_t i = 0; i < initial.size(); ++i) {
        if (i % 4 == 3) minimumAo = std::min(minimumAo, initial[i]);
        else peak = std::max(peak, initial[i]);
    }
    requireSmoke(peak > 0.00001f && minimumAo < 0.99f,
                 "real GPU GI/AO output was missing");
    renderer.setVisualQuality(quality);
    renderer.setEnhancedVisualSettings(settings);
    settings.bloomStrength = 25;
    renderer.setEnhancedVisualSettings(settings);
    frame();
    requireSmoke(renderer.voxelGiStatus().sourceChunks == chunks.size() &&
                 renderer.voxelGiStatus().pendingSlices == 0 &&
                 renderer.voxelGiStatus().uploadedBytes == 0 &&
                 renderer.voxelGiStatus().injectedVoxels == 0,
                 "unchanged quality/unrelated effect rebuilt GI");
    for (bool bloom : {true, false}) {
        settings.bloom = bloom;
        renderer.setEnhancedVisualSettings(settings);
        frame();
        requireSmoke(renderer.voxelGiStatus().sourceChunks == chunks.size() &&
                     renderer.voxelGiStatus().pendingSlices == 0 &&
                     renderer.voxelGiStatus().uploadedBytes == 0 &&
                     renderer.voxelGiStatus().injectedVoxels == 0,
                     "Bloom swapchain rebuild discarded the GI volume cache");
    }
    for (int axis = 0; axis < 3; ++axis) {
        camera[axis] += 1;
        frame();
        const auto partial = renderer.voxelGiStatus();
        requireSmoke(partial.uploadedBytes == 64u*64u*8u &&
                     partial.injectedVoxels == 64u*64u,
                     "single-axis motion did not use one actual GPU plane");
        const auto local = VulkanGiSmokeProbe::readEffects(renderer);
        VulkanGiSmokeProbe::forceFullUpdate(renderer);
        frame();
        const auto full = VulkanGiSmokeProbe::readEffects(renderer);
        requireSmoke(local.size() == full.size(), "GPU comparison size mismatch");
        for (size_t i = 0; i < local.size(); ++i)
            requireSmoke(std::abs(local[i]-full[i]) <= 0.00001f,
                         "partial injection disagrees with full-volume GPU reference");
    }
    camera += glm::vec3(1);
    frame();
    const auto diagonalStatus = renderer.voxelGiStatus();
    requireSmoke(diagonalStatus.uploadedBytes > 0 &&
                 diagonalStatus.injectedVoxels * 8 == diagonalStatus.uploadedBytes &&
                 diagonalStatus.uploadedBytes < 3u*64u*64u*64u*8u,
                 "diagonal move did not retain disjoint partial GPU regions");
    const auto diagonal = VulkanGiSmokeProbe::readEffects(renderer);
    VulkanGiSmokeProbe::forceFullUpdate(renderer);
    frame();
    const auto diagonalFull = VulkanGiSmokeProbe::readEffects(renderer);
    for (size_t i = 0; i < diagonal.size(); ++i)
        requireSmoke(std::abs(diagonal[i]-diagonalFull[i]) <= 0.00001f,
                     "multi-region injection differs from full-volume GPU reference");
    settings.gi.temporalStability = 100;
    renderer.setEnhancedVisualSettings(settings);
    frame();
    requireSmoke(renderer.voxelGiStatus().uploadedBytes == 0 &&
                 renderer.voxelGiStatus().injectedVoxels == 0,
                 "stability adjustment rebuilt GI");
    settings.gi.strength = 50;
    renderer.setEnhancedVisualSettings(settings);
    frame();
    requireSmoke(VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0 &&
                 renderer.voxelGiStatus().uploadedBytes == 0,
                 "strength adjustment kept old-scale history or rebuilt volume");
    chunks[12]->setBlock(8,65,8,BlockId::TORCH);
    frame();
    requireSmoke(VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0,
                 "block edit reused stale lighting history");
    while (renderer.voxelGiStatus().pendingSlices > 0) {
        frame();
        requireSmoke(VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0,
                     "bounded edit propagation accumulated intermediate old lighting");
    }
    settle();
    environment.daylight = 0.2f;
    environment.ambientColor *= 0.5f;
    frame();
    const size_t levels = quality == VisualQuality::Ultra ? 4 : 3;
    requireSmoke(renderer.voxelGiStatus().injectedVoxels == levels*64u*64u*64u &&
                 renderer.voxelGiStatus().uploadedBytes == 0 &&
                 VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0,
                 "environment change did not relight fully and reject stale history");
    ++sceneId;
    settle();
    if (quality == VisualQuality::VeryHigh) {
        settings.gi.distance = 256;
        renderer.setEnhancedVisualSettings(settings);
        settle();
    }
    settings.gi.strength = 0;
    renderer.setEnhancedVisualSettings(settings);
    // The disabled frame has no GI passes or source-cache submissions.
    renderer.beginFrame();
    renderer.finishScene(PostProcessState{});
    renderer.endFrame();
    requireSmoke(!renderer.voxelGiStatus().active, "zero strength left GI active");
    renderer.waitIdle();
    renderer.releaseChunkMesh(mesh);
    std::cout << "GI smoke tier=" << (quality == VisualQuality::Ultra ? "Ultra" : "VeryHigh")
              << " peak=" << peak << " AO=" << minimumAo
              << " single-plane=32768 bytes/4096 voxels; GPU local/full equal\n";
}
}

int main(int argc,char** argv) {
    if (argc != 2) return 2;
    try {
        const auto assets = std::filesystem::absolute(argv[1]);
        runSmoke(assets,VisualQuality::VeryHigh);
        runSmoke(assets,VisualQuality::Ultra);
        std::cout << "Voxel GI Vulkan smoke passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Voxel GI Vulkan smoke failed: " << error.what() << '\n';
        return 1;
    }
}
