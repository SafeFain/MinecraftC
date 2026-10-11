#include "minecraftc/plugin.hpp"
namespace {
const MC_Host* host=nullptr;
int32_t load(const MC_Host* api,MC_PluginContext* ctx) {
    host=api;minecraftc::Host h(api,ctx);const auto* shapes=h.blockShapes();if(!shapes)return -1;
    const char* keys[]={"block_shapes_demo:slab","block_shapes_demo:stair","block_shapes_demo:door","block_shapes_demo:button"};
    for(uint32_t kind=1;kind<=4;++kind) {
        MC_BlockFamily family{};family.size=sizeof(family);family.kind=kind;auto& b=family.block;
        b.size=sizeof(b);b.key=keys[kind-1];b.name="Demo shape";b.color[0]=.4f;b.color[1]=.7f;b.color[2]=1;
        b.hardness=1;b.alpha=1;b.solid=kind==MC_BUTTON_FAMILY?0:1;b.layer=kind==MC_DOOR_FAMILY?1:0;
        for(auto& material:b.materials)material="stone";
        const char* items[]={"block_shapes_demo:slab","block_shapes_demo:stair","block_shapes_demo:door","block_shapes_demo:button"};
        b.drop_item=items[kind-1];family.full_block="minecraftc:stone";family.manual_open=1;family.villager_open=1;family.release_ticks=7;family.arrow_trigger=1;
        uint16_t id;if(shapes->register_family(ctx,&family,&id))return -1;
        MC_BlockState state{};state.size=sizeof(state);uint16_t composed;
        if(shapes->state(ctx,id,&state)||shapes->compose(ctx,&state,&composed)||composed!=id)return -1;
        MC_Item item{};item.size=sizeof(item);item.key=items[kind-1];item.name="Demo shape";item.material="stone";item.placed_block=keys[kind-1];item.kind=MC_BLOCK_ITEM;item.category=MC_BUILDING;item.max_stack=64;
        if(api->register_item(ctx,&item,&id))return -1;
    }
    MC_Block box{};box.size=sizeof(box);box.key="block_shapes_demo:box";box.name="Fixed box model";
    box.hardness=1;box.alpha=1;box.solid=1;box.drop_item=box.key;
    for(float& color:box.color)color=1;
    for(auto& material:box.materials)material="stone";
    uint16_t id;if(api->register_block(ctx,&box,&id))return -1;
    const MC_BlockBox bounds{{0,0,0},{.5f,.5f,.5f}};
    MC_BlockGeometry geometry{};geometry.size=sizeof(geometry);geometry.block=box.key;
    geometry.collision=geometry.selection=geometry.model=&bounds;
    geometry.collision_count=geometry.selection_count=geometry.model_count=1;
    if(shapes->register_geometry(ctx,&geometry))return -1;
    MC_Item item{};item.size=sizeof(item);item.key=box.key;item.name=box.name;item.material="stone";item.placed_block=box.key;
    item.kind=MC_BLOCK_ITEM;item.category=MC_BUILDING;item.max_stack=64;
    return api->register_item(ctx,&item,&id);
}
void unload(MC_PluginContext*){host=nullptr;}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"block_shapes_demo","1.0.0",load,unload};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
