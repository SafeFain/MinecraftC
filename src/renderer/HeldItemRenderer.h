#pragma once

#include "game/Item.h"
#include "game/FishingSystem.h"
#include "renderer/RenderHandles.h"
#include "renderer/HeldToolModel.h"
#include "world/BlockLightLogic.h"

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
    glm::vec3 firstPersonFishingTip(float swing, float attackStrength, float aspect,
        const glm::mat4& movement, const glm::mat4& worldViewProjection) const;
    static glm::vec3 thirdPersonFishingTip(const glm::mat4& hand);
    void renderFishing(const FishingView& fishing, const glm::dvec3& renderOrigin,
                       const glm::vec3& tip, const glm::mat4& viewProjection);
    void renderThirdPerson(const ItemStack& item, const glm::mat4& viewProjection,
                           const glm::mat4& handTransform,
                           const ItemStack& offhand, const glm::mat4& leftHandTransform,
                           const HeldItemUseState* useState = nullptr);
    void renderDropped(const ItemStack& item, const glm::mat4& viewProjection,
                       const glm::vec3& position, float ageSeconds, uint32_t phaseSeed,
                       SmoothLightSample light);

private:
    struct CachedMesh {
        RenderMeshHandle handle{};
        bool blockAtlas = false;
        bool toolModel = false;
        std::vector<HeldToolRange> ranges;
        glm::mat4 droppedTransform{1.0f};
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
    RenderMeshHandle m_fishingCube{};
    RenderTextureHandle m_fishingTexture{};
    RenderMaterialHandle m_fishingMaterial{};
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
