#include "minecraftc/plugin.hpp"
#include <cmath>
namespace {
int32_t behavior(MC_PluginContext*,uint32_t kind,const MC_EntitySnapshot* entity,MC_EntityIntent* intent,void*) {
    if(kind==MC_ENTITY_DECIDE){intent->movement[0]=std::cos(entity->age)*1.5f;intent->movement[2]=std::sin(entity->age)*1.5f;intent->state="{\"behavior\":\"circle\"}";}
    return 0;
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    minecraftc::Host h(api,context);const auto* entities=h.entities();if(!entities)return -1;
    MC_EntityDefinition definition{};definition.size=sizeof(definition);definition.key="entity_demo:grazer";definition.name="Circle Grazer";
    definition.template_type=1;definition.health=30;definition.size_xyz[0]=definition.size_xyz[2]=.8f;definition.size_xyz[1]=1.2f;definition.speed=1.5f;definition.attack_damage=2;definition.callback=behavior;definition.state_version=1;definition.natural_spawn=1;
    definition.drop_item="minecraftc:wheat";definition.drop_count=2;uint16_t id;if(entities->register_type(context,&definition,&id))return -1;
    definition.key="entity_demo:sturdy_pig";definition.name="Sturdy Pig";definition.target="minecraftc:entity_2";definition.template_type=2;definition.health=24;definition.callback=nullptr;definition.state_version=0;
    if(entities->register_type(context,&definition,&id))return -1;
    MC_Material material{};material.size=sizeof(material);material.key="entity_demo:egg_material";material.color[0]=.35f;material.color[1]=.8f;material.color[2]=.5f;material.color[3]=1;
    if(api->register_material(context,&material))return -1;
    MC_SpawnEggDefinition egg{};egg.size=sizeof(egg);egg.entity="entity_demo:grazer";egg.item.size=sizeof(egg.item);egg.item.key="entity_demo:grazer_egg";egg.item.name="Circle Grazer Spawn Egg";egg.item.material=material.key;egg.item.category=MC_SPAWN_EGGS;egg.item.max_stack=64;
    if(entities->register_spawn_egg(context,&egg,&id))return -1;
    return 0;
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"entity_demo","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
