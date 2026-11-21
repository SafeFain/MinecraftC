#pragma once

#include <filesystem>
#include <memory>

#include "renderer/RenderHandles.h"
#include "renderer/UIRenderBackend.h"

class IGameRenderer;

// The block atlas is owned by the renderer. Destroy the backend before
// reinitializing or destroying the renderer.
std::unique_ptr<IUIRenderBackend> createVulkanUIBackend(
    IGameRenderer& renderer, RenderTextureHandle blockAtlasTexture,
    const std::filesystem::path& assetRoot);
