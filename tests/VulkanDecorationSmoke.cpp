#include "core/Window.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "world/ChunkMesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: vulkan_decoration_smoke <asset-directory> [--natural|--natural-preview]\n";
        return 2;
    }
    const std::string mode = argc == 3 ? argv[2] : "";
    if (!mode.empty() && mode != "--natural" && mode != "--natural-preview") return 2;
    const bool natural = !mode.empty();
    const int frames = mode == "--natural-preview" ? 1200 : 120;
    const int count = natural ? 24 : 18;
    const int first = natural ? 201 : 183;
    const int cubes = natural ? 13 : 18;
    const int plants = natural ? 11 : 0;
    try {
        Window window(960, 540, "MinecraftC natural materials smoke",
                      Window::SurfaceMode::Vulkan, false, false);
        VulkanRenderer renderer;
        renderer.initialize(window, std::filesystem::absolute(argv[1]));
        if (getAtlasTextureIndex(BlockTexture::BlackWool) != 161)
            throw std::runtime_error("Black wool lost its generated atlas slot");
        renderer.setVisualQuality(VisualQuality::Medium);
        renderer.setEnhancedVisuals(false);
        std::vector<uint8_t> blocks(Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z *
                                    Config::CHUNK_SIZE_Y, 0);
        int maxima[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]{};
        for (int i = 0; i < count; ++i) {
            const int x = 2 + (i % 6) * 2;
            const int y = 66 + (3 - i / 6) * 2;
            const int z = 8;
            blocks[x + z * 16 + Config::worldYToStorageY(y) * 256] =
                static_cast<uint8_t>(first + i);
            maxima[x][z] = y;
        }
        ChunkMesh mesh;
        mesh.build(0, 0, blocks.data(), maxima,
            [](int, int, int) { return BlockId::AIR; },
            [](int, int, int) { return LightSample{15, 0}; });
        if (mesh.vertices.size() != static_cast<size_t>(cubes*24+plants*8) ||
            mesh.indices.size() != static_cast<size_t>(cubes*72+plants*48) ||
            mesh.opaqueIndexCount != static_cast<size_t>(cubes*36+plants*24) || mesh.translucentIndexCount != 0 ||
            mesh.shadowCasterIndexOffset != mesh.opaqueIndexCount ||
            mesh.shadowCasterIndexCount != static_cast<size_t>(cubes*36+plants*24) || mesh.indexCount != mesh.indices.size())
            throw std::runtime_error("Decoration cube geometry/layer handoff mismatch");
        for (uint16_t raw = first; raw < first+count; ++raw) {
            const float tile = getFaceTextureIndex(static_cast<BlockId>(raw), FaceDir::TOP);
            bool found = false;
            for (const auto& vertex : mesh.vertices) if (std::floor(vertex.tile) == tile) found = true;
            if (!found) throw std::runtime_error("Decoration material missing from CPU mesh");
        }
        renderer.uploadChunkMesh(mesh);
        const glm::vec3 camera(7.5f, 70.0f, 0.0f);
        const auto view = glm::lookAt(camera, glm::vec3(7.5f, 69.5f, 8.5f), glm::vec3(0,1,0));
        const auto vp = glm::perspective(glm::radians(70.0f), window.aspectRatio(),
                                         0.1f, 128.0f) * view;
        const RenderEnvironment environment = DayNightCycle{}.evaluate();
        for (int frame = 0; frame < frames; ++frame) {
            renderer.beginFrame();
            renderer.setEnvironment(environment, camera);
            renderer.setViewProjection(vp);
            renderer.renderSky(environment, glm::inverse(vp), camera, false);
            renderer.renderChunk(mesh, glm::mat4(1), vp);
            PostProcessState post;
            post.environment = environment;
            post.cameraPosition = camera;
            post.inverseViewProjection = glm::inverse(vp);
            renderer.finishScene(post);
            renderer.endFrame();
        }
        renderer.waitIdle();
        renderer.releaseChunkMesh(mesh);
        std::cout << "Decoration smoke: " << count << " materials, " << frames << " Vulkan frames, geometry/layer/upload handoff passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Decoration smoke failed: " << error.what() << '\n';
        return 1;
    }
}
