#pragma once

#include "entity/EntityLogic.h"
#include "model/GltfLoader.h"
#include "model/ModelAnimation.h"

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
    uint32_t handle = 0;
    bool usesPlaceholder = false;
};

class EntityModelRegistry {
public:
    using Loader = std::function<model::LoadResult(const std::filesystem::path&)>;
    explicit EntityModelRegistry(Loader loader = model::loadGltf);

    void loadAll(const std::filesystem::path& assetRoot);
    void uploadAll(model::ModelRenderer& renderer);
    const EntityModelDefinition& definition(EntityType type) const;
    std::string clipFor(EntityType type, EntityPlayback playback) const;
    void beginFrame();
    void queue(EntityType type, uint64_t id, const glm::dvec3& position,
               const glm::vec3& velocity, uint32_t behaviorSeed,
               bool hurt, bool dead, float sourceTime,
               const glm::dvec3& renderOrigin, const glm::vec3& cameraPosition,
               model::ModelRenderer& renderer);
    void endFrame();

private:
    Loader m_loader;
    std::array<EntityModelDefinition, 8> m_definitions;
    std::map<std::filesystem::path, std::shared_ptr<const model::ModelAsset>> m_cache;
    std::shared_ptr<const model::ModelAsset> m_placeholder;
    struct InstanceEntry {
        model::ModelInstance instance;
        EntityPlayback playback = EntityPlayback::Idle;
        EntityPlayback previousPlayback = EntityPlayback::Idle;
        float lastSourceTime = 0.0f;
    };
    std::map<uint64_t, InstanceEntry> m_instances;
    std::unordered_set<uint64_t> m_seen;
};
