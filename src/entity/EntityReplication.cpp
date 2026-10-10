#include "entity/EntityManager.h"
#include "entity/EntitySnapshot.h"
#include <algorithm>
#include <cmath>

void EntityManager::applyReplicaSnapshots(const std::vector<EntitySnapshot>& snapshots,
                                         const std::vector<EntityDeathSnapshot>& deaths) {
    std::map<uint64_t, Entity> old;
    for (auto& entity : m_entities) old.emplace(entity.id, std::move(entity));
    m_entities.clear(); m_replicaTargets.clear();
    for (const auto& snapshot : snapshots) {
        Entity entity;
        const auto previous = old.find(snapshot.id);
        const bool retained = previous != old.end() && previous->second.type == snapshot.type;
        if (retained) entity = std::move(previous->second);
        entity.id = snapshot.id; entity.type = snapshot.type;
        if (!retained || glm::distance(entity.position, snapshot.position) > 4) entity.position = snapshot.position;
        m_replicaTargets.emplace(entity.id, std::make_pair(snapshot.position, snapshot.facing));
        entity.velocity = snapshot.velocity; entity.locomotionVelocity = snapshot.locomotion;
        if (!retained) entity.facing = snapshot.facing;
        entity.health = snapshot.health; entity.ageSeconds = snapshot.age;
        if (m_modelRegistry.hasModel(entity.type)) {
            if (snapshot.hurt > 0 && entity.hurtFlashSeconds <= 0) m_modelRegistry.playAction(entity.type, entity.id, "hurt");
            if (snapshot.attacking && !entity.attackPending) m_modelRegistry.playAction(entity.type, entity.id, "attack");
        }
        entity.hurtFlashSeconds = snapshot.hurt; entity.burningSeconds = snapshot.burning;
        entity.item = snapshot.item; entity.behaviorSeed = snapshot.seed;
        entity.inGround = snapshot.inGround; entity.playerOwned = snapshot.playerOwned; entity.sleeping = snapshot.sleeping;
        entity.attackPending = snapshot.attacking; entity.villager = snapshot.villager;
        m_entities.push_back(std::move(entity));
    }
    std::set<uint64_t> previousDeaths;
    for (const auto& dead : m_deadEntityRenders) previousDeaths.insert(dead.id);
    m_deadEntityRenders.clear();
    for (const auto& death : deaths) {
        if (!m_modelRegistry.hasModel(death.type)) continue;
        m_deadEntityRenders.push_back({death.id, death.type, death.position, death.velocity, death.facing, death.seed, death.elapsed});
        if (!previousDeaths.count(death.id)) m_modelRegistry.playAction(death.type, death.id, "death");
    }
}
void EntityManager::advanceReplicaPresentation(float dt) {
    const double blend = 1 - std::exp(-std::max(0.0f, dt) / .05);
    for (auto& entity : m_entities) {
        const auto target = m_replicaTargets.find(entity.id);
        if (target == m_replicaTargets.end()) continue;
        entity.position = glm::mix(entity.position, target->second.first, blend);
        const auto facing = glm::mix(entity.facing, target->second.second, static_cast<float>(blend));
        if (glm::length(facing) > .001f) entity.facing = glm::normalize(facing);
        entity.hurtFlashSeconds = std::max(0.0f, entity.hurtFlashSeconds - dt);
        entity.ageSeconds += std::max(0.0f, dt);
        if (m_modelRegistry.hasModel(entity.type)) {
            m_modelRegistry.setLocomotion(entity.type, entity.id, std::hypot(entity.locomotionVelocity.x, entity.locomotionVelocity.z));
            (void)m_modelRegistry.advance(entity.type, entity.id, dt);
        }
    }
    for (auto& dead : m_deadEntityRenders) {
        dead.elapsed = advanceDeathPresentation(dead.elapsed, dt);
        (void)m_modelRegistry.advance(dead.type, dead.id, dt);
    }
    m_deadEntityRenders.erase(std::remove_if(m_deadEntityRenders.begin(), m_deadEntityRenders.end(),
        [](const DeadEntityRender& dead) { return !deathPresentationVisible(dead.elapsed); }), m_deadEntityRenders.end());
}
