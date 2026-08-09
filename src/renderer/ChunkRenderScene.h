#pragma once

#include "renderer/Camera.h"
#include "renderer/RenderDevice.h"

#include <filesystem>

class ChunkRenderScene {
public:
    ChunkRenderScene(IRenderDevice& renderer,
                     const std::filesystem::path& assetRoot);
    ~ChunkRenderScene();
    ChunkRenderScene(const ChunkRenderScene&) = delete;
    ChunkRenderScene& operator=(const ChunkRenderScene&) = delete;
    void render(float aspectRatio);

private:
    IRenderDevice& m_renderer;
    Camera m_camera{62.0f, 0.1f, 256.0f};
    RenderMeshHandle m_mesh{};
    RenderTextureHandle m_texture{};
    RenderMaterialHandle m_material{};
    RenderMaterialHandle m_translucentMaterial{};
    uint32_t m_opaqueIndexCount = 0;
    uint32_t m_translucentIndexOffset = 0;
    uint32_t m_translucentIndexCount = 0;
};
