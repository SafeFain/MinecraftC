#include "plugins/PluginManager.h"
#include "minecraftc/plugin.hpp"
#include "plugins/OfficialHeavenContent.h"
#include "plugins/OfficialHeavenActions.h"
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

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

int32_t heavenLoad(const MC_Host* host,MC_PluginContext* ctx){
    Plugins::publishHeavenDimension();
    auto& manager=*static_cast<Plugins::PluginManager*>(ctx->manager);
    const auto* actions=static_cast<const MC_PlayerActionsV2*>(host->query_extension(ctx,MC_PLAYER_ACTIONS_EXTENSION,2));
    const auto* behaviors=static_cast<const MC_BehaviorsV2*>(host->query_extension(ctx,MC_BEHAVIORS_EXTENSION,2));
    if(!actions||!behaviors)return -1;
    MC_PlayerActionV2 a{};a.size=sizeof(a);a.base.size=sizeof(a.base);a.base.key="official_heaven:skyway";a.base.range=256;a.target=Plugins::skywayTarget;
    if(actions->register_action(ctx,&a))return -1;
    a.base.key="official_heaven:starstep";a.base.range=96;a.base.cooldown=3;a.base.durability=1;a.target=Plugins::starstepTarget;a.range_from_eye=a.tool_pose=a.invalid_marker=1;a.use_cooldown=.15f;
    if(actions->register_action(ctx,&a))return -1;
    bool failed=false;
    Plugins::heavenSemantics([&](const MC_BlockBehaviorV2& d){failed=behaviors->register_block(ctx,&d)!=0||failed;},[&](const MC_ItemBehaviorV2& d){failed=behaviors->register_item(ctx,&d)!=0||failed;});
    if(failed)return -1;
    MC_HeldModel model{};model.size=sizeof(model);const auto item=Plugins::itemKey(ItemId::STARSTEP_SCEPTER);model.item=item.c_str();model.path="starstep.gltf";
    const glm::mat4 first(1),third=glm::rotate(glm::mat4(1),glm::radians(-120.f),glm::vec3(1,0,0));
    for(int i=0;i<16;++i){model.first_person[i]=first[i/4][i%4];model.third_person[i]=third[i/4][i%4];}
    manager.heldModel(*ctx,model);return 0;
}
const MC_Plugin heavenPlugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"official_heaven","1.0.0",heavenLoad,nullptr};
const MC_Plugin* builtinHeaven(uint32_t abi){return abi==MC_PLUGIN_ABI?&heavenPlugin:nullptr;}
}
namespace Plugins {
const MC_Plugin* builtinContent(uint32_t abi){return abi==MC_PLUGIN_ABI?&contentPlugin:nullptr;}
const MC_Plugin* builtinAtmosphere(uint32_t abi){return abi==MC_PLUGIN_ABI?&atmospherePlugin:nullptr;}
const std::vector<BuiltinPluginDescriptor>& builtinPlugins(){
    static const std::vector<BuiltinPluginDescriptor> entries{
        {"official_content","Official Content Example","builtin-api1-content1","",false,false,builtinContent},
        {"official_atmosphere","Official Atmosphere Example","builtin-api1-content1","",true,false,builtinAtmosphere},
        {"official_heaven","Heaven","heaven-v9-content1-actions2","plugins/official_heaven",false,true,builtinHeaven}};
    return entries;
}
}
