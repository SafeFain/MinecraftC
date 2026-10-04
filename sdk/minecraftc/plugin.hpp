#pragma once
#include "plugin.h"
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
    const MC_Host* api;
    MC_PluginContext* ctx;
};
}
