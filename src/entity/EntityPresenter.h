#pragma once

#include "entity/EntityModelRegistry.h"
#include "entity/EntityPresentation.h"

#include <filesystem>
#include <string_view>

#include <glm/glm.hpp>

class EntityManager;
class IGameRenderer;
class World;

// Owns all entity model assets, animation instances and per-frame model
// drawing. EntityManager only knows this through IEntityPresentation.
class EntityPresenter final : public IEntityPresentation {
public:
    void initializeModels(const std::filesystem::path& assetRoot,
                          IGameRenderer& renderer);
    // Reuses the loaded definitions, GPU handles and immutable animation graphs
    // of another dimension. Instances stay per-dimension.
    void shareAssetsFrom(const EntityPresenter& source);
    void render(IGameRenderer& renderer, const glm::mat4& viewProjection,
                const glm::dvec3& renderOrigin, const EntityManager& entities,
                const World& world) const;

    bool hasVisual(EntityType type) const override;
    void clearInstances() override;
    bool play(EntityType type, uint64_t id, std::string_view action) override;
    EntityAttackTiming attackTiming(EntityType type) const override;
    void setLocomotion(EntityType type, uint64_t id, float speed) override;
    void advance(EntityType type, uint64_t id, float dt) override;

private:
    // Drawing and animation queries run from const session views.
    mutable EntityModelRegistry m_modelRegistry;
};
