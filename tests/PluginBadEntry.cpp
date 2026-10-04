#include "minecraftc/plugin.h"
#if defined(MC_CALLBACK_FAULT)
static int32_t fail(MC_PluginContext*,MC_Event*,void*) {return 1;}
static int32_t load(const MC_Host* host,MC_PluginContext* context) {
    return host->subscribe(context,MC_UPDATE_POST,0,fail,nullptr);
}
static const MC_Plugin plugin={sizeof(MC_Plugin),MC_PLUGIN_ABI,"callback_fault","1.0.0",load,nullptr};
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t) {return &plugin;}
#elif defined(MC_BAD_ABI)
static const MC_Plugin plugin={sizeof(MC_Plugin),999,"bad_abi","1.0.0",0,0};
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t) {return &plugin;}
#else
extern "C" MC_PLUGIN_EXPORT int NoPluginEntry() {return 1;}
#endif
