#include "core/Window.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "world/ChunkMesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: vulkan_decoration_smoke <asset-directory>\n";
        return 2;
    }
    try {
        Window window(960, 540, "MinecraftC crafted decoration smoke",
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
        for (int i = 0; i < 18; ++i) {
            const int x = 2 + (i % 6) * 2;
            const int y = 66 + (2 - i / 6) * 2;
            const int z = 8;
            blocks[x + z * 16 + Config::worldYToStorageY(y) * 256] =
                static_cast<uint8_t>(183 + i);
            maxima[x][z] = y;
        }
        ChunkMesh mesh;
        mesh.build(0, 0, blocks.data(), maxima,
            [](int, int, int) { return BlockId::AIR; },
            [](int, int, int) { return LightSample{15, 0}; });
        if (mesh.vertices.size() != 18 * 24 || mesh.indices.size() != 18 * 72 ||
            mesh.opaqueIndexCount != 18 * 36 || mesh.translucentIndexCount != 0 ||
            mesh.shadowCasterIndexOffset != mesh.opaqueIndexCount ||
            mesh.shadowCasterIndexCount != 18 * 36 || mesh.indexCount != mesh.indices.size())
            throw std::runtime_error("Decoration cube geometry/layer handoff mismatch");
        for (uint16_t raw = 183; raw <= 200; ++raw) {
            const float tile = getFaceTextureIndex(static_cast<BlockId>(raw), FaceDir::TOP);
            bool found = false;
            for (const auto& vertex : mesh.vertices) if (std::floor(vertex.tile) == tile) found = true;
            if (!found) throw std::runtime_error("Decoration material missing from CPU mesh");
        }
        renderer.uploadChunkMesh(mesh);
        const glm::vec3 camera(7.5f, 70.0f, 0.0f);
        const auto view = glm::lookAt(camera, glm::vec3(7.5f, 68.5f, 8.5f), glm::vec3(0,1,0));
        const auto vp = glm::perspective(glm::radians(70.0f), window.aspectRatio(),
                                         0.1f, 128.0f) * view;
        const RenderEnvironment environment = DayNightCycle{}.evaluate();
        for (int frame = 0; frame < 48; ++frame) {
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
        std::cout << "Decoration smoke: 18 materials, 48 Vulkan frames, cube/layer/upload handoff passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Decoration smoke failed: " << error.what() << '\n';
        return 1;
    }
}
