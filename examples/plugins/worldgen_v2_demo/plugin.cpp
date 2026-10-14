#include "minecraftc/plugin.hpp"
#include <algorithm>
#include <cstdint>

namespace {
uint16_t stone, grass, crystal, wood;
int floor16(int x) { const int q=x/16; return x%16<0?q-1:q; }
int top(const MC_GenerationContext* c,int x,int z,int layer) {
    const uint64_t h=c->seed ^ (uint64_t(int64_t(x))*0x9e3779b97f4a7c15ULL) ^
        (uint64_t(int64_t(z))*0xbf58476d1ce4e5b9ULL);
    return -32+layer*64+int((h^(h>>27))%4);
}
bool feature(int x,int z) { return x-floor16(x)*16==1&&z-floor16(z)*16==1; }
int32_t column(const MC_GenerationContext* c,int x,int z,MC_SurfaceSample* out,void*) {
    out->height=top(c,x,z,4);out->water_level=-65;out->biome=1;return 0;
}
int32_t voxel(const MC_GenerationContext* c,int x,int y,int z,uint16_t* out,void*) {
    *out=0;
    for(int layer=0;layer<5;++layer) {
        const int t=top(c,x,z,layer);
        if(y>=t-6&&y<=t)*out=y==t?(feature(x,z)?crystal:grass):stone;
        if(feature(x,z)&&y>t&&y<=t+3)*out=crystal;
    }
    return 0;
}
int32_t batch(const MC_GenerationContext* c,const MC_GenerationVolume* v,uint16_t* out,uint32_t capacity,void*) {
    if(capacity!=v->width*v->height*v->depth)return -1;
    // Real bulk work: sample heights/features once per column, then fill runs.
    std::fill(out,out+capacity,uint16_t(0));
    for(uint32_t z=0;z<v->depth;++z)for(uint32_t x=0;x<v->width;++x) {
        const int wx=v->x+int(x),wz=v->z+int(z);const bool detail=feature(wx,wz);
        for(int layer=0;layer<5;++layer) {
            const int t=top(c,wx,wz,layer);
            for(int y=t-6;y<=t+(detail?3:0);++y)
                if(y>=v->y&&y<v->y+int(v->height))out[x+z*v->width+uint32_t(y-v->y)*v->width*v->depth]=
                    y<t?stone:y==t&&!detail?grass:crystal;
        }
    }
    return 0;
}
int32_t terrain(const MC_GenerationContext* c,const MC_LodCell* cell,MC_LodSpan* out,uint32_t capacity,uint32_t* count,void*) {
    if(capacity<10)return -1;
    *count=0;
    for(int layer=0;layer<5;++layer) {
        const int t=top(c,cell->x+int(cell->cell_size/2),cell->z+int(cell->cell_size/2),layer);
        out[(*count)++]={t-6,t-1,stone};out[(*count)++]={t,t,grass};
    }
    return 0;
}
int32_t decoration(const MC_GenerationContext* c,const MC_LodCell* cell,MC_LodSpan* out,uint32_t capacity,uint32_t* count,void*) {
    if(capacity<5)return -1;
    *count=0;
    // Fine cells retain a narrow crystal even when its center misses the stem.
    if(cell->cell_size>8)return 0;
    const int fx=floor16(cell->x)*16+1,fz=floor16(cell->z)*16+1;
    if(fx<cell->x||fx>=cell->x+int(cell->cell_size)||fz<cell->z||fz>=cell->z+int(cell->cell_size))return 0;
    for(int layer=0;layer<5;++layer) {
        const int t=top(c,cell->x+int(cell->cell_size/2),cell->z+int(cell->cell_size/2),layer);
        out[(*count)++]={t,t+3,crystal};
    }
    return 0;
}
int32_t markers(const MC_GenerationContext* c,int x,int z,MC_DecorationBlock* out,uint32_t capacity,uint32_t* count,void*) {
    if(capacity<2)return -1;
    const int t=top(c,x+1,z-1,2);*count=2;
    out[0]={x+1,t+1,z-1,wood};out[1]={x+1,t+2,z-1,wood};return 0;
}
int32_t markerLod(const MC_GenerationContext* c,const MC_LodCell* cell,MC_LodSpan* out,uint32_t capacity,uint32_t* count,void*) {
    if(capacity<1)return -1;
    *count=0;if(cell->cell_size>8)return 0;
    const int mx=floor16(cell->x)*16+1,mz=floor16(cell->z+1)*16-1;
    if(mx>=cell->x&&mx<cell->x+int(cell->cell_size)&&mz>=cell->z&&mz<cell->z+int(cell->cell_size)) {
        const int t=top(c,mx,mz,2);out[(*count)++]={t+1,t+2,wood};
    }
    return 0;
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    minecraftc::Host host(api,context);const auto* v2=host.worldgenV2();if(!v2)return -1;
    stone=host.block("minecraftc:stone");grass=host.block("minecraftc:grass");
    crystal=host.block("minecraftc:glass");wood=host.block("minecraftc:wood");
    MC_GeneratorDefinitionV2 g{};g.size=sizeof(g);
    g.base={sizeof(g.base),"worldgen_v2_demo:islands","Five layer islands","1",0,column,voxel,nullptr};
    g.lod_revision="1";g.batch=batch;g.terrain_lod=terrain;g.decoration_lod=decoration;
    MC_ModifierDefinitionV2 m{};m.size=sizeof(m);
    m.base={sizeof(m.base),"worldgen_v2_demo:markers","1",0,2,markers,nullptr};
    m.lod_revision="1";m.decoration_lod=markerLod;
#ifdef MC_WORLDGEN_BAD_REGISTRATION
    // Regression fixture: reject a missing LOD revision after the generator
    // registered successfully, testing the loader's transaction rollback.
    m.lod_revision=nullptr;
#endif
    return v2->register_generator(context,&g)||v2->register_modifier(context,&m)?-1:0;
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"worldgen_v2_demo","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi) {return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
