#pragma once

#include "entity/EntityLogic.h"
#include "model/GltfLoader.h"
#include "model/AnimationMixer.h"
#include "model/ModelRenderer.h"
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
    model::ModelHandle handle{};
    bool usesPlaceholder = false;
};

class EntityModelRegistry {
public:
    using Loader = std::function<model::LoadResult(const std::filesystem::path&)>;
    explicit EntityModelRegistry(Loader loader = [](const std::filesystem::path& path) { return model::loadGltf(path); });

    void loadAll(const std::filesystem::path& assetRoot, model::ModelAssetCache* cache = nullptr);
    void clearInstances();
    void shareAssets(const EntityModelRegistry& source) {
        clearInstances();m_definitions=source.m_definitions;m_cache=source.m_cache;m_placeholder=source.m_placeholder;
    }
    void uploadAll(model::ModelRenderer& renderer);
    const EntityModelDefinition& definition(EntityType type) const;
    bool hasModel(EntityType type) const;
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
               SmoothLightSample light = {}, bool sleeping = false, float scale = 1.0f);
    void endFrame();
    void retainInstances(const std::vector<uint64_t>& live);
    void measurePerformance(bool enabled) { m_measure=enabled; }
    double poseCpuMs() const { return m_poseCpuMs; }
    double queueCpuMs() const { return m_queueCpuMs; }

private:
    Loader m_loader;
    std::vector<EntityModelDefinition> m_definitions;
    model::ModelAssetCache m_cache;
    std::shared_ptr<const model::ModelAsset> m_placeholder;
    struct InstanceEntry {
        model::ModelInstance instance;
        model::AnimationMixer mixer;
        EntityType type = EntityType::Cow;
        bool initialized = false;
        uint64_t retentionEpoch=0;
    };
    std::map<uint64_t, InstanceEntry> m_instances;
    uint64_t m_retentionEpoch=0;
    bool m_measure=false;
    double m_poseCpuMs=0, m_queueCpuMs=0;
    InstanceEntry& ensure(EntityType type, uint64_t id);
};
