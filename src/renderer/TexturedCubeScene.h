#pragma once

#include "renderer/Camera.h"
#include "renderer/RenderDevice.h"

#include <chrono>
#include <filesystem>

class TexturedCubeScene {
public:
    TexturedCubeScene(IRenderDevice& renderer,
                      const std::filesystem::path& assetRoot);
    ~TexturedCubeScene();

    TexturedCubeScene(const TexturedCubeScene&) = delete;
    TexturedCubeScene& operator=(const TexturedCubeScene&) = delete;

    void render(float aspectRatio);

private:
    IRenderDevice& m_renderer;
    Camera m_camera{55.0f, 0.1f, 20.0f};
    RenderMeshHandle m_mesh{};
    RenderTextureHandle m_texture{};
    RenderMaterialHandle m_material{};
    std::chrono::steady_clock::time_point m_started;
};
