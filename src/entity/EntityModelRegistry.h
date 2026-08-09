#pragma once

#include "entity/EntityLogic.h"
#include "model/GltfLoader.h"
#include "model/AnimationMixer.h"
#include "world/BlockLightLogic.h"

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

namespace model { class ModelRenderer; }

struct EntityModelDefinition {
    EntityType type = EntityType::Cow;
    std::filesystem::path filename;
    std::shared_ptr<const model::ModelAsset> asset;
    std::shared_ptr<const model::AnimationGraphAsset> graph;
    uint32_t handle = 0;
    bool usesPlaceholder = false;
};

class EntityModelRegistry {
public:
    using Loader = std::function<model::LoadResult(const std::filesystem::path&)>;
    explicit EntityModelRegistry(Loader loader = model::loadGltf);

    void loadAll(const std::filesystem::path& assetRoot);
    void clearInstances();
    void uploadAll(model::ModelRenderer& renderer);
    const EntityModelDefinition& definition(EntityType type) const;
    void setLocomotion(EntityType type, uint64_t id, float speed);
    bool playAction(EntityType type, uint64_t id, const std::string& semantic,
                    model::PlayPolicy policy = model::PlayPolicy::Replace);
    std::vector<model::FiredAnimationEvent> advance(EntityType type, uint64_t id,
                                                     float dt);
    bool playing(EntityType type, uint64_t id, const std::string& semantic);
    void beginFrame();
    void queue(EntityType type, uint64_t id, const glm::dvec3& position,
               const glm::vec3& facing, uint32_t behaviorSeed,
               const glm::dvec3& renderOrigin, const glm::vec3& cameraPosition,
               model::ModelRenderer& renderer, const glm::vec3& visualTint,
               SmoothLightSample light = {});
    void endFrame();

private:
    Loader m_loader;
    std::array<EntityModelDefinition, 8> m_definitions;
    std::map<std::filesystem::path, std::shared_ptr<const model::ModelAsset>> m_cache;
    std::shared_ptr<const model::ModelAsset> m_placeholder;
    struct InstanceEntry {
        model::ModelInstance instance;
        model::AnimationMixer mixer;
        EntityType type = EntityType::Cow;
        bool initialized = false;
    };
    std::map<uint64_t, InstanceEntry> m_instances;
    std::unordered_set<uint64_t> m_seen;
    InstanceEntry& ensure(EntityType type, uint64_t id);
};
