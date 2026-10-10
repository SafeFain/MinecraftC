#pragma once
#include "plugin.h"
#include <cstddef>
namespace minecraftc {
class Host {
public:
    Host(const MC_Host* host, MC_PluginContext* context) : api(host), ctx(context) {}
    bool on(MC_EventKind kind, MC_EventCallback callback, void* userdata=nullptr, int32_t priority=0) const {
        return api->subscribe(ctx,static_cast<uint32_t>(kind),priority,callback,userdata)==0;
    }
    uint16_t block(const char* key) const { return api->find_block(ctx,key); }
    uint16_t item(const char* key) const { return api->find_item(ctx,key); }
    void log(const char* message) const { api->log(ctx,1,message); }
    const MC_MultiplayerV1* multiplayer() const {
        if(api->size<offsetof(MC_Host,query_extension)+sizeof(api->query_extension)||!api->query_extension)return nullptr;
        auto extension=static_cast<const MC_MultiplayerV1*>(api->query_extension(ctx,MC_MULTIPLAYER_EXTENSION,1));
        return extension&&extension->version==1&&extension->size>=sizeof(MC_MultiplayerV1)?extension:nullptr;
    }
    const MC_Host* api;
    MC_PluginContext* ctx;
};
}
