#include "minecraftc/plugin.hpp"
namespace {
uint16_t grass,stone;
int height(const MC_GenerationContext* c){return 48+static_cast<int>(c->seed%4);}
int32_t column(const MC_GenerationContext* c,int32_t,int32_t,MC_SurfaceSample* out,void*) {
    out->height=height(c);out->water_level=-65;out->biome=1;return 0;
}
int32_t voxel(const MC_GenerationContext* c,int32_t,int32_t y,int32_t,uint16_t* out,void*) {
    const auto h=height(c);*out=y>h?0:y==h?grass:stone;return 0;
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    minecraftc::Host host(api,context);const auto* dimensions=host.dimensions();const auto* worldgen=host.worldgen();
    if(!dimensions||!worldgen)return -1;
    grass=host.block("minecraftc:grass");stone=host.block("minecraftc:stone");
    if(grass==MC_INVALID_ID||stone==MC_INVALID_ID)return -1;
    MC_DimensionDefinition d{};d.size=sizeof(d);d.key="dimension_demo:plateau";d.name="Plateau";d.revision="1";
    d.generator="dimension_demo:terrain";d.generator_revision="1";d.weather=MC_WEATHER_CLEAR;d.disable_natural_spawning=1;
    d.death_target="minecraftc:overworld";d.void_target="minecraftc:overworld";
    uint32_t id=MC_INVALID_DIMENSION;if(dimensions->register_dimension(context,&d,&id))return -1;
    MC_GeneratorDefinition g{sizeof(g),"dimension_demo:terrain","Plateau terrain","1",id,column,voxel,nullptr};
    return worldgen->register_generator(context,&g);
}
const MC_Plugin plugin{sizeof(plugin),MC_PLUGIN_ABI,"dimension_demo","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
