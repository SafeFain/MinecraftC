#pragma once

#include "game/Item.h"
#include "model/AnimationMixer.h"
#include "model/GltfLoader.h"
#include "player/PlayerVisual.h"
#include "world/BlockLightLogic.h"

#include <filesystem>
#include <memory>

class IGameRenderer;

struct PlayerHandTransforms {
    glm::mat4 right{1.0f};
    glm::mat4 left{1.0f};
};

class PlayerRenderer {
public:
    void initialize(const std::filesystem::path& assetRoot,
                    IGameRenderer& renderer);
    void shareAssets(const PlayerRenderer& source);
    void update(const PlayerVisualState& state, float dt);
    PlayerHandTransforms renderThirdPerson(IGameRenderer& renderer,
                                const glm::dvec3& playerPosition,
                                const glm::dvec3& renderOrigin,
                                float yawDegrees, float pitchDegrees,
                                const glm::mat4& viewProjection,
                                SmoothLightSample light,
                                glm::vec3 sleepingFacing = glm::vec3(0.0f));

private:
    std::shared_ptr<const model::ModelAsset> m_asset;
    std::shared_ptr<const model::AnimationGraphAsset> m_graph;
    model::AnimationMixer m_mixer;
    model::ModelInstance m_instance;
    uint32_t m_handle = 0;
    uint32_t m_lastSwingSequence = 0;
    std::string m_locomotion = "idle";
    int m_headNode = -1;
    int m_rightArmNode = -1;
    int m_leftArmNode = -1;
    bool m_bowCharging = false;
    float m_bowCharge = 0.0f;
    float m_shieldRaise = 0.0f;
    bool m_sleeping = false;
    bool m_prone = false;
};
