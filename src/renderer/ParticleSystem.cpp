#include "renderer/ParticleSystem.h"

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "world/BiomeMap.h"
#include "world/World.h"

void ParticleSystem::clear() {
    m_particles.clear();
    m_weatherEmission = 0.0f;
    m_skyMoteEmission = 0.0f;
    m_pollenEmission = 0.0f;
    m_sparkleEmission = 0.0f;
    m_overworldAmbientEmission = 0.0f;
    m_skyDaylight = 1.0f;
}

void ParticleSystem::setEnhancedVisuals(bool enabled, VisualQuality quality) {
    m_enhancedVisual = enhancedVisualConfig(quality, enabled);
    if (m_enhancedVisual.ambientParticlesPerSecond > 0.0f) return;
    m_overworldAmbientEmission = 0.0f;
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
        [](const Particle& particle) {
            return particle.kind == ParticleKind::OverworldMote ||
                   particle.kind == ParticleKind::Firefly;
        }), m_particles.end());
}

uint64_t ParticleSystem::randomBits() {
    m_randomState += 0x9e3779b97f4a7c15ULL;
    uint64_t value = m_randomState;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

float ParticleSystem::randomFloat() {
    return static_cast<float>(randomBits() >> 40) / 16777216.0f;
}

void ParticleSystem::emitWeatherParticle(World& world,
                                         const glm::dvec3& viewer,
                                         uint64_t seed) {
    m_randomState ^= seed;
    const double x = viewer.x + (randomFloat() * 2.0 - 1.0) * 13.0;
    const double z = viewer.z + (randomFloat() * 2.0 - 1.0) * 13.0;
    const int bx = static_cast<int>(std::floor(x));
    const int bz = static_cast<int>(std::floor(z));
    const int surface = world.getSurfaceY(bx, bz);
    if (!Config::isValidWorldY(surface)) return;
    const auto precipitation = world.precipitationAt(bx, surface + 1, bz);
    if (precipitation == PrecipitationType::None) return;

    Particle particle;
    particle.kind = precipitation == PrecipitationType::Rain
        ? ParticleKind::Rain : ParticleKind::Snow;
    particle.position = {x, std::min<double>(viewer.y + 16.0,
                                             Config::WORLD_MAX_Y - 0.25), z};
    particle.velocity = particle.kind == ParticleKind::Rain
        ? glm::vec3((randomFloat() - 0.5f) * 0.35f, -15.0f,
                    (randomFloat() - 0.5f) * 0.35f)
        : glm::vec3((randomFloat() - 0.5f) * 0.8f, -2.2f,
                    (randomFloat() - 0.5f) * 0.8f);
    particle.lifetime = particle.kind == ParticleKind::Rain ? 1.6f : 7.0f;
    particle.phase = randomFloat();
    if (particle.kind == ParticleKind::Rain) {
        particle.texture = static_cast<float>(
            getAtlasTextureIndex(BlockTexture::Water));
        particle.size = 0.10f + randomFloat() * 0.09f;
        particle.rotation = randomFloat() * 6.2831853f;
        particle.angularVelocity = (randomFloat() - 0.5f) * 7.0f;
    }
    m_particles.push_back(particle);
}

void ParticleSystem::emitSkyMote(const glm::dvec3& viewer, uint64_t seed,
                                 float daylight) {
    m_randomState ^= seed + 0xA0761D6478BD642FULL;
    Particle particle;
    particle.kind = ParticleKind::SkyMote;
    particle.position = {
        viewer.x + (randomFloat() * 2.0 - 1.0) * 15.0,
        viewer.y + (randomFloat() * 2.0 - 0.45) * 10.0,
        viewer.z + (randomFloat() * 2.0 - 1.0) * 15.0};
    particle.velocity = {
        (randomFloat() - 0.5f) * 0.18f,
        (randomFloat() - 0.5f) * 0.10f,
        (randomFloat() - 0.5f) * 0.18f};
    particle.lifetime = 5.0f + randomFloat() * 5.0f;
    particle.phase = randomFloat();
    // Sky motes reuse the texture channel as an environment-color scalar;
    // ordinary weather particles continue to store their atlas tile there.
    particle.texture = std::clamp(daylight, 0.0f, 1.0f);
    particle.size = 0.10f + randomFloat() * 0.12f;
    particle.rotation = randomFloat() * 6.2831853f;
    particle.angularVelocity = (randomFloat() - 0.5f) * 1.8f;
    m_particles.push_back(particle);
}

void ParticleSystem::emitHeavenPollen(const glm::dvec3& viewer, uint64_t seed,
                                      int paletteIndex) {
    m_randomState ^= seed + 0x9E3D7B5F3C11E2A7ULL;
    Particle particle;
    particle.kind = ParticleKind::HeavenPollen;
    particle.position = {
        viewer.x + (randomFloat() * 2.0 - 1.0) * 14.0,
        viewer.y + (randomFloat() * 2.0 - 0.45) * 10.0,
        viewer.z + (randomFloat() * 2.0 - 1.0) * 14.0};
    particle.velocity = {
        (randomFloat() - 0.5f) * 0.14f,
        (randomFloat() - 0.5f) * 0.08f,
        (randomFloat() - 0.5f) * 0.14f};
    particle.lifetime = 7.0f + randomFloat() * 4.0f;
    particle.phase = randomFloat();
    // The texture channel carries the 0..7 exclusive-biome palette index;
    // the shader decodes it into the pollen tint.
    particle.texture = static_cast<float>(
        std::clamp(paletteIndex, 0, 7));
    particle.size = 0.14f + randomFloat() * 0.10f;
    particle.rotation = randomFloat() * 6.2831853f;
    particle.angularVelocity = (randomFloat() - 0.5f) * 0.9f;
    m_particles.push_back(particle);
}

void ParticleSystem::emitHeavenSparkle(const glm::dvec3& viewer, uint64_t seed,
                                       int paletteIndex) {
    m_randomState ^= seed + 0x4B1D2E7A9C35F0D1ULL;
    Particle particle;
    particle.kind = ParticleKind::HeavenSparkle;
    particle.position = {
        viewer.x + (randomFloat() * 2.0 - 1.0) * 13.0,
        viewer.y + (randomFloat() * 2.0 - 0.45) * 8.0,
        viewer.z + (randomFloat() * 2.0 - 1.0) * 13.0};
    particle.velocity = {
        (randomFloat() - 0.5f) * 0.05f,
        0.06f + randomFloat() * 0.08f,
        (randomFloat() - 0.5f) * 0.05f};
    particle.lifetime = 0.7f + randomFloat() * 0.9f;
    particle.phase = randomFloat();
    particle.texture = static_cast<float>(
        std::clamp(paletteIndex, 0, 7));
    particle.size = 0.16f + randomFloat() * 0.14f;
    particle.rotation = randomFloat() * 6.2831853f;
    particle.angularVelocity = 0.0f;
    m_particles.push_back(particle);
}

void ParticleSystem::emitOverworldAmbient(
    World& world, const glm::dvec3& viewer, uint64_t seed, bool firefly) {
    m_randomState ^= seed + (firefly ? 0xC6BC279692B5CC83ULL
                                    : 0xD1B54A32D192ED03ULL);
    const double radius = firefly ? 11.0 : 14.0;
    const double x = viewer.x + (randomFloat() * 2.0 - 1.0) * radius;
    const double z = viewer.z + (randomFloat() * 2.0 - 1.0) * radius;
    const int bx = static_cast<int>(std::floor(x));
    const int bz = static_cast<int>(std::floor(z));
    const int eyeY = static_cast<int>(std::floor(viewer.y + 1.0));
    if (!world.hasSkyAccess(bx, eyeY, bz)) return;
    const Biome biome = world.biomeAt(bx, bz);
    const BiomeProperties& properties = getBiomeProps(biome);
    if (firefly) {
        if (!supportsFireflies(biome)) return;
    } else if (properties.decorationDensity == 0 &&
               properties.treeDensity <= 0.04f) {
        return;
    }

    Particle particle;
    particle.kind = firefly ? ParticleKind::Firefly
                            : ParticleKind::OverworldMote;
    particle.position = {
        x, viewer.y + 0.5 + randomFloat() * (firefly ? 3.2 : 7.0), z};
    particle.velocity = {
        (randomFloat() - 0.5f) * (firefly ? 0.10f : 0.14f),
        (randomFloat() - 0.35f) * (firefly ? 0.08f : 0.06f),
        (randomFloat() - 0.5f) * (firefly ? 0.10f : 0.14f)};
    particle.lifetime = firefly
        ? 3.0f + randomFloat() * 3.0f : 4.0f + randomFloat() * 4.0f;
    particle.phase = randomFloat();
    particle.size = firefly
        ? 0.11f + randomFloat() * 0.07f : 0.08f + randomFloat() * 0.08f;
    particle.rotation = randomFloat() * 6.2831853f;
    particle.angularVelocity = (randomFloat() - 0.5f) * 0.8f;
    m_particles.push_back(particle);
}

void ParticleSystem::update(World& world, const glm::dvec3& viewer, float dt,
                            float rainIntensity, uint64_t seed,
                            DimensionId dimension, float daylight) {
    dt = std::min(dt, 0.1f);
    m_skyDaylight = std::clamp(daylight, 0.0f, 1.0f);
    if (rainIntensity > 0.01f) {
        m_weatherEmission += dt * rainIntensity * 520.0f;
        const int count = std::min(64, static_cast<int>(m_weatherEmission));
        m_weatherEmission -= count;
        for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES; ++i)
            emitWeatherParticle(world, viewer, seed + static_cast<uint64_t>(i));
    } else {
        m_weatherEmission = 0.0f;
    }
    if (dimension == DimensionId::Heaven) {
        // Light dust, drifting pollen, and star sparkles share one budgeted
        // ambient layer.  Pollen and sparkles tint themselves with the
        // exclusive biome sampled under a nearby column, so the air reads
        // differently over each sanctuary.
        m_skyMoteEmission += dt * 16.0f;
        m_pollenEmission += dt * 10.0f;
        m_sparkleEmission += dt * 16.0f;
        const int dustCount = std::min(
            static_cast<int>(MAX_SKY_MOTES_PER_UPDATE),
            static_cast<int>(m_skyMoteEmission));
        const int pollenCount = std::min(
            static_cast<int>(MAX_POLLEN_PER_UPDATE),
            static_cast<int>(m_pollenEmission));
        const int sparkleCount = std::min(
            static_cast<int>(MAX_SPARKLE_PER_UPDATE),
            static_cast<int>(m_sparkleEmission));
        m_skyMoteEmission -= static_cast<float>(dustCount);
        m_pollenEmission -= static_cast<float>(pollenCount);
        m_sparkleEmission -= static_cast<float>(sparkleCount);
        for (int i = 0; i < dustCount && m_particles.size() < MAX_PARTICLES;
             ++i)
            emitSkyMote(viewer, seed + static_cast<uint64_t>(i) * 17u,
                        m_skyDaylight);
        for (int i = 0; i < pollenCount && m_particles.size() < MAX_PARTICLES;
             ++i) {
            const int bx = static_cast<int>(std::floor(
                viewer.x + (randomFloat() * 2.0 - 1.0) * 14.0));
            const int bz = static_cast<int>(std::floor(
                viewer.z + (randomFloat() * 2.0 - 1.0) * 14.0));
            emitHeavenPollen(viewer, seed + static_cast<uint64_t>(i) * 31u,
                             world.heavenBiomePaletteIndex(bx, bz));
        }
        for (int i = 0; i < sparkleCount && m_particles.size() < MAX_PARTICLES;
             ++i) {
            const int bx = static_cast<int>(std::floor(
                viewer.x + (randomFloat() * 2.0 - 1.0) * 13.0));
            const int bz = static_cast<int>(std::floor(
                viewer.z + (randomFloat() * 2.0 - 1.0) * 13.0));
            const int palette = world.heavenBiomePaletteIndex(bx, bz);
            // Sparkles favour the crystal and garden sanctuaries; the
            // mineral and bloom biomes glow more softly.
            int gate = 40;
            if (palette == 3 || palette == 6 || palette == 7) gate = 100;
            else if (palette == 2 || palette == 4) gate = 70;
            if (randomFloat() * 100.0f >= static_cast<float>(gate)) continue;
            emitHeavenSparkle(viewer, seed + static_cast<uint64_t>(i) * 43u,
                              palette);
        }
    } else {
        m_skyMoteEmission = 0.0f;
        m_pollenEmission = 0.0f;
        m_sparkleEmission = 0.0f;
        m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
            [](const Particle& particle) {
                return particle.kind == ParticleKind::SkyMote ||
                       particle.kind == ParticleKind::HeavenPollen ||
                       particle.kind == ParticleKind::HeavenSparkle;
            }), m_particles.end());

        const int viewerX = static_cast<int>(std::floor(viewer.x));
        const int viewerY = static_cast<int>(std::floor(viewer.y + 1.0));
        const int viewerZ = static_cast<int>(std::floor(viewer.z));
        const bool outdoors = world.hasSkyAccess(viewerX, viewerY, viewerZ);
        const Biome biome = world.biomeAt(viewerX, viewerZ);
        const BiomeProperties& properties = getBiomeProps(biome);
        const bool vegetated = properties.decorationDensity > 0 ||
            properties.treeDensity > 0.04f;
        const OverworldAmbientKind ambientKind = selectOverworldAmbient(
            dimension, biome, vegetated, outdoors, m_skyDaylight,
            rainIntensity, m_enhancedVisual.ambientParticlesPerSecond);
        if (ambientKind != OverworldAmbientKind::None) {
            m_overworldAmbientEmission += dt *
                m_enhancedVisual.ambientParticlesPerSecond;
            const int count = std::min(
                4, static_cast<int>(m_overworldAmbientEmission));
            m_overworldAmbientEmission -= static_cast<float>(count);
            for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES;
                 ++i)
                emitOverworldAmbient(
                    world, viewer, seed + static_cast<uint64_t>(i) * 59u,
                    ambientKind == OverworldAmbientKind::Firefly);
        } else {
            m_overworldAmbientEmission = 0.0f;
            m_particles.erase(std::remove_if(
                m_particles.begin(), m_particles.end(),
                [](const Particle& particle) {
                    return particle.kind == ParticleKind::OverworldMote ||
                           particle.kind == ParticleKind::Firefly;
                }), m_particles.end());
        }
    }

    for (auto& particle : m_particles) {
        particle.age += dt;
        if (particle.kind == ParticleKind::Lightning) continue;

        if (particle.kind == ParticleKind::BlockDebris) {
            particle.velocity.y -= 18.0f * dt;
            particle.velocity *= std::pow(0.18f, dt);
            particle.rotation += particle.angularVelocity * dt;
        } else if (particle.kind == ParticleKind::Rain) {
            particle.rotation += particle.angularVelocity * dt;
        } else if (particle.kind == ParticleKind::RainSplash) {
            particle.phase = std::clamp(
                particle.age / std::max(particle.lifetime, 0.001f), 0.0f, 1.0f);
        } else if (particle.kind == ParticleKind::Snow) {
            const float sway = std::sin(particle.age * 2.4f + particle.phase * 6.283f);
            particle.velocity.x += sway * dt * 0.6f;
            particle.velocity.z += std::cos(particle.age * 1.9f + particle.phase) * dt * 0.4f;
        } else if (particle.kind == ParticleKind::SkyMote ||
                   particle.kind == ParticleKind::HeavenPollen) {
            particle.velocity.x += std::sin(
                particle.age * 0.8f + particle.phase * 6.283f) * dt * 0.035f;
            particle.velocity.y += std::cos(
                particle.age * 0.6f + particle.phase * 4.1f) * dt * 0.025f;
            particle.velocity.z += std::sin(
                particle.age * 0.7f + particle.phase * 3.3f) * dt * 0.035f;
            particle.rotation += particle.angularVelocity * dt;
        } else if (particle.kind == ParticleKind::HeavenSparkle) {
            particle.velocity.x += std::sin(
                particle.age * 1.3f + particle.phase * 6.283f) * dt * 0.02f;
            particle.velocity.y += std::cos(
                particle.age * 2.2f + particle.phase * 3.9f) * dt * 0.015f;
            particle.rotation += particle.angularVelocity * dt;
        } else if (particle.kind == ParticleKind::OverworldMote ||
                   particle.kind == ParticleKind::Firefly) {
            const float drift = particle.kind == ParticleKind::Firefly
                ? 0.055f : 0.025f;
            particle.velocity.x += std::sin(
                particle.age * 1.1f + particle.phase * 6.283f) * dt * drift;
            particle.velocity.y += std::cos(
                particle.age * 1.4f + particle.phase * 4.7f) * dt * drift;
            particle.velocity.z += std::sin(
                particle.age * 0.9f + particle.phase * 3.1f) * dt * drift;
            particle.rotation += particle.angularVelocity * dt;
        } else if (particle.kind == ParticleKind::CriticalHit ||
                   particle.kind == ParticleKind::SweepAttack) {
            particle.phase = std::clamp(
                particle.age / std::max(particle.lifetime, 0.001f), 0.0f, 1.0f);
            particle.rotation += particle.angularVelocity * dt;
        }

        if (particle.kind == ParticleKind::SkyMote ||
            particle.kind == ParticleKind::HeavenPollen ||
            particle.kind == ParticleKind::HeavenSparkle ||
            particle.kind == ParticleKind::OverworldMote ||
            particle.kind == ParticleKind::Firefly ||
            particle.kind == ParticleKind::CriticalHit ||
            particle.kind == ParticleKind::SweepAttack) {
            particle.position += glm::dvec3(particle.velocity) * static_cast<double>(dt);
            continue;
        }

        const glm::dvec3 next = particle.position + glm::dvec3(particle.velocity) *
                                                    static_cast<double>(dt);
        const int nx = static_cast<int>(std::floor(next.x));
        const int ny = static_cast<int>(std::floor(next.y));
        const int nz = static_cast<int>(std::floor(next.z));
        if (!Config::isValidWorldY(ny) || isSolid(world.getBlock(nx, ny, nz))) {
            if (particle.kind == ParticleKind::BlockDebris &&
                particle.velocity.y < 0.0f) {
                particle.velocity.y *= -0.28f;
                particle.velocity.x *= 0.62f;
                particle.velocity.z *= 0.62f;
                particle.position.y = std::floor(particle.position.y) + 0.02;
            } else if (particle.kind == ParticleKind::Rain) {
                particle.kind = ParticleKind::RainSplash;
                particle.position = {next.x, static_cast<double>(ny) + 1.015,
                                     next.z};
                particle.velocity = glm::vec3(0.0f);
                particle.age = 0.0f;
                particle.lifetime = 0.24f;
                particle.phase = 0.0f;
                particle.size = 0.34f;
                particle.rotation = 0.0f;
                particle.angularVelocity = 0.0f;
            } else {
                particle.age = particle.lifetime;
            }
        } else {
            particle.position = next;
        }
    }

    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
        [&viewer](const Particle& particle) {
            const glm::dvec3 delta = particle.position - viewer;
            return particle.age >= particle.lifetime ||
                   glm::dot(delta, delta) > 48.0 * 48.0;
        }), m_particles.end());
}

void ParticleSystem::emitBlockBreak(const glm::ivec3& position, BlockId block) {
    if (block == BlockId::AIR) return;
    const float texture = static_cast<float>(getFaceTextureIndex(block, FaceDir::TOP));
    for (int i = 0; i < 12 && m_particles.size() < MAX_PARTICLES; ++i) {
        Particle particle;
        particle.kind = ParticleKind::BlockDebris;
        particle.position = glm::dvec3(position) + glm::dvec3(
            0.12 + randomFloat() * 0.76, 0.12 + randomFloat() * 0.76,
            0.12 + randomFloat() * 0.76);
        const float angle = randomFloat() * 6.2831853f;
        const float speed = 1.2f + randomFloat() * 2.3f;
        particle.velocity = {std::cos(angle) * speed,
                             1.8f + randomFloat() * 2.8f,
                             std::sin(angle) * speed};
        particle.lifetime = 0.65f + randomFloat() * 0.55f;
        particle.phase = randomFloat();
        particle.texture = texture;
        particle.size = 0.10f + randomFloat() * 0.09f;
        particle.rotation = randomFloat() * 6.2831853f;
        particle.angularVelocity = (randomFloat() - 0.5f) * 9.0f;
        m_particles.push_back(particle);
    }
}

void ParticleSystem::emitExplosion(const glm::dvec3& position) {
    const float texture = static_cast<float>(
        getAtlasTextureIndex(BlockTexture::Tnt));
    for (int i = 0; i < 80 && m_particles.size() < MAX_PARTICLES; ++i) {
        Particle particle;
        particle.kind = ParticleKind::BlockDebris;
        particle.position = position + glm::dvec3(
            (randomFloat() - .5f) * .8f, randomFloat() * .8f,
            (randomFloat() - .5f) * .8f);
        glm::vec3 direction(randomFloat() * 2.0f - 1.0f,
                            randomFloat() * 1.4f - .2f,
                            randomFloat() * 2.0f - 1.0f);
        if (glm::length(direction) < .01f) direction = {0,1,0};
        direction = glm::normalize(direction);
        particle.velocity = direction * (3.0f + randomFloat() * 8.0f);
        particle.lifetime = .55f + randomFloat() * .9f;
        particle.phase = randomFloat();
        particle.texture = texture;
        particle.size = .14f + randomFloat() * .22f;
        particle.rotation = randomFloat() * 6.2831853f;
        particle.angularVelocity = (randomFloat() - .5f) * 14.0f;
        m_particles.push_back(particle);
    }
}

void ParticleSystem::emitCriticalHit(const glm::dvec3& position) {
    for (int i = 0; i < 12 && m_particles.size() < MAX_PARTICLES; ++i) {
        Particle particle;
        particle.kind = ParticleKind::CriticalHit;
        particle.position = position + glm::dvec3(
            (randomFloat() - 0.5f) * 0.8f,
            0.35f + randomFloat() * 1.2f,
            (randomFloat() - 0.5f) * 0.8f);
        particle.velocity = {(randomFloat() - 0.5f) * 1.4f,
                             0.35f + randomFloat() * 1.0f,
                             (randomFloat() - 0.5f) * 1.4f};
        particle.lifetime = 0.28f + randomFloat() * 0.18f;
        particle.size = 0.16f + randomFloat() * 0.12f;
        particle.rotation = randomFloat() * 6.2831853f;
        particle.angularVelocity = (randomFloat() - 0.5f) * 8.0f;
        m_particles.push_back(particle);
    }
}

void ParticleSystem::emitSweepAttack(const glm::dvec3& position) {
    for (int i = 0; i < 3 && m_particles.size() < MAX_PARTICLES; ++i) {
        Particle particle;
        particle.kind = ParticleKind::SweepAttack;
        particle.position = position + glm::dvec3(0.0, 0.75 + i * 0.18, 0.0);
        particle.lifetime = 0.22f + i * 0.035f;
        particle.size = 1.05f + i * 0.18f;
        particle.rotation = -0.35f + i * 0.35f;
        m_particles.push_back(particle);
    }
}

void ParticleSystem::appendLightning(const glm::dvec3& position) {
    for (int segment = 0; segment < 16 && m_particles.size() < MAX_PARTICLES;
         ++segment) {
        const double y = position.y + segment * 4.0;
        if (y >= Config::WORLD_MAX_Y) break;
        Particle particle;
        particle.kind = ParticleKind::Lightning;
        particle.position = {position.x + 0.5, y, position.z + 0.5};
        particle.lifetime = 0.5f;
        particle.phase = segment / 16.0f;
        m_particles.push_back(particle);
    }
}

std::vector<ParticleRenderData> ParticleSystem::buildRenderData(
    const glm::dvec3& renderOrigin) const {
    std::vector<ParticleRenderData> result;
    buildRenderData(renderOrigin, result);
    return result;
}

void ParticleSystem::buildRenderData(
    const glm::dvec3& renderOrigin,
    std::vector<ParticleRenderData>& result) const {
    result.clear();
    result.reserve(m_particles.size());
    for (const auto& particle : m_particles) {
        const glm::dvec3 relative = particle.position - renderOrigin;
        result.push_back({glm::vec3(relative), static_cast<float>(particle.kind),
                          particle.phase, particle.texture, particle.size,
                          particle.rotation});
    }
}
