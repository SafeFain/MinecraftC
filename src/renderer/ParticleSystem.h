#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "game/Weather.h"
#include "game/GameRules.h"
#include "world/Block.h"
#include "world/Biome.h"
#include "renderer/VisualQuality.h"

class World;

enum class ParticleKind : uint8_t {
    Rain, Snow, Lightning, BlockDebris, RainSplash, Trajectory, SkyMote,
    HeavenPollen, HeavenSparkle, CriticalHit, SweepAttack, OverworldMote,
    Firefly
};

inline bool supportsFireflies(Biome biome) {
    return biome == Biome::SWAMP || biome == Biome::FOREST ||
        biome == Biome::BIRCH_FOREST || biome == Biome::FLOWER_FOREST ||
        biome == Biome::JUNGLE || biome == Biome::KARST_FOREST ||
        biome == Biome::LUSH_VALLEY;
}

enum class OverworldAmbientKind : uint8_t { None, Mote, Firefly };

inline OverworldAmbientKind selectOverworldAmbient(
    DimensionId dimension, Biome biome, bool vegetated, bool outdoors,
    float daylight, float rainIntensity, float particlesPerSecond) {
    if (dimension != DimensionId::Overworld || particlesPerSecond <= 0.0f ||
        !outdoors || rainIntensity > 0.01f) {
        return OverworldAmbientKind::None;
    }
    if (daylight >= 0.35f && vegetated)
        return OverworldAmbientKind::Mote;
    if (daylight <= 0.25f && supportsFireflies(biome))
        return OverworldAmbientKind::Firefly;
    return OverworldAmbientKind::None;
}

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
    static constexpr size_t MAX_SKY_MOTES_PER_UPDATE = 8;
    static constexpr size_t MAX_POLLEN_PER_UPDATE = 4;
    static constexpr size_t MAX_SPARKLE_PER_UPDATE = 6;

    void setEnhancedVisuals(bool enabled, VisualQuality quality);

    void clear();
    void update(World& world, const glm::dvec3& viewer, float dt,
                float rainIntensity, uint64_t seed,
                DimensionId dimension = DimensionId::Overworld,
                float daylight = 1.0f);
    void emitBlockBreak(const glm::ivec3& position, BlockId block);
    void emitExplosion(const glm::dvec3& position);
    void emitCriticalHit(const glm::dvec3& position);
    void emitSweepAttack(const glm::dvec3& position);
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
    float m_skyMoteEmission = 0.0f;
    float m_pollenEmission = 0.0f;
    float m_sparkleEmission = 0.0f;
    float m_overworldAmbientEmission = 0.0f;
    float m_skyDaylight = 1.0f;
    EnhancedVisualConfig m_enhancedVisual{};

    uint64_t randomBits();
    float randomFloat();
    void emitWeatherParticle(World& world, const glm::dvec3& viewer,
                             uint64_t seed);
    void emitSkyMote(const glm::dvec3& viewer, uint64_t seed,
                     float daylight);
    void emitHeavenPollen(const glm::dvec3& viewer, uint64_t seed,
                          int paletteIndex);
    void emitHeavenSparkle(const glm::dvec3& viewer, uint64_t seed,
                              int paletteIndex);
    void emitOverworldAmbient(World& world, const glm::dvec3& viewer,
                              uint64_t seed, bool firefly);
};
