#include <minecraftc/plugin.hpp>
namespace {
const MC_Host* host=nullptr;
uint16_t brick=MC_INVALID_ID;
uint16_t crystal=MC_INVALID_ID;
int32_t use(MC_PluginContext* context,MC_Event* event,void*) {
    if(event->item!=crystal)return 0;
    event->cancelled=1;
    return host->give_item(context,brick,1);
}
int32_t gift(MC_PluginContext* context,MC_Event*,void*) {
    return host->give_item(context,brick,4);
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    host=api;
    const auto block=api->find_block(context,"package_demo:blue_brick");
    brick=api->find_item(context,"package_demo:blue_brick");
    crystal=api->find_item(context,"package_demo:crystal");
    if(block==MC_INVALID_ID||brick==MC_INVALID_ID||crystal==MC_INVALID_ID)return -1;
    if(api->subscribe(context,MC_USE_PRE,0,use,nullptr))return -1;
    return api->register_command(context,"package_demo:gift",gift,nullptr);
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"package_demo","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi) {
    return abi==MC_PLUGIN_ABI?&plugin:nullptr;
}
