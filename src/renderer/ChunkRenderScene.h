#pragma once

#include "renderer/Camera.h"
#include "renderer/GameRenderer.h"
#include "world/ChunkMesh.h"

#include <filesystem>
#include <functional>
#include <vector>

class ChunkRenderScene {
public:
    ChunkRenderScene(IGameRenderer& renderer,
                     const std::filesystem::path& assetRoot,
                     int benchmarkGridRadius = 0);
    ~ChunkRenderScene();
    ChunkRenderScene(const ChunkRenderScene&) = delete;
    ChunkRenderScene& operator=(const ChunkRenderScene&) = delete;
    using ExtraPass = std::function<void(const glm::mat4& viewProjection)>;
    void render(float aspectRatio, const ExtraPass& extraPass = {});
    float groundHeight() const { return m_groundHeight; }

private:
    IGameRenderer& m_renderer;
    Camera m_camera{62.0f, 0.1f, 256.0f};
    float m_groundHeight = 0.0f;
    ChunkMesh m_mesh;
    std::vector<glm::mat4> m_instances;
};
