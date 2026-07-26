#include "renderer/ParticleSystem.h"

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "world/World.h"

void ParticleSystem::clear() {
    m_particles.clear();
    m_weatherEmission = 0.0f;
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
        ? glm::vec3((randomFloat() - 0.5f) * 0.25f, -20.0f,
                    (randomFloat() - 0.5f) * 0.25f)
        : glm::vec3((randomFloat() - 0.5f) * 0.8f, -2.2f,
                    (randomFloat() - 0.5f) * 0.8f);
    particle.lifetime = particle.kind == ParticleKind::Rain ? 1.6f : 7.0f;
    particle.phase = randomFloat();
    particle.size = 1.0f;
    m_particles.push_back(particle);
}

void ParticleSystem::update(World& world, const glm::dvec3& viewer, float dt,
                            float rainIntensity, uint64_t seed) {
    dt = std::min(dt, 0.1f);
    if (rainIntensity > 0.01f) {
        m_weatherEmission += dt * rainIntensity * 520.0f;
        const int count = std::min(64, static_cast<int>(m_weatherEmission));
        m_weatherEmission -= count;
        for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES; ++i)
            emitWeatherParticle(world, viewer, seed + static_cast<uint64_t>(i));
    } else {
        m_weatherEmission = 0.0f;
    }

    for (auto& particle : m_particles) {
        particle.age += dt;
        if (particle.kind == ParticleKind::Lightning) continue;

        if (particle.kind == ParticleKind::BlockDebris) {
            particle.velocity.y -= 18.0f * dt;
            particle.velocity *= std::pow(0.18f, dt);
            particle.rotation += particle.angularVelocity * dt;
        } else if (particle.kind == ParticleKind::Snow) {
            const float sway = std::sin(particle.age * 2.4f + particle.phase * 6.283f);
            particle.velocity.x += sway * dt * 0.6f;
            particle.velocity.z += std::cos(particle.age * 1.9f + particle.phase) * dt * 0.4f;
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
    result.reserve(m_particles.size());
    for (const auto& particle : m_particles) {
        const glm::dvec3 relative = particle.position - renderOrigin;
        result.push_back({glm::vec3(relative), static_cast<float>(particle.kind),
                          particle.phase, particle.texture, particle.size,
                          particle.rotation});
    }
    return result;
}
