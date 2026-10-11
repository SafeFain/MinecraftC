#pragma once
#include "plugin.h"
#include "extensions.h"
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
    const void* extension(const char* key,uint32_t version=1) const {
        if(api->size<offsetof(MC_Host,query_extension)+sizeof(api->query_extension)||!api->query_extension)return nullptr;
        return api->query_extension(ctx,key,version);
    }
    const MC_BehaviorsV1* behaviors() const {return static_cast<const MC_BehaviorsV1*>(extension(MC_BEHAVIORS_EXTENSION));}
    const MC_StructuresV1* structures() const {return static_cast<const MC_StructuresV1*>(extension(MC_STRUCTURES_EXTENSION));}
    const MC_PlayerActionsV1* playerActions() const {return static_cast<const MC_PlayerActionsV1*>(extension(MC_PLAYER_ACTIONS_EXTENSION));}
    const MC_EnvironmentV1* environment() const {return static_cast<const MC_EnvironmentV1*>(extension(MC_ENVIRONMENT_EXTENSION));}
    const MC_PresentationV1* presentation() const {return static_cast<const MC_PresentationV1*>(extension(MC_PRESENTATION_EXTENSION));}
    const MC_BlockShapesV1* blockShapes() const {return static_cast<const MC_BlockShapesV1*>(extension(MC_BLOCK_SHAPES_EXTENSION));}
    const MC_EntitiesV1* entities() const {return static_cast<const MC_EntitiesV1*>(extension(MC_ENTITIES_EXTENSION));}
    const MC_DimensionsV1* dimensions() const {return static_cast<const MC_DimensionsV1*>(extension(MC_DIMENSIONS_EXTENSION));}
    const MC_WorldgenV1* worldgen() const {return static_cast<const MC_WorldgenV1*>(extension(MC_WORLDGEN_EXTENSION));}
    const MC_RenderV1* render() const {return static_cast<const MC_RenderV1*>(extension(MC_RENDER_EXTENSION));}
    const MC_MultiplayerV1* multiplayer() const {
        if(api->size<offsetof(MC_Host,query_extension)+sizeof(api->query_extension)||!api->query_extension)return nullptr;
        auto extension=static_cast<const MC_MultiplayerV1*>(api->query_extension(ctx,MC_MULTIPLAYER_EXTENSION,1));
        return extension&&extension->version==1&&extension->size>=sizeof(MC_MultiplayerV1)?extension:nullptr;
    }
    const MC_Host* api;
    MC_PluginContext* ctx;
};
}
