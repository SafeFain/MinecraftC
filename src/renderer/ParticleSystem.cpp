#include "renderer/ParticleSystem.h"
#include "plugins/EnvironmentPresentation.h"

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "world/BiomeMap.h"
#include "world/World.h"

void ParticleSystem::clear() {
    m_particles.clear();m_pluginEmitted=0;m_ambientState.clear();m_ambientDimension=MC_INVALID_DIMENSION;
    m_weatherEmission = 0.0f;
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
                   particle.kind == ParticleKind::Firefly ||
                   particle.kind == ParticleKind::CaveSpore ||
                   particle.kind == ParticleKind::CaveDust ||
                   particle.kind == ParticleKind::CaveEmber || particle.kind==ParticleKind::NetherMote;
        }), m_particles.end());
}

void ParticleSystem::setEnhancedVisuals(
    const EnhancedVisualSettings& settings, VisualQuality quality) {
    m_enhancedVisual = enhancedVisualConfig(quality, settings);
    if (m_enhancedVisual.ambientParticlesPerSecond > 0.0f) return;
    m_overworldAmbientEmission = 0.0f;
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
        [](const Particle& particle) {
            return particle.kind == ParticleKind::OverworldMote ||
                   particle.kind == ParticleKind::Firefly ||
                   particle.kind == ParticleKind::CaveSpore ||
                   particle.kind == ParticleKind::CaveDust ||
                   particle.kind == ParticleKind::CaveEmber || particle.kind==ParticleKind::NetherMote;
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

void ParticleSystem::emitCaveAmbient(
    World& world, const glm::dvec3& viewer, uint64_t seed,
    CaveAmbientKind ambient) {
    m_randomState ^= seed + 0x7A1B4C9D2E6F8031ULL;
    Particle particle;
    particle.kind = ambient == CaveAmbientKind::Spore ? ParticleKind::CaveSpore
                  : ambient == CaveAmbientKind::Ember ? ParticleKind::CaveEmber
                                                      : ParticleKind::CaveDust;
    particle.position = {
        viewer.x + (randomFloat() * 2.0 - 1.0) * 10.0,
        viewer.y + (randomFloat() * 2.0 - 0.6) * 6.0,
        viewer.z + (randomFloat() * 2.0 - 1.0) * 10.0};
    const int x = static_cast<int>(std::floor(particle.position.x));
    const int y = static_cast<int>(std::floor(particle.position.y));
    const int z = static_cast<int>(std::floor(particle.position.z));
    if (!Config::isValidWorldY(y) || world.getBlock(x, y, z) != BlockId::AIR ||
        world.hasSkyAccess(x, y, z)) return;
    const float lift = ambient == CaveAmbientKind::Ember ? 0.16f : 0.035f;
    particle.velocity = {(randomFloat() - 0.5f) * 0.09f,
                         lift + (randomFloat() - 0.5f) * 0.05f,
                         (randomFloat() - 0.5f) * 0.09f};
    particle.lifetime = 3.5f + randomFloat() * 3.5f;
    particle.phase = randomFloat();
    particle.size = ambient == CaveAmbientKind::Dust
        ? 0.07f + randomFloat() * 0.07f
        : 0.10f + randomFloat() * 0.08f;
    particle.rotation = randomFloat() * 6.2831853f;
    particle.angularVelocity = (randomFloat() - 0.5f) * 0.7f;
    m_particles.push_back(particle);
}

void ParticleSystem::update(World& world, const glm::dvec3& viewer, float dt,
                            float rainIntensity, uint64_t seed,
                            DimensionId dimension, float daylight) {
    dt = std::min(dt, 0.1f);
    m_pluginEmitted=0;
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
    const uint32_t dim=static_cast<uint32_t>(dimension);
    if(m_ambientDimension!=dim) {
        m_ambientState.clear();m_ambientDimension=dim;
        m_particles.erase(std::remove_if(m_particles.begin(),m_particles.end(),[](const Particle& p){return p.ambient;}),m_particles.end());
    }
    bool replaceAmbient=false;
    for(const auto& entry:Plugins::content().extensions.ambient) {
        const auto& a=entry.second;if(a.dimension!=dim)continue;replaceAmbient=replaceAmbient||a.replaceNative;
        auto& state=m_ambientState[a.key];state.resize((a.stateBytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));
        struct Reader {ParticleSystem* system;size_t owner;uint32_t dimension;World* world;uint32_t samples=0;};
        Reader reader{this,a.owner,dim,&world};
        const auto& g=world.generator();MC_AmbientContext view{};view.size=sizeof(view);view.dimension=dim;
        view.capacity=uint32_t(std::min<size_t>(128-m_pluginEmitted,MAX_PARTICLES-m_particles.size()));
        view.generation={sizeof(MC_GenerationContext),dim,g.getSeed(),g.generationConfig().parameters.c_str()};
        view.generation_provider=g.generationConfig().generator.c_str();
        view.layers=[](void* data,int32_t x,int32_t z,MC_GenerationLayer* out,uint32_t capacity,uint32_t* count)->int32_t {
            auto& r=*static_cast<Reader*>(data);
            if(!out||!count||++r.samples>256||std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000)return -1;
            try{const auto layers=r.world->generator().sampleGenerationLayers(x,z);*count=uint32_t(layers.size());
                if(layers.size()>capacity)return -1;std::copy(layers.begin(),layers.end(),out);return 0;}catch(...){return -1;}
        };
        for(int i=0;i<3;++i)view.viewer[i]=viewer[i];view.dt=dt;view.daylight=m_skyDaylight;view.seed=seed;view.reader=&reader;
        view.random_bits=[](void* p){return static_cast<Reader*>(p)->system->randomBits();};
        view.mix_random=[](void* p,uint64_t value){static_cast<Reader*>(p)->system->m_randomState^=value;};
        view.emit=[](void* p,const MC_ParticleInstanceV2* value)->int32_t {
            const auto& r=*static_cast<Reader*>(p);if(!value||!value->particle||value->dimension!=r.dimension)return -1;
            const auto found=Plugins::content().extensions.particles.find(value->particle);
            if(found==Plugins::content().extensions.particles.end()||found->second.owner!=r.owner)return -1;
            if(!r.system->emitPluginV2(*value))return -1;r.system->m_particles.back().ambient=true;return 0;
        };
        if(!a.update(view,state.empty()?nullptr:state.data()))break;
    }
    if(dimension==DimensionId::Nether && m_enhancedVisual.ambientParticlesPerSecond>0) {
        m_overworldAmbientEmission+=dt*m_enhancedVisual.ambientParticlesPerSecond*.7f;
        const int count=std::min(4,int(m_overworldAmbientEmission));m_overworldAmbientEmission-=count;
        const auto biome=world.generator().nether().biomeAt(int(std::floor(viewer.x)),int(std::floor(viewer.z)));
        constexpr glm::vec4 colors[]={{1,.42f,.13f,.65f},{.45f,.74f,.82f,.42f},{.94f,.23f,.25f,.5f},{.22f,.85f,.75f,.5f},{.7f,.67f,.64f,.65f}};
        for(int i=0;i<count && m_particles.size()<MAX_PARTICLES;++i) {
            Particle p;p.kind=ParticleKind::NetherMote;p.ambient=true;
            p.position=viewer+glm::dvec3((randomFloat()-.5f)*20,(randomFloat()-.3f)*12,(randomFloat()-.5f)*20);
            const auto cell=glm::ivec3(glm::floor(p.position));
            if(world.getLoadedBlock(cell.x,cell.y,cell.z)!=BlockId::AIR)continue;
            p.velocity={(randomFloat()-.5f)*.18f,biome==NetherBiome::BasaltDeltas?-.18f:.08f,(randomFloat()-.5f)*.18f};
            p.lifetime=4+randomFloat()*4;p.phase=randomFloat();p.size=.06f+randomFloat()*.07f;
            p.color=colors[static_cast<size_t>(biome)];m_particles.push_back(p);
        }
    } else if(!replaceAmbient) {
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
            const CaveAmbientKind caveAmbient = selectCaveAmbient(
                dimension, world.caveBiomeAt(viewerX, viewerY, viewerZ),
                !outdoors, m_enhancedVisual.ambientParticlesPerSecond);
            if (caveAmbient != CaveAmbientKind::None) {
                m_overworldAmbientEmission += dt *
                    m_enhancedVisual.ambientParticlesPerSecond * 0.65f;
                const int count = std::min(
                    3, static_cast<int>(m_overworldAmbientEmission));
                m_overworldAmbientEmission -= static_cast<float>(count);
                for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES;
                     ++i)
                    emitCaveAmbient(world, viewer,
                        seed + static_cast<uint64_t>(i) * 71u, caveAmbient);
            } else {
                m_overworldAmbientEmission = 0.0f;
            }
        }
        if (ambientKind == OverworldAmbientKind::None && outdoors) {
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

        if(particle.kind==ParticleKind::Plugin){
            particle.velocity.y-=particle.gravity*dt;
            if(!particle.advanced)particle.phase=particle.age/particle.lifetime;
            else {
                for(int axis=0;axis<3;++axis) {
                    const auto& drift=particle.drift[size_t(axis)];const float angle=particle.age*drift[0]+particle.phase*drift[1];
                    particle.velocity[axis]+=(drift[3]==0?std::sin(angle):std::cos(angle))*dt*drift[2];
                }
                particle.rotation+=particle.angularVelocity*dt;
            }
        }
        else if (particle.kind == ParticleKind::BlockDebris) {
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

        if ((particle.kind==ParticleKind::Plugin&&!particle.collision) ||
                   particle.kind == ParticleKind::OverworldMote ||
                   particle.kind == ParticleKind::Firefly ||
                   particle.kind == ParticleKind::CaveSpore ||
                   particle.kind == ParticleKind::CaveDust ||
                   particle.kind == ParticleKind::CaveEmber ||
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
        result.push_back({glm::vec3(relative), particle.kind==ParticleKind::NetherMote?6.f:particle.kind==ParticleKind::Plugin&&particle.shape?float(5+particle.shape):static_cast<float>(particle.kind),
                          particle.phase, particle.texture, particle.size,
                          particle.rotation,particle.color});
    }
}

void ParticleSystem::emitFishingSplash(const glm::dvec3& position, bool bite) {
    const int count = bite ? 14 : 8;
    for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES; ++i) {
        const float angle = static_cast<float>(i) / count * 6.2831853f;
        Particle p;
        p.kind = ParticleKind::RainSplash;
        p.position = position + glm::dvec3(std::cos(angle) * .16, .08, std::sin(angle) * .16);
        p.velocity = glm::vec3(std::cos(angle) * .5f, .2f, std::sin(angle) * .5f);
        p.lifetime = .45f;
        p.size = bite ? .14f : .09f;
        m_particles.push_back(p);
    }
}

bool ParticleSystem::emitPlugin(const MC_ParticleEmission& e){
    if(!e.particle||!e.count||e.count>128||m_pluginEmitted+e.count>128||m_particles.size()+e.count>MAX_PARTICLES)return false;
    const auto found=Plugins::content().extensions.particles.find(e.particle);if(found==Plugins::content().extensions.particles.end())return false;const auto& d=found->second.definition;const auto tile=Plugins::materialTile(found->second.material);
    for(uint32_t i=0;i<e.count;++i){Particle p;p.kind=ParticleKind::Plugin;p.position={e.position[0],e.position[1],e.position[2]};p.velocity={d.velocity[0],d.velocity[1],d.velocity[2]};p.velocity+=glm::vec3(randomFloat()-.5f,randomFloat()-.5f,randomFloat()-.5f)*.2f;p.lifetime=d.lifetime;p.size=d.scale;p.gravity=d.gravity;p.color={d.color[0],d.color[1],d.color[2],d.color[3]};p.texture=tile;m_particles.push_back(p);}m_pluginEmitted+=e.count;return true;
}

bool ParticleSystem::emitPluginV2(const MC_ParticleInstanceV2& e) {
    if(e.size<sizeof(e)||!e.particle||m_pluginEmitted>=128||m_particles.size()>=MAX_PARTICLES)return false;
    const auto found=Plugins::content().extensions.particles.find(e.particle);
    if(found==Plugins::content().extensions.particles.end()||!found->second.advanced)return false;
    const auto finite=[](float value,float lo,float hi){return std::isfinite(value)&&value>=lo&&value<=hi;};
    for(double value:e.position)if(!std::isfinite(value)||std::abs(value)>30000000)return false;
    for(float value:e.velocity)if(!finite(value,-100,100))return false;
    for(float value:e.color)if(!finite(value,0,16))return false;
    if(!finite(e.lifetime,.01f,60)||!finite(e.scale,.001f,4)||!finite(e.phase,0,1)||
       !finite(e.rotation,-100,100)||!finite(e.angular_velocity,-100,100))return false;
    const auto& d=found->second;Particle p;p.kind=ParticleKind::Plugin;p.advanced=true;p.collision=d.collision;p.shape=d.shape;p.drift=d.drift;
    p.position={e.position[0],e.position[1],e.position[2]};p.velocity={e.velocity[0],e.velocity[1],e.velocity[2]};
    p.color={e.color[0],e.color[1],e.color[2],e.color[3]};p.lifetime=e.lifetime;p.size=e.scale;p.phase=e.phase;
    p.rotation=e.rotation;p.angularVelocity=e.angular_velocity;p.gravity=d.definition.gravity;p.texture=Plugins::materialTile(d.material);
    m_particles.push_back(p);++m_pluginEmitted;return true;
}
