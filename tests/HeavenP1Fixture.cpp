#include "minecraftc/plugin.hpp"
#include <cstring>
#ifndef P1_FIXTURE_KIND
#define P1_FIXTURE_KIND 0
#endif
#if P1_FIXTURE_KIND == 1
#define P1_ID "p1_bad_namespace"
#elif P1_FIXTURE_KIND == 2
#define P1_ID "p1_bad_english"
#else
#define P1_ID "p1_fixture"
#endif
#define P1_KEY(s) P1_ID ":" s
namespace {
uint16_t stone;
int32_t spawn(const MC_GenerationContext* c,double* out,void*) {
    out[0]=double(c->seed%4)+.5;out[1]=81.01;out[2]=-.5;
    return std::strcmp(c->parameters,"{\"fixture\":3}")==0?0:-1;
}
int32_t column(const MC_GenerationContext*,int32_t,int32_t,MC_SurfaceSample* out,void*) {
    out->height=80;out->water_level=-65;out->biome=1;return 0;
}
int32_t voxel(const MC_GenerationContext*,int32_t,int32_t y,int32_t,uint16_t* out,void*) {*out=y<=80?stone:0;return 0;}
int32_t layers(const MC_GenerationContext*,int32_t,int32_t,MC_GenerationLayer* out,uint32_t cap,uint32_t* count,void*) {
    if(cap<2)return -1;*count=2;out[0]={-64,80,1,7};out[1]={210,220,0,9};return 0;
}
int32_t target(const MC_ActionViewV3* view,MC_ActionTargetV2* out,void*) {
    out->handled=1;
    if(std::strcmp(view->generation_provider,P1_KEY("flat")))return 0;
    out->base.valid=1;out->base.position[0]=view->base.base.position[0];out->base.position[1]=81.01;out->base.position[2]=view->base.base.position[2];return 0;
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    minecraftc::Host host(api,context);
    const auto* localization=static_cast<const MC_LocalizationV1*>(host.extension(MC_LOCALIZATION_EXTENSION,1));
    const auto* dimensions=static_cast<const MC_DimensionsV3*>(host.extension(MC_DIMENSIONS_EXTENSION,3));
    const auto* worldgen=static_cast<const MC_WorldgenV3*>(host.extension(MC_WORLDGEN_EXTENSION,3));
    const auto* actions=static_cast<const MC_PlayerActionsV3*>(host.extension(MC_PLAYER_ACTIONS_EXTENSION,3));
    const auto* environment=static_cast<const MC_EnvironmentV2*>(host.extension(MC_ENVIRONMENT_EXTENSION,2));
    const auto* presentation=static_cast<const MC_PresentationV2*>(host.extension(MC_PRESENTATION_EXTENSION,2));
    const auto* materials=static_cast<const MC_MaterialSequencesV1*>(host.extension(MC_MATERIAL_SEQUENCES_EXTENSION,1));
    if(!localization||!dimensions||!worldgen||!actions||!environment||!presentation||!materials)return -1;
    MC_DimensionDefinitionV3 dimension{};dimension.size=sizeof(dimension);dimension.base.size=sizeof(dimension.base);
    auto& d=dimension.base.base;d.size=sizeof(d);d.key=P1_KEY("realm");d.name="Third realm";d.revision="1";
    d.generator=P1_KEY("flat");d.generator_revision="1";d.parameters="{\"fixture\":3}";d.disable_natural_spawning=1;
    dimension.name_key=P1_KEY("name");dimension.seed_domain=0x50315f444f4d4149ULL;dimension.base.sample_spawn=spawn;
    uint32_t handle=MC_INVALID_DIMENSION;
    if(dimensions->register_dimension(context,&dimension,&handle)||localization->register_bundle(context,"lang"))return -1;
    stone=host.block("minecraftc:stone");
    MC_GeneratorDefinitionV3 generator{};generator.size=sizeof(generator);generator.base.size=sizeof(generator.base);
    generator.base.base={sizeof(MC_GeneratorDefinition),P1_KEY("flat"),"Third realm","1",handle,column,voxel,nullptr};generator.layers=layers;
    if(worldgen->register_generator(context,&generator))return -1;
    MC_DimensionTravelEntry travel{sizeof(travel),0,handle,P1_KEY("travel"),P1_KEY("travel_name")};
    if(dimensions->register_bed_travel(context,&travel))return -1;
    MC_PlayerActionV3 action{};action.size=sizeof(action);action.base.size=sizeof(action.base);action.base.base.size=sizeof(action.base.base);
    action.base.base.key=P1_KEY("action");action.base.base.range=16;action.target=target;
    if(actions->register_action(context,&action))return -1;
    MC_EnvironmentProfileV2 profile{};profile.size=sizeof(profile);profile.base.size=sizeof(profile.base);profile.base.key=P1_KEY("environment");
    profile.base.dimension=handle;profile.base.parameters.size=sizeof(profile.base.parameters);profile.base.parameters.clouds=1;
    profile.base.parameters.sun_size=profile.base.parameters.moon_size=1;profile.sky.size=sizeof(profile.sky);
    profile.override_particle_intensity=1;profile.particle_intensity=.35f;
    profile.sky_flags=MC_SKY_NO_ATMOSPHERE;profile.grade_strength=.25f;
    profile.grade_color[0]=.9f;profile.grade_color[1]=1.1f;profile.grade_color[2]=1;
    if(environment->register_profile(context,&profile))return -1;
    MC_ParticleDefinitionV2 particle{};particle.size=sizeof(particle);particle.base.size=sizeof(particle.base);
    particle.base.key=P1_KEY("particle");particle.base.material="stone";particle.base.lifetime=2;particle.base.scale=.1f;
    for(float& channel:particle.base.color)channel=1;particle.shape=2;particle.drift[0][0]=.8f;particle.drift[0][2]=.01f;
    if(presentation->register_particle(context,&particle))return -1;
    const char* images[]={"tile.png","tile.png","tile.png","tile.png"};
    MC_MaterialSequenceDefinition material{};material.size=sizeof(material);material.base.size=sizeof(material.base);material.base.key=P1_KEY("material");
    material.variants=material.frames=2;material.fps_milli=2500;material.seed=123;material.images=images;
    return materials->register_sequence(context,&material);
}
const MC_Plugin plugin{sizeof(plugin),MC_PLUGIN_ABI,P1_ID,"1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
