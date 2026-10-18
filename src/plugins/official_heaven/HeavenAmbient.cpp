#include "plugins/official_heaven/HeavenPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
namespace OfficialHeaven {
namespace {
enum class Kind {Mote,Tuft,Sparkle};
struct Particle {
    Kind kind=Kind::Mote;
    glm::dvec3 position{0};glm::vec3 velocity{0};
    float lifetime=1,phase=0,texture=0,size=.1f,rotation=0,angularVelocity=0;
};
struct State {float dust=0,pollen=0,sparkle=0;};
struct Emitter {
    const MC_AmbientContext& c;
    uint32_t emitted=0;bool failed=false;
    float randomFloat(){return float(c.random_bits(c.reader)>>40)/16777216.0f;}
    int biomeAt(int x,int z) {
        MC_GenerationLayer layers[5]{};uint32_t count=0;
        if(c.layers(c.reader,x,z,layers,5,&count)||count!=5||layers[2].biome>=8){failed=true;return 0;}
        return static_cast<int>(layers[2].biome);
    }
    void push(const Particle& p) {
        constexpr std::array<glm::vec3,8> palette{{{1,.86f,.55f},{.62f,.95f,.55f},{1,.72f,.30f},{.45f,.85f,1},{.95f,.97f,1},{.78f,.80f,.86f},{.40f,1,.80f},{.85f,.75f,1}}};
        glm::vec3 tint;
        if(p.kind==Kind::Mote) {
            tint=glm::mix(glm::vec3(.40f,.58f,1),glm::vec3(1,.84f,.38f),std::clamp(p.texture,0.f,1.f));
            tint=glm::mix(tint,glm::vec3(.78f,.42f,1),.5f+.5f*std::sin(p.phase*9.f));
        } else {tint=palette[size_t(std::clamp(int(p.texture),0,7))];if(p.kind==Kind::Sparkle)tint*=1.2f;}
        MC_ParticleInstanceV2 out{};out.size=sizeof(out);out.dimension=c.dimension;
        out.particle=p.kind==Kind::Mote?"official_heaven:mote":p.kind==Kind::Tuft?"official_heaven:pollen":"official_heaven:sparkle";
        for(int i=0;i<3;++i){out.position[i]=p.position[i];out.velocity[i]=p.velocity[i];out.color[i]=tint[i];}out.color[3]=1;
        out.lifetime=p.lifetime;out.scale=p.size;out.phase=p.phase;out.rotation=p.rotation;out.angular_velocity=p.angularVelocity;
        if(c.emit(c.reader,&out))failed=true;else ++emitted;
    }
void emitSkyMote(const glm::dvec3& viewer, uint64_t seed,
                                 float daylight) {
    c.mix_random(c.reader,seed + 0xA0761D6478BD642FULL);
    Particle particle;
    particle.kind = Kind::Mote;
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
    push(particle);
}
void emitHeavenPollen(const glm::dvec3& viewer, uint64_t seed,
                                      int paletteIndex) {
    c.mix_random(c.reader,seed + 0x9E3D7B5F3C11E2A7ULL);
    Particle particle;
    particle.kind = Kind::Tuft;
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
    push(particle);
}
void emitHeavenSparkle(const glm::dvec3& viewer, uint64_t seed,
                                       int paletteIndex) {
    c.mix_random(c.reader,seed + 0x4B1D2E7A9C35F0D1ULL);
    Particle particle;
    particle.kind = Kind::Sparkle;
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
    push(particle);
}
    void update(State& state) {
        float& m_skyMoteEmission=state.dust;float& m_pollenEmission=state.pollen;float& m_sparkleEmission=state.sparkle;
        const float dt=c.dt,m_skyDaylight=c.daylight;const uint64_t seed=c.seed;
        const glm::dvec3 viewer(c.viewer[0],c.viewer[1],c.viewer[2]);
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
        for (int i = 0; i < dustCount && emitted < c.capacity;
             ++i)
            emitSkyMote(viewer, seed + static_cast<uint64_t>(i) * 17u,
                        m_skyDaylight);
        for (int i = 0; i < pollenCount && emitted < c.capacity;
             ++i) {
            const int bx = static_cast<int>(std::floor(
                viewer.x + (randomFloat() * 2.0 - 1.0) * 14.0));
            const int bz = static_cast<int>(std::floor(
                viewer.z + (randomFloat() * 2.0 - 1.0) * 14.0));
            emitHeavenPollen(viewer, seed + static_cast<uint64_t>(i) * 31u,
                             biomeAt(bx,bz));
        }
        for (int i = 0; i < sparkleCount && emitted < c.capacity;
             ++i) {
            const int bx = static_cast<int>(std::floor(
                viewer.x + (randomFloat() * 2.0 - 1.0) * 13.0));
            const int bz = static_cast<int>(std::floor(
                viewer.z + (randomFloat() * 2.0 - 1.0) * 13.0));
            const int palette = biomeAt(bx,bz);
            // Sparkles favour the crystal and garden sanctuaries; the
            // mineral and bloom biomes glow more softly.
            int gate = 40;
            if (palette == 3 || palette == 6 || palette == 7) gate = 100;
            else if (palette == 2 || palette == 4) gate = 70;
            if (randomFloat() * 100.0f >= static_cast<float>(gate)) continue;
            emitHeavenSparkle(viewer, seed + static_cast<uint64_t>(i) * 43u,
                              palette);
        }

    }
};
int32_t ambientUpdate(const MC_AmbientContext* c,void* state,void*) {
    if(!c||c->size<sizeof(*c)||!state||!c->emit||!c->random_bits||!c->mix_random)return -1;
    if(!c->generation_provider||std::strcmp(c->generation_provider,"official_heaven:heaven"))return 0;
    if(!c->layers)return -1;
    State counters{};std::memcpy(&counters,state,sizeof(counters));
    Emitter e{*c};e.update(counters);std::memcpy(state,&counters,sizeof(counters));return e.failed?-1:0;
}
}
std::array<MC_ParticleDefinitionV2,3> particleDefinitions() {
    std::array<MC_ParticleDefinitionV2,3> result{};
    const char* keys[]={"official_heaven:mote","official_heaven:pollen","official_heaven:sparkle"};
    for(size_t i=0;i<3;++i) {
        auto& d=result[i];d.size=sizeof(d);d.base.size=sizeof(d.base);d.base.key=keys[i];d.base.material="star_crystal";
        d.base.lifetime=1;d.base.scale=.1f;for(float& value:d.base.color)value=1;d.shape=uint32_t(i+1);d.collision=0;
        if(i<2){const float drift[3][4]={{.8f,6.283f,.035f,0},{.6f,4.1f,.025f,1},{.7f,3.3f,.035f,0}};for(size_t axis=0;axis<3;++axis)std::copy(std::begin(drift[axis]),std::end(drift[axis]),d.drift[axis]);}
        else {const float drift[3][4]={{1.3f,6.283f,.02f,0},{2.2f,3.9f,.015f,1},{0,0,0,0}};for(size_t axis=0;axis<3;++axis)std::copy(std::begin(drift[axis]),std::end(drift[axis]),d.drift[axis]);}
    }return result;
}
MC_AmbientDefinition ambientDefinition() {
    return {sizeof(MC_AmbientDefinition),1,sizeof(State),1,"official_heaven:ambient",ambientUpdate,nullptr};
}
}
