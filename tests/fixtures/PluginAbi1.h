#ifndef MINECRAFTC_PLUGIN_H
#define MINECRAFTC_PLUGIN_H
#include <stdint.h>
#ifdef _WIN32
#define MC_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MC_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define MC_PLUGIN_ABI 1u
#define MC_INVALID_ID 65535u
/* All strings are UTF-8 and borrowed for the call only. Host copies registration
   data. Callbacks and host functions must not throw across this C ABI.
   Host functions are main-thread only; other threads must not access a context. */
typedef struct MC_PluginContext MC_PluginContext;
typedef enum MC_EventKind {
    MC_REGISTER=0, MC_INITIALIZE, MC_WORLD_READY, MC_WORLD_CLOSE, MC_SHUTDOWN,
    MC_UPDATE_PRE, MC_UPDATE_POST, MC_BREAK_PRE, MC_BREAK_POST,
    MC_PLACE_PRE, MC_PLACE_POST, MC_USE_PRE, MC_USE_POST,
    MC_DAMAGE_PRE, MC_DAMAGE_POST, MC_ENVIRONMENT, MC_HUD, MC_COMMAND,
    MC_EVENT_COUNT
} MC_EventKind;
typedef struct MC_Environment {
    float zenith[3], horizon[3], fog[3], ambient_color[3], direct_color[3];
    float ambient_intensity, direct_intensity;
} MC_Environment;
typedef struct MC_Event {
    uint32_t size, kind, cancelled;
    double dt, player[3];
    int32_t x,y,z;
    uint16_t block, item;
    float damage;
    MC_Environment environment;
    float screen_width, screen_height;
    const char* command;
    const char* arguments;
} MC_Event;
typedef int32_t (*MC_EventCallback)(MC_PluginContext*, MC_Event*, void*);
typedef enum MC_ItemKind { MC_MATERIAL=0, MC_BLOCK_ITEM=1, MC_TOOL=2, MC_WEAPON=3, MC_ARMOR=4, MC_FOOD=5, MC_SPAWN_EGG=6 } MC_ItemKind;
typedef enum MC_Category { MC_BUILDING=0, MC_NATURE=1, MC_FUNCTIONAL=2, MC_TOOLS=3, MC_COMBAT=4, MC_FOODS=5, MC_MATERIALS=6, MC_SPAWN_EGGS=7 } MC_Category;
typedef enum MC_ToolKind { MC_NO_TOOL=0, MC_PICKAXE=1, MC_AXE=2, MC_SHOVEL=3, MC_HOE=4, MC_SWORD=5 } MC_ToolKind;
typedef enum MC_Tier { MC_NO_TIER=0, MC_WOOD=1, MC_STONE=2, MC_IRON=3, MC_GOLD=4, MC_DIAMOND=5 } MC_Tier;
typedef struct MC_Block {
    uint32_t size;
    const char* key;
    const char* name;
    float color[3], hardness, alpha;
    uint32_t solid, cross, layer; /* layer: 0 opaque, 1 cutout, 2 translucent */
    uint32_t preferred_tool, minimum_tier, unbreakable, emission;
    const char* materials[6]; /* top,bottom,front,back,right,left */
    const char* drop_item;
} MC_Block;
typedef struct MC_Item {
    uint32_t size;
    const char* key;
    const char* name;
    const char* material;
    const char* placed_block;
    uint32_t kind, category, max_stack, durability, tool, tier, food;
    float saturation, attack_damage, attack_speed;
} MC_Item;
typedef struct MC_Material {
    uint32_t size;
    const char* key;
    const char* image; /* relative plugin path, optional 16x16 PNG */
    const char* normal;
    const char* properties;
    float color[4]; /* used when image is absent */
    uint32_t replace;
} MC_Material;
typedef struct MC_Recipe {
    uint32_t size;
    const char* key;
    const char* target; /* explicit target for replace/remove */
    uint32_t operation; /* 0 add, 1 replace, 2 remove */
    uint32_t smelting, width, height, mirror, count, cook_ticks;
    const char* ingredients[9];
    const char* output;
} MC_Recipe;
typedef struct MC_PlayerSnapshot {
    uint32_t size;
    double position[3];
    float health;
    uint32_t mode;
} MC_PlayerSnapshot;
typedef struct MC_Host {
    uint32_t size, abi;
    void (*log)(MC_PluginContext*,uint32_t,const char*);
    const char* (*last_error)(MC_PluginContext*);
    int32_t (*register_block)(MC_PluginContext*,const MC_Block*,uint16_t*);
    int32_t (*register_item)(MC_PluginContext*,const MC_Item*,uint16_t*);
    int32_t (*register_material)(MC_PluginContext*,const MC_Material*);
    int32_t (*register_recipe)(MC_PluginContext*,const MC_Recipe*);
    int32_t (*subscribe)(MC_PluginContext*,uint32_t,int32_t,MC_EventCallback,void*);
    uint16_t (*find_block)(MC_PluginContext*,const char*);
    uint16_t (*find_item)(MC_PluginContext*,const char*);
    int32_t (*player)(MC_PluginContext*,MC_PlayerSnapshot*);
    int32_t (*get_block)(MC_PluginContext*,int32_t,int32_t,int32_t,uint16_t*);
    int32_t (*set_block)(MC_PluginContext*,int32_t,int32_t,int32_t,uint16_t);
    int32_t (*give_item)(MC_PluginContext*,uint16_t,uint32_t);
    int32_t (*register_command)(MC_PluginContext*,const char*,MC_EventCallback,void*);
    int32_t (*hud_rect)(MC_PluginContext*,float,float,float,float,const float*);
    int32_t (*hud_text)(MC_PluginContext*,float,float,const char*,const float*);
    int32_t (*hud_image)(MC_PluginContext*,float,float,float,float,const char*,const float*);
    int32_t (*config_read)(MC_PluginContext*,char*,uint32_t);
    int32_t (*config_write)(MC_PluginContext*,const char*);
    int32_t (*world_data_read)(MC_PluginContext*,char*,uint32_t);
    int32_t (*world_data_write)(MC_PluginContext*,const char*);
} MC_Host;
typedef struct MC_Plugin {
    uint32_t size, abi;
    const char* id;
    const char* version;
    int32_t (*load)(const MC_Host*,MC_PluginContext*);
    void (*unload)(MC_PluginContext*);
} MC_Plugin;
typedef const MC_Plugin* (*MC_PluginQuery)(uint32_t);
/* Export exactly: const MC_Plugin* MCPlugin_Query(uint32_t abi). */
#ifdef __cplusplus
}
#endif
#endif
