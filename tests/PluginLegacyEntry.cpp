#include "fixtures/PluginAbi1.h"
#include <cstddef>
static const MC_Host* host;
static int32_t gift(MC_PluginContext* c,MC_Event*,void*) {return host->give_item(c,host->find_item(c,"minecraftc:diamond"),3);}
static int32_t use(MC_PluginContext* c,MC_Event* e,void*) {
    if(e->item!=host->find_item(c,"official_content:crystal"))return 0;
    MC_PlayerSnapshot p{};p.size=sizeof(p);if(host->player(c,&p))return -1;
    e->cancelled=1;return gift(c,e,nullptr);
}
static int32_t load(const MC_Host* h,MC_PluginContext* c) {
    if(h->abi!=1||h->size<sizeof(MC_Host))return -1;
    host=h;
    if(h->register_command(c,"legacy_fixture:gift",gift,nullptr))return -1;
    return h->subscribe(c,MC_USE_PRE,10,use,nullptr);
}
static const MC_Plugin plugin{sizeof(MC_Plugin),1,"legacy_fixture","1.0.0",load,nullptr};
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==1?&plugin:nullptr;}
