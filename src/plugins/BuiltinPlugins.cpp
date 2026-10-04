#include "plugins/PluginManager.h"
#include "minecraftc/plugin.hpp"

namespace {
struct Demo {const MC_Host* host;MC_PluginContext* context;};
Demo contentDemo{}, atmosphereDemo{};
int32_t interact(MC_PluginContext* ctx,MC_Event* e,void* userdata) {
    const auto& demo=*static_cast<Demo*>(userdata);
    if(e->kind==MC_USE_PRE&&e->item==demo.host->find_item(ctx,"official_content:crystal")) {
        MC_PlayerSnapshot player{};player.size=sizeof(player);
        if(demo.host->player(ctx,&player))return -1;
        demo.host->log(ctx,1,"The crystal resonates.");e->cancelled=1;
    }
    return 0;
}
int32_t contentLoad(const MC_Host* host,MC_PluginContext* ctx) {
    contentDemo={host,ctx};MC_Material material{};material.size=sizeof(material);material.key="official_content:crystal";material.color[0]=0.26f;material.color[1]=0.82f;material.color[2]=0.95f;material.color[3]=1;
    if(host->register_material(ctx,&material))return -1;
    MC_Block block{};block.size=sizeof(block);block.key="official_content:crystal_block";block.name="Crystal Block";block.solid=1;block.hardness=1.5f;block.alpha=1;block.emission=9;block.drop_item="official_content:crystal_block";
    for(auto& face:block.materials)face=material.key;
    for(float& c:block.color)c=1;
    uint16_t id=0;if(host->register_block(ctx,&block,&id))return -1;
    MC_Item item{};item.size=sizeof(item);item.key=block.key;item.name=block.name;item.material=material.key;item.placed_block=block.key;item.kind=static_cast<uint32_t>(ItemKind::Block);item.category=0;item.max_stack=64;
    if(host->register_item(ctx,&item,&id))return -1;
    item.key="official_content:crystal";item.name="Resonant Crystal";item.placed_block=nullptr;item.kind=0;item.category=6;
    if(host->register_item(ctx,&item,&id))return -1;
    MC_Recipe recipe{};recipe.size=sizeof(recipe);recipe.key="official_content:crystal";recipe.width=1;recipe.height=1;recipe.mirror=1;recipe.count=4;recipe.ingredients[0]="minecraftc:diamond";recipe.output=item.key;
    if(host->register_recipe(ctx,&recipe))return -1;
    recipe.key="official_content:crystal_block";recipe.width=2;recipe.height=2;recipe.count=1;recipe.output=block.key;for(size_t i=0;i<4;++i)recipe.ingredients[i]=item.key;
    if(host->register_recipe(ctx,&recipe))return -1;
    return host->subscribe(ctx,MC_USE_PRE,0,interact,&contentDemo);
}
int32_t atmosphere(MC_PluginContext* ctx,MC_Event* e,void* userdata) {
    const auto& demo=*static_cast<Demo*>(userdata);
    if(e->kind==MC_ENVIRONMENT){e->environment.zenith[0]*=0.72f;e->environment.zenith[2]*=1.1f;e->environment.fog[0]*=0.8f;}
    if(e->kind==MC_HUD){const float c[4]={0.5f,0.95f,1,1};return demo.host->hud_text(ctx,12,e->screen_height-30,"Atmosphere plugin",c);}
    return 0;
}
int32_t atmosphereLoad(const MC_Host* host,MC_PluginContext* ctx) {
    atmosphereDemo={host,ctx};if(host->subscribe(ctx,MC_ENVIRONMENT,0,atmosphere,&atmosphereDemo))return -1;
    return host->subscribe(ctx,MC_HUD,0,atmosphere,&atmosphereDemo);
}
const MC_Plugin contentPlugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"official_content","1.0.0",contentLoad,nullptr};
const MC_Plugin atmospherePlugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"official_atmosphere","1.0.0",atmosphereLoad,nullptr};
}
namespace Plugins {
const MC_Plugin* builtinContent(uint32_t abi){return abi==MC_PLUGIN_ABI?&contentPlugin:nullptr;}
const MC_Plugin* builtinAtmosphere(uint32_t abi){return abi==MC_PLUGIN_ABI?&atmospherePlugin:nullptr;}
}
