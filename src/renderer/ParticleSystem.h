#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "game/Weather.h"
#include "world/Block.h"

class World;

enum class ParticleKind : uint8_t {
    Rain, Snow, Lightning, BlockDebris, RainSplash
};

struct ParticleRenderData {
    glm::vec3 position{0.0f};
    float kind = 0.0f;
    float phase = 0.0f;
    float texture = 0.0f;
    float size = 1.0f;
    float rotation = 0.0f;
};

class ParticleSystem {
public:
    static constexpr size_t MAX_PARTICLES = 2048;

    void clear();
    void update(World& world, const glm::dvec3& viewer, float dt,
                float rainIntensity, uint64_t seed);
    void emitBlockBreak(const glm::ivec3& position, BlockId block);
    void emitExplosion(const glm::dvec3& position);
    void appendLightning(const glm::dvec3& position);
    std::vector<ParticleRenderData> buildRenderData(
        const glm::dvec3& renderOrigin) const;
    void buildRenderData(const glm::dvec3& renderOrigin,
                         std::vector<ParticleRenderData>& output) const;

    size_t size() const { return m_particles.size(); }

private:
    struct Particle {
        ParticleKind kind = ParticleKind::Rain;
        glm::dvec3 position{0.0};
        glm::vec3 velocity{0.0f};
        float age = 0.0f;
        float lifetime = 1.0f;
        float phase = 0.0f;
        float texture = 0.0f;
        float size = 1.0f;
        float rotation = 0.0f;
        float angularVelocity = 0.0f;
    };

    std::vector<Particle> m_particles;
    uint64_t m_randomState = 1;
    float m_weatherEmission = 0.0f;

    uint64_t randomBits();
    float randomFloat();
    void emitWeatherParticle(World& world, const glm::dvec3& viewer,
                             uint64_t seed);
};
