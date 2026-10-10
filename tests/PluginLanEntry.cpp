#include "minecraftc/plugin.h"
#include <cstddef>
static const MC_Host* host;
static const MC_MultiplayerV1* multiplayer;
static int32_t gift(MC_PluginContext* c,MC_Event* e,void*) {
    if(e->size<sizeof(MC_Event)||e->role==MC_CLIENT)return -1;
    uint32_t role=0,dimension=0,count=0;
    MC_PlayerSnapshot snapshot{};snapshot.size=sizeof(snapshot);
    if(multiplayer->session_role(c,&role)||role!=e->role||multiplayer->players(c,nullptr,0,&count)||!count||
       multiplayer->player(c,e->player_id,&snapshot,&dimension)||dimension!=e->dimension)return -1;
    return multiplayer->give_item(c,e->player_id,host->find_item(c,"minecraftc:emerald"),2);
}
static int32_t dimensionBlock(MC_PluginContext* c,MC_Event*,void*) {
    return multiplayer->set_block(c,1,0,2,3,host->find_block(c,"official_content:crystal_block"));
}
static int32_t fault(MC_PluginContext*,MC_Event*,void*) {return -1;}
static int32_t leave(MC_PluginContext* c,MC_Event* e,void*) {return gift(c,e,nullptr);}
static int32_t load(const MC_Host* h,MC_PluginContext* c) {
    if(h->size<offsetof(MC_Host,query_extension)+sizeof(h->query_extension))return -1;
    host=h;multiplayer=static_cast<const MC_MultiplayerV1*>(h->query_extension(c,MC_MULTIPLAYER_EXTENSION,1));
    if(!multiplayer||multiplayer->size<sizeof(*multiplayer))return -1;
    if(multiplayer->register_command(c,"lan_fixture:gift",MC_COMMAND_ALLOW_GUEST,gift,nullptr))return -1;
    if(multiplayer->register_command(c,"lan_fixture:dimension",MC_COMMAND_ALLOW_GUEST,dimensionBlock,nullptr))return -1;
    if(h->register_command(c,"lan_fixture:fault",fault,nullptr))return -1;
    return h->subscribe(c,MC_PLAYER_LEAVE,0,leave,nullptr);
}
static const MC_Plugin plugin{sizeof(MC_Plugin),1,"lan_fixture","1.0.0",load,nullptr};
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==1?&plugin:nullptr;}
