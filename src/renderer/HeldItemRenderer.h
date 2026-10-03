#pragma once

#include "game/Item.h"
#include "renderer/RenderHandles.h"
#include "renderer/HeldToolModel.h"

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

class IGameRenderer;

class HeldItemRenderer {
public:
    HeldItemRenderer() = default;
    ~HeldItemRenderer();
    HeldItemRenderer(const HeldItemRenderer&) = delete;
    HeldItemRenderer& operator=(const HeldItemRenderer&) = delete;

    void initialize(IGameRenderer& renderer,
                    const std::filesystem::path& assetRoot);
    void reset();
    void updateUseState(bool bowCharging, float charge, bool blocking, float dt);
    void renderFirstPerson(const ItemStack& item, const ItemStack& offhand, float swingProgress,
                           float attackStrength,
                           float aspectRatio, const glm::mat4& movementTransform);
    void renderThirdPerson(const ItemStack& item, const glm::mat4& viewProjection,
                           const glm::mat4& handTransform,
                           const ItemStack& offhand, const glm::mat4& leftHandTransform);

private:
    struct CachedMesh {
        RenderMeshHandle handle{};
        bool blockAtlas = false;
        bool toolModel = false;
        std::vector<HeldToolRange> ranges;
    };
    IGameRenderer* m_renderer = nullptr;
    RenderTextureHandle m_itemTexture{};
    RenderTextureHandle m_armTexture{};
    RenderTextureHandle m_toolTexture{};
    RenderMaterialHandle m_toolMaterial{};
    HeldItemUseState m_use;
    RenderMaterialHandle m_itemMaterial{};
    RenderMaterialHandle m_blockMaterial{};
    RenderMaterialHandle m_armMaterial{};
    RenderMeshHandle m_armMesh{};
    int m_itemColumns = 0;
    int m_itemRows = 0;
    int m_blockTiles = 1;
    std::unordered_map<std::string, int> m_itemIndices;
    std::vector<uint8_t> m_itemPixels;
    uint32_t m_itemWidth = 0;
    uint32_t m_itemHeight = 0;
    std::unordered_map<uint16_t, CachedMesh> m_meshes;

    CachedMesh meshFor(ItemId item);
    void drawItem(const ItemStack& item, const glm::mat4& vp,
                  const glm::mat4& transform, bool firstPerson);
};
