#include "plugins/official_heaven/HeavenEnvironment.h"
#include <algorithm>
namespace OfficialHeaven {
namespace {
int32_t evaluate(MC_PluginContext*,MC_EnvironmentParameters* p,void*) {
    if(!p||p->size<sizeof(*p))return -1;
    RenderEnvironment input;input.daylight=p->daylight;input.starIntensity=p->star_intensity;
    input.sunDirection={p->sun_direction[0],p->sun_direction[1],p->sun_direction[2]};
    input.directIntensity=p->colors.direct_intensity;input.ambientIntensity=p->colors.ambient_intensity;
    const auto output=environment(input);
    const auto copy=[](float* out,const glm::vec3& v){for(int i=0;i<3;++i)out[i]=v[i];};
    copy(p->colors.zenith,output.zenithColor);copy(p->colors.horizon,output.horizonColor);copy(p->colors.fog,output.fogColor);
    copy(p->colors.ambient_color,output.ambientColor);copy(p->colors.direct_color,output.directColor);
    p->colors.ambient_intensity=output.ambientIntensity;p->colors.direct_intensity=output.directIntensity;
    p->star_intensity=output.starIntensity;return 0;
}
}
MC_EnvironmentProfileV2 environmentDefinition() {
    MC_EnvironmentProfileV2 d{};d.size=sizeof(d);d.base.size=sizeof(d.base);d.base.dimension=1;d.base.key="official_heaven:environment";
    d.base.parameters.size=sizeof(d.base.parameters);d.base.parameters.dimension=1;d.base.parameters.sky_style=1;d.base.parameters.clouds=1;
    d.base.parameters.sun_size=d.base.parameters.moon_size=1;d.base.callback=evaluate;
    d.override_particle_intensity=1;d.particle_intensity=.80f;
    d.sky_flags=MC_SKY_NO_ATMOSPHERE|MC_SKY_NO_LOW_CLOUDS|MC_SKY_NO_VOXEL_CLOUDS|MC_SKY_NO_LIGHT_SHAFTS;
    d.grade_color[0]=1.018f;d.grade_color[1]=1.008f;d.grade_color[2]=.982f;d.grade_strength=.36f;
    d.sky.size=sizeof(d.sky);
    const float layers[MC_SKY_LAYER_VECTORS][4]={
        {.16f,.0022f,-.0008f,.82f},{-.42f,.04f,.40f,.72f},
        {.94f,.90f,.74f,0},{.70f,.86f,.96f,0},{2.18f,23,.64f,.36f},{.12f,0,0,0},
        {.75f,.0012f,-.0004f,.18f},{.58f,.82f,.04f,.42f},
        {.18f,.92f,.78f,0},{.66f,.38f,1,0},{1.3f,2.7f,11,.62f},{.55f,.18f,.04f,.05f}};
    for(size_t i=0;i<MC_SKY_LAYER_VECTORS;++i)std::copy(std::begin(layers[i]),std::end(layers[i]),d.sky.layers[i]);return d;
}
}
