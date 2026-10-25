#include "entity/EntityPresenter.h"

#include "entity/EntityManager.h"
#include "renderer/GameRenderer.h"
#include "world/World.h"

#include <cmath>
#include <string>

namespace {

glm::vec3 renderColor(EntityType type) {
    switch (type) {
        case EntityType::Item: return {0.95f, 0.78f, 0.22f};
        case EntityType::Cow: return {0.34f, 0.20f, 0.12f};
        case EntityType::Pig: return {0.92f, 0.52f, 0.58f};
        case EntityType::Sheep: return {0.88f, 0.88f, 0.82f};
        case EntityType::Chicken: return {0.92f, 0.90f, 0.72f};
        case EntityType::Zombie: return {0.20f, 0.48f, 0.28f};
        case EntityType::Skeleton: return {0.72f, 0.72f, 0.68f};
        case EntityType::Spider: return {0.16f, 0.08f, 0.07f};
        case EntityType::Blastling: return {0.35f, 0.72f, 0.30f};
        case EntityType::Villager: return {0.56f, 0.36f, 0.22f};
        case EntityType::ZombieVillager: return {0.27f, 0.48f, 0.29f};
        case EntityType::IronGolem: return {.78f, .79f, .70f};
        case EntityType::Arrow: return {0.58f, 0.42f, 0.20f};
        case EntityType::PrimedTnt: return {0.86f, 0.18f, 0.12f};
    }
    return {1.0f, 0.0f, 1.0f};
}

int compatibilityTexture(EntityType type) {
    switch (type) {
        case EntityType::Cow: return 0;
        case EntityType::Pig: return 1;
        case EntityType::Sheep: return 2;
        case EntityType::Chicken: return 3;
        case EntityType::Zombie: return 4;
        case EntityType::Skeleton: return 5;
        case EntityType::Spider: return 6;
        case EntityType::Blastling: return 7;
        case EntityType::Villager: return 9;
        case EntityType::ZombieVillager: return 10;
        case EntityType::IronGolem: return 11;
        case EntityType::Item:
        case EntityType::Arrow:
        case EntityType::PrimedTnt: return 8;
    }
    return 8;
}

} // namespace

void EntityPresenter::initializeModels(const std::filesystem::path& assetRoot,
                                       IGameRenderer& renderer) {
    m_modelRegistry.loadAll(assetRoot, renderer.capabilities().gameplay
        ? &renderer.modelRenderer().assets() : nullptr);
    if (renderer.capabilities().gameplay)
        m_modelRegistry.uploadAll(renderer.modelRenderer());
}

void EntityPresenter::shareAssetsFrom(const EntityPresenter& source) {
    m_modelRegistry.shareAssets(source.m_modelRegistry);
}

void EntityPresenter::render(IGameRenderer& renderer,
                             const glm::mat4& viewProjection,
                             const glm::dvec3& renderOrigin,
                             const EntityManager& entities,
                             const World& world) const {
    m_modelRegistry.beginFrame();
    for (const auto& entity : entities.entities()) {
        // Item geometry/materials are drawn by the scene presenter's shared
        // item cache.
        if (entity.type == EntityType::Item) continue;
        glm::vec3 color = renderColor(entity.type);
        glm::vec3 visualTint(1.0f);
        if (entity.type == EntityType::PrimedTnt) {
            const float flash = std::fmod(entity.ageSeconds, 0.25f) < 0.10f
                ? 1.0f : 0.0f;
            color = glm::mix(glm::vec3(0.86f, 0.18f, 0.12f),
                             glm::vec3(1.0f), flash);
        } else if (entity.hurtFlashSeconds > 0.0f) {
            color = {1.0f, 0.16f, 0.16f};
            visualTint = color;
        } else if (entity.burningSeconds > 0.0f) {
            const float pulse = 0.08f * std::sin(entity.ageSeconds * 18.0f);
            color = {1.0f, 0.32f + pulse, 0.08f};
            visualTint = color;
        }
        const glm::vec3 position(
            glm::dvec3(entity.position) - renderOrigin);
        const glm::vec3 size = EntityManager::entitySize(entity);
        const SmoothLightSample light = world.sampleLight(
            entity.position + glm::dvec3(0.0, size.y * 0.5, 0.0));
        if (m_modelRegistry.hasModel(entity.type) &&
            renderer.capabilities().gameplay) {
            m_modelRegistry.queue(entity.type, entity.id, entity.position,
                entity.facing, entity.behaviorSeed,
                renderOrigin, glm::vec3(0.0f), renderer.modelRenderer(),
                visualTint * (entity.type == EntityType::Villager ?
                    glm::mix(glm::vec3(1),
                        glm::vec3(.65f + .025f * static_cast<int>(
                            entity.villager.profession), .85f, .72f), .5f) :
                    glm::vec3(1)),
                light, entity.sleeping,
                (entity.type == EntityType::Villager ||
                 entity.type == EntityType::ZombieVillager) &&
                    !entity.villager.adult() ? .5f : 1.0f);
            continue;
        }
        const float facingLength = std::hypot(entity.facing.x, entity.facing.z);
        const float yaw = facingLength > 0.001f
            ? std::atan2(-entity.facing.x, -entity.facing.z)
            : static_cast<float>(entity.behaviorSeed % 628u) * 0.01f;
        renderer.renderCompatibilityEntityCube(
            position, size, color, compatibilityTexture(entity.type), yaw,
            viewProjection, light);
    }
    for (const auto& dead : entities.deadEntityRenders()) {
        if (!m_modelRegistry.hasModel(dead.type) ||
            !renderer.capabilities().gameplay) continue;
        m_modelRegistry.queue(dead.type, dead.id, dead.position,
            dead.facing, dead.behaviorSeed, renderOrigin,
            glm::vec3(0.0f), renderer.modelRenderer(),
            glm::vec3(1.0f),
            world.sampleLight(dead.position + glm::dvec3(0.0, 0.8, 0.0)));
    }
    m_modelRegistry.endFrame();
    std::vector<uint64_t> liveModels;
    liveModels.reserve(entities.entities().size() +
                       entities.deadEntityRenders().size());
    for (const auto& entity : entities.entities()) liveModels.push_back(entity.id);
    for (const auto& entity : entities.deadEntityRenders())
        liveModels.push_back(entity.id);
    m_modelRegistry.retainInstances(liveModels);
    renderer.flushModels(viewProjection);
}

bool EntityPresenter::hasVisual(EntityType type) const {
    return m_modelRegistry.hasModel(type);
}

void EntityPresenter::clearInstances() { m_modelRegistry.clearInstances(); }

bool EntityPresenter::play(EntityType type, uint64_t id,
                           std::string_view action) {
    return m_modelRegistry.playAction(type, id, std::string(action));
}

bool EntityPresenter::playing(EntityType type, uint64_t id,
                              std::string_view action) const {
    return m_modelRegistry.playing(type, id, std::string(action));
}

void EntityPresenter::setLocomotion(EntityType type, uint64_t id, float speed) {
    m_modelRegistry.setLocomotion(type, id, speed);
}

std::vector<EntityPresentationEvent> EntityPresenter::advance(
    EntityType type, uint64_t id, float dt) {
    const auto fired = m_modelRegistry.advance(type, id, dt);
    std::vector<EntityPresentationEvent> events;
    events.reserve(fired.size());
    for (const auto& event : fired)
        events.push_back({event.action, event.name});
    return events;
}
