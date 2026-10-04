#include "minecraftc/plugin.hpp"

namespace {
const MC_Host* api=nullptr;
float cooldown=0;
int32_t callback(MC_PluginContext* context,MC_Event* event,void*) {
    if(event->kind==MC_UPDATE_POST)cooldown=cooldown>event->dt?cooldown-static_cast<float>(event->dt):0;
    if(event->kind==MC_USE_PRE&&event->item==api->find_item(context,"native_demo:crystal")) {
        event->cancelled=1;
        if(cooldown==0){cooldown=1;return api->give_item(context,api->find_item(context,"minecraftc:bread"),1);}
    }
    if(event->kind==MC_ENVIRONMENT)event->environment.fog[2]*=1.1f;
    if(event->kind==MC_HUD){const float tint[4]={.4f,.9f,1,1};return api->hud_text(context,12,40,"Native Demo",tint);}
    if(event->kind==MC_COMMAND)return api->give_item(context,api->find_item(context,"native_demo:crystal"),1);
    return 0;
}
int32_t load(const MC_Host* host,MC_PluginContext* context) {
    if(!host||host->abi!=MC_PLUGIN_ABI||host->size<sizeof(MC_Host))return -1;
    api=host;cooldown=0;
    MC_Material material{};material.size=sizeof(material);material.key="native_demo:crystal";material.color[0]=.4f;material.color[1]=.9f;material.color[2]=1;material.color[3]=1;
    if(api->register_material(context,&material))return -1;
    MC_Item item{};item.size=sizeof(item);item.key="native_demo:crystal";item.name="Demo Crystal";item.material=material.key;item.max_stack=64;item.category=6;
    uint16_t id=0;if(api->register_item(context,&item,&id))return -1;
    const uint32_t events[]={MC_UPDATE_POST,MC_USE_PRE,MC_ENVIRONMENT,MC_HUD};
    for(uint32_t kind:events)
        if(api->subscribe(context,kind,0,callback,nullptr))return -1;
    return api->register_command(context,"native_demo:gift",callback,nullptr);
}
void unload(MC_PluginContext*){api=nullptr;}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"native_demo","1.0.0",load,unload};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
