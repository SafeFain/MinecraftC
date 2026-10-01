#include "core/Window.h"
#include "renderer/BlockAtlasData.h"
#include "renderer/CloudRenderData.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "world/ChunkMesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void runSmoke(const std::filesystem::path& assets) {
    Window window(640, 360, "MinecraftC Cloud LOD smoke",
                  Window::SurfaceMode::Vulkan, false, false);
    VulkanRenderer renderer;
    renderer.initialize(window, assets);
    renderer.setVisualQuality(VisualQuality::Medium);
    renderer.setEnhancedVisuals(false);

    ChunkMesh terrain;
    const float tile = static_cast<float>(getAtlasTextureIndex(BlockTexture::Stone));
    terrain.vertices = {
        {-128, 200, 256, 1, 1, 0, 1, 0, 0, tile, 4},
        {128, 200, 256, 1, 1, 0, 1, 1, 0, tile, 4},
        {128, 200, 512, 1, 1, 0, 1, 1, 1, tile, 4},
        {-128, 200, 512, 1, 1, 0, 1, 0, 1, tile, 4}};
    terrain.indices = {0, 2, 1, 0, 3, 2};
    terrain.opaqueIndexCount = 6;
    renderer.uploadChunkMesh(terrain);
    const RenderEnvironment environment = DayNightCycle{}.evaluate();
    for (int frame = 0; frame < 24; ++frame) {
        const float y = frame < 8 ? 80.0f : frame < 16 ? 193.0f : 210.0f;
        const glm::vec3 camera(0, y, 0);
        const glm::mat4 view = glm::lookAt(
            camera, camera + glm::vec3(0, 0.1f, 1), glm::vec3(0, 1, 0));
        const glm::mat4 nearVp = glm::perspective(
            glm::radians(70.0f), window.aspectRatio(),
            Config::NEAR_PLANE, Config::FAR_PLANE) * view;
        const glm::mat4 farVp = glm::perspective(
            glm::radians(70.0f), window.aspectRatio(),
            8.0f, Config::CLOUD_LOD_DISTANCE * 1.5f) * view;
        renderer.beginFrame();
        renderer.setEnvironment(environment, camera);
        renderer.setViewProjection(nearVp);
        renderer.renderSky(environment, glm::inverse(nearVp), camera, true);
        renderer.renderClouds({-0.5 + frame * 16.0, y, -0.5}, farVp, 77,
                              20.0f + frame, frame < 12 ? 192 : 1024);
        if (frame % 2 == 0)
            renderer.renderLod(terrain, glm::mat4(1), farVp, {0, 0, 0, 1}, 0, 4096);
        renderer.renderChunk(terrain,
            glm::translate(glm::mat4(1), glm::vec3(0, -20, -200)), nearVp);
        renderer.finishScene(PostProcessState{});
        renderer.endFrame();
        // Sky, both cloud projections and near terrain; alternate frames add LOD.
        if (renderer.performanceStats().drawCalls < (frame % 2 == 0 ? 5u : 4u))
            throw std::runtime_error("Cloud/terrain smoke did not submit the expected draw passes");
    }
    renderer.waitIdle();
    renderer.releaseChunkMesh(terrain);
    for (int radius : {6, 12, 32, 64}) {
        const auto start = std::chrono::steady_clock::now();
        const auto clouds = buildCloudLodInstances(77, -17, -31, radius);
        if (clouds.empty() || clouds.size() >= 65536)
            throw std::runtime_error("Cloud LOD smoke instance budget invalid");
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "radius=" << radius << " instances=" << clouds.size()
                  << " rebuild_ms=" << ms << '\n';
    }
    std::cout << "Cloud LOD smoke: 24 Vulkan frames, below/inside/above cloud layer, "
                 "moving sampling window, distance changes and near/far terrain passes\n";
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: vulkan_cloud_lod_smoke <asset-directory>\n";
        return 2;
    }
    try {
        const auto assets = std::filesystem::absolute(argv[1]);
        if (!std::filesystem::is_directory(assets))
            throw std::invalid_argument("Asset directory does not exist");
        runSmoke(assets);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cloud LOD smoke failed: " << error.what() << '\n';
        return 1;
    }
}
