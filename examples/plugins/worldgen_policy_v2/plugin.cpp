#include "minecraftc/plugin.hpp"
#include <cstring>

namespace {
uint16_t stone,chest;
int32_t spawn(const MC_GenerationContext* c,double* out,void*) {
    out[0]=double(c->seed%4)*16+.5;out[1]=101.01;out[2]=.5;
    return std::strcmp(c->parameters,"{\"layer\":2}")==0?0:-1;
}
int32_t column(const MC_GenerationContext*,int32_t,int32_t,MC_SurfaceSample* out,void*) {
    out->height=240;out->water_level=-65;out->biome=1;return 0;
}
int32_t voxel(const MC_GenerationContext*,int32_t,int32_t y,int32_t,uint16_t* out,void*) {
    *out=(y==100||y==240)?stone:0;return 0;
}
int32_t placements(const MC_StructureContext* c,int32_t x,int32_t z,MC_StructureInstance* out,uint32_t cap,uint32_t* count,void*) {
    if(cap<2)return -1;
    *count=2;
    out[0]={{x*64+16,180,z*64+16},c->generation.seed,1};
    out[1]={{x*64+16,180,z*64+16},c->generation.seed+1,0};
    return 0;
}
int32_t build(const MC_StructureContext* c,MC_StructureBlockV2* out,uint32_t cap,uint32_t* count,void*) {
    if(cap<6)return -1;
    *count=6;
    out[0]={{-1,-88,0,(c->variant&1)?uint16_t(0):stone,nullptr},0};
    out[1]={{0,88,0,stone,nullptr},0};
    out[2]={{0,1,0,chest,"worldgen_policy_v2:loot"},123};
    out[3]={{0,1,0,0,nullptr},0};
    out[4]={{1,1,0,chest,"worldgen_policy_v2:loot"},123};
    out[5]={{1,1,0,chest,"worldgen_policy_v2:loot"},0};
    return 0;
}
int32_t load(const MC_Host* api,MC_PluginContext* c) {
    minecraftc::Host host(api,c);
    const auto* d=static_cast<const MC_DimensionsV2*>(host.extension(MC_DIMENSIONS_EXTENSION,2));
    const auto* s=static_cast<const MC_StructuresV2*>(host.extension(MC_STRUCTURES_EXTENSION,2));
    if(!d||!s||!d->v1||!s->v1)return -1;
    stone=host.block("minecraftc:stone");chest=host.block("minecraftc:chest");
    MC_DimensionDefinitionV2 def{};def.size=sizeof(def);def.base.size=sizeof(def.base);
    def.base.key="worldgen_policy_v2:layered";def.base.name="Layered";def.base.revision="1";
    def.base.generator="worldgen_policy_v2:terrain";def.base.generator_revision="1";
    def.base.parameters="{\"layer\":2}";def.base.weather=MC_WEATHER_CLEAR;def.base.disable_natural_spawning=1;
    def.sample_spawn=spawn;uint32_t id=MC_INVALID_DIMENSION;
    if(d->register_dimension(c,&def,&id))return -1;
    MC_GeneratorDefinition g{sizeof(g),def.base.generator,"Layered terrain","1",id,column,voxel,nullptr};
    if(host.worldgen()->register_generator(c,&g))return -1;
    MC_LootEntry entry{"minecraftc:coal",2,2,100};
    MC_LootTable loot{sizeof(loot),"worldgen_policy_v2:loot","1",&entry,1};
    if(s->v1->register_loot(c,&loot))return -1;
    MC_StructureDefinitionV2 structure{};structure.size=sizeof(structure);structure.dimension=id;
    structure.key="worldgen_policy_v2:shrine";structure.name="Layered shrine";structure.revision="1";
    structure.cell_size=64;structure.radius=2;structure.min_y=-88;structure.max_y=90;
    structure.placements=placements;structure.build=build;
    if(s->register_structure(c,&structure))return -1;
#ifdef MC_POLICY_BAD_REGISTRATION
    structure.key="worldgen_policy_v2:invalid";structure.min_y=-384;
    s->register_structure(c,&structure);
#endif
    return 0;
}
const MC_Plugin plugin{sizeof(plugin),MC_PLUGIN_ABI,"worldgen_policy_v2","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
