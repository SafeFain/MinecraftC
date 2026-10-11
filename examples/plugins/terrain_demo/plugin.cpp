#include "minecraftc/plugin.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace {
uint16_t grass,stone,wood;
int height(const MC_GenerationContext* c,int x,int z) {
    int base=64;const char* value=std::strstr(c->parameters,"\"base_height\"");
    if(value){value=std::strchr(value,':');if(value)base=static_cast<int>(std::clamp(std::strtol(value+1,nullptr,10),-40L,260L));}
    const uint64_t h=c->seed^(uint64_t(static_cast<int64_t>(x))*0x9e3779b97f4a7c15ULL)^(uint64_t(static_cast<int64_t>(z))*0xbf58476d1ce4e5b9ULL);
    return base+static_cast<int>((h^(h>>27))%4);
}
int32_t column(const MC_GenerationContext* c,int x,int z,MC_SurfaceSample* out,void*) {out->height=height(c,x,z);out->water_level=-65;out->biome=1;return 0;}
int32_t voxel(const MC_GenerationContext* c,int x,int y,int z,uint16_t* out,void*) {const int h=height(c,x,z);*out=y>h?0:y==h?grass:stone;return 0;}
int32_t decorate(const MC_GenerationContext* c,int x,int z,MC_DecorationBlock* output,uint32_t capacity,uint32_t* count,void*) {
    if(capacity<2)return -1;
    *count=2;
    const int top=height(c,x+1,z-1);
    output[0]={x+1,top+1,z-1,wood};
    output[1]={x+1,top+2,z-1,wood};
    return 0;
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    minecraftc::Host host(api,context);const auto* generation=host.worldgen();if(!generation)return -1;
    grass=host.block("minecraftc:grass");stone=host.block("minecraftc:stone");wood=host.block("minecraftc:wood");if(grass==MC_INVALID_ID||stone==MC_INVALID_ID||wood==MC_INVALID_ID)return -1;
    MC_GeneratorDefinition g{sizeof(MC_GeneratorDefinition),"terrain_demo:plateau","Seeded Plateau","1",0,column,voxel,nullptr};
    MC_ModifierDefinition m{sizeof(MC_ModifierDefinition),"terrain_demo:markers","1",0,2,decorate,nullptr};
    return generation->register_generator(context,&g)||generation->register_modifier(context,&m)?-1:0;
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"terrain_demo","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
