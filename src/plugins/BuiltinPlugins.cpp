#include "plugins/PluginManager.h"
#include "minecraftc/plugin.hpp"
#include "plugins/OfficialHeavenContent.h"
#include "plugins/official_heaven/HeavenPresentation.h"
#include "plugins/official_heaven/HeavenEnvironment.h"
#include "plugins/official_heaven/HeavenDefinitions.h"
#include "plugins/official_heaven/HeavenProvider.h"
#include "plugins/OfficialHeavenActions.h"
#include "plugins/ContentQueries.h"
#include "plugins/ContentMutation.h"
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#ifdef MINECRAFTC_CONTENT_PACKAGES_HEADER
#include MINECRAFTC_CONTENT_PACKAGES_HEADER
#endif

namespace {
struct Demo {const MC_Host* host;MC_PluginContext* context;};
Demo contentDemo{}, atmosphereDemo{};
int32_t interact(MC_PluginContext* ctx,MC_Event* e,void* userdata) {
    const auto& demo=*static_cast<Demo*>(userdata);
    if(e->kind==MC_USE_PRE&&e->item==demo.host->find_item(ctx,"official_content:crystal")) {
        MC_PlayerSnapshot player{};player.size=sizeof(player);
        if(demo.host->player(ctx,&player))return -1;
        demo.host->log(ctx,1,"The crystal resonates.");e->cancelled=1;
    }
    return 0;
}
int32_t contentLoad(const MC_Host* host,MC_PluginContext* ctx) {
    contentDemo={host,ctx};MC_Material material{};material.size=sizeof(material);material.key="official_content:crystal";material.color[0]=0.26f;material.color[1]=0.82f;material.color[2]=0.95f;material.color[3]=1;
    if(host->register_material(ctx,&material))return -1;
    MC_Block block{};block.size=sizeof(block);block.key="official_content:crystal_block";block.name="Crystal Block";block.solid=1;block.hardness=1.5f;block.alpha=1;block.emission=9;block.drop_item="official_content:crystal_block";
    for(auto& face:block.materials)face=material.key;
    for(float& c:block.color)c=1;
    uint16_t id=0;if(host->register_block(ctx,&block,&id))return -1;
    MC_Item item{};item.size=sizeof(item);item.key=block.key;item.name=block.name;item.material=material.key;item.placed_block=block.key;item.kind=static_cast<uint32_t>(ItemKind::Block);item.category=0;item.max_stack=64;
    if(host->register_item(ctx,&item,&id))return -1;
    item.key="official_content:crystal";item.name="Resonant Crystal";item.placed_block=nullptr;item.kind=0;item.category=6;
    if(host->register_item(ctx,&item,&id))return -1;
    MC_Recipe recipe{};recipe.size=sizeof(recipe);recipe.key="official_content:crystal";recipe.width=1;recipe.height=1;recipe.mirror=1;recipe.count=4;recipe.ingredients[0]="minecraftc:diamond";recipe.output=item.key;
    if(host->register_recipe(ctx,&recipe))return -1;
    recipe.key="official_content:crystal_block";recipe.width=2;recipe.height=2;recipe.count=1;recipe.output=block.key;for(size_t i=0;i<4;++i)recipe.ingredients[i]=item.key;
    if(host->register_recipe(ctx,&recipe))return -1;
    return host->subscribe(ctx,MC_USE_PRE,0,interact,&contentDemo);
}
int32_t atmosphere(MC_PluginContext* ctx,MC_Event* e,void* userdata) {
    const auto& demo=*static_cast<Demo*>(userdata);
    if(e->kind==MC_ENVIRONMENT){e->environment.zenith[0]*=0.72f;e->environment.zenith[2]*=1.1f;e->environment.fog[0]*=0.8f;}
    if(e->kind==MC_HUD){const float c[4]={0.5f,0.95f,1,1};return demo.host->hud_text(ctx,12,e->screen_height-30,"Atmosphere plugin",c);}
    return 0;
}
int32_t atmosphereLoad(const MC_Host* host,MC_PluginContext* ctx) {
    atmosphereDemo={host,ctx};if(host->subscribe(ctx,MC_ENVIRONMENT,0,atmosphere,&atmosphereDemo))return -1;
    return host->subscribe(ctx,MC_HUD,0,atmosphere,&atmosphereDemo);
}
const MC_Plugin contentPlugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"official_content","1.0.0",contentLoad,nullptr};
const MC_Plugin atmospherePlugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"official_atmosphere","1.0.0",atmosphereLoad,nullptr};

int32_t heavenLoad(const MC_Host* host,MC_PluginContext* ctx){
    auto& manager=*static_cast<Plugins::PluginManager*>(ctx->manager);
    const auto* dimensions=static_cast<const MC_DimensionsV3*>(host->query_extension(ctx,MC_DIMENSIONS_EXTENSION,3));
    const auto* localization=static_cast<const MC_LocalizationV1*>(host->query_extension(ctx,MC_LOCALIZATION_EXTENSION,1));
    if(!dimensions||!localization)return -1;
    MC_DimensionDefinitionV3 d{};d.size=sizeof(d);d.base.size=sizeof(d.base);d.base.base.size=sizeof(d.base.base);
    d.base.base.key="minecraftc:heaven";d.base.base.name="Heaven";d.base.base.revision="1";
    d.base.base.generator="official_heaven:heaven";d.base.base.generator_revision="9";
    d.base.base.weather=MC_WEATHER_CLEAR;d.base.base.disable_natural_spawning=1;
    d.base.base.death_target=d.base.base.void_target="minecraftc:overworld";
    d.base.sample_spawn=OfficialHeaven::sampleSpawn;d.name_key="official_heaven:dimension.heaven";d.seed_domain=OfficialHeaven::HeavenGenerator::SEED_DOMAIN;
    uint32_t id=MC_INVALID_DIMENSION;if(dimensions->register_dimension(ctx,&d,&id)||id!=1)return -1;
    OfficialHeaven::loadDefinitions(manager.plugins().at(ctx->index).root);
    if(localization->register_bundle(ctx,"lang"))return -1;
    MC_DimensionTravelEntry travel{sizeof(travel),0,1,"official_heaven:bed_travel","official_heaven:travel.heaven"};
    if(dimensions->register_bed_travel(ctx,&travel))return -1;
    const auto* actions=static_cast<const MC_PlayerActionsV3*>(host->query_extension(ctx,MC_PLAYER_ACTIONS_EXTENSION,3));
    const auto* behaviors=static_cast<const MC_BehaviorsV2*>(host->query_extension(ctx,MC_BEHAVIORS_EXTENSION,2));
    if(!actions||!behaviors)return -1;
    MC_PlayerActionV3 definition{};definition.size=sizeof(definition);auto& a=definition.base;a.size=sizeof(a);a.base.size=sizeof(a.base);a.base.key="official_heaven:skyway";a.base.range=256;definition.target=Plugins::skywayTargetV3;
    if(actions->register_action(ctx,&definition))return -1;
    a.base.key="official_heaven:starstep";a.base.range=96;a.base.cooldown=3;a.base.durability=1;definition.target=Plugins::starstepTargetV3;a.range_from_eye=a.tool_pose=a.invalid_marker=1;a.use_cooldown=.15f;
    if(actions->register_action(ctx,&definition))return -1;
    auto& dimension=Plugins::mutableContentState().extensions.dimensions.at(1);
    dimension.seedDomain=OfficialHeaven::HeavenGenerator::SEED_DOMAIN;
    dimension.generation={"official_heaven:heaven","9","{}",{}};
    dimension.spawn=MC_SPAWN_SAMPLED;dimension.sampleSpawn=OfficialHeaven::sampleSpawn;
    bool failed=false;
    const auto* worldgen=static_cast<const MC_WorldgenV3*>(host->query_extension(ctx,MC_WORLDGEN_EXTENSION,3));
    const auto* structures=static_cast<const MC_StructuresV3*>(host->query_extension(ctx,MC_STRUCTURES_EXTENSION,3));
    if(!worldgen||!structures)return -1;
    OfficialHeaven::describeGeneration([&](const auto& d){failed=worldgen->register_generator(ctx,&d)!=0||failed;},
        [&](const auto& d){failed=structures->v2->register_structure(ctx,&d)!=0||failed;failed=structures->set_coarse_lod_policy(ctx,d.key,1)!=0||failed;},
        [&](const auto& d){failed=structures->v2->v1->register_loot(ctx,&d)!=0||failed;});
    OfficialHeaven::installStructureAliases();
    Plugins::heavenSemantics([&](const MC_BlockBehaviorV2& d){failed=behaviors->register_block(ctx,&d)!=0||failed;},[&](const MC_ItemBehaviorV2& d){failed=behaviors->register_item(ctx,&d)!=0||failed;});
    if(failed)return -1;
    const auto* presentation=static_cast<const MC_PresentationV2*>(host->query_extension(ctx,MC_PRESENTATION_EXTENSION,2));
    if(!presentation)return -1;
    if(OfficialHeaven::registerAssets(host,ctx,manager.plugins().at(ctx->index).root))return -1;
    for(const auto& particle:OfficialHeaven::particleDefinitions())if(presentation->register_particle(ctx,&particle))return -1;
    const auto ambient=OfficialHeaven::ambientDefinition();if(presentation->register_ambient(ctx,&ambient))return -1;
    const auto* environments=static_cast<const MC_EnvironmentV2*>(host->query_extension(ctx,MC_ENVIRONMENT_EXTENSION,2));
    if(!environments)return -1;const auto environment=OfficialHeaven::environmentDefinition();
    if(environments->register_profile(ctx,&environment))return -1;
    const char* tracks[]={"audio/heaven_ether.wav","audio/heaven_sanctum.wav"};
    const MC_MusicPlaylist music{sizeof(music),1,0,1,"official_heaven:music",tracks,2,0};
    if(presentation->v1->register_playlist(ctx,&music))return -1;
    MC_HeldModel model{};model.size=sizeof(model);const auto item=Plugins::itemKey(ItemId::STARSTEP_SCEPTER);model.item=item.c_str();model.path="starstep.gltf";
    const glm::mat4 first(1),third=glm::rotate(glm::mat4(1),glm::radians(-120.f),glm::vec3(1,0,0));
    for(int i=0;i<16;++i){model.first_person[i]=first[i/4][i%4];model.third_person[i]=third[i/4][i%4];}
    manager.heldModel(*ctx,model);return 0;
}
const MC_Plugin heavenPlugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"official_heaven","1.0.0",heavenLoad,nullptr};
const MC_Plugin* builtinHeaven(uint32_t abi){return abi==MC_PLUGIN_ABI?&heavenPlugin:nullptr;}
}
namespace Plugins {
const MC_Plugin* builtinContent(uint32_t abi){return abi==MC_PLUGIN_ABI?&contentPlugin:nullptr;}
const MC_Plugin* builtinAtmosphere(uint32_t abi){return abi==MC_PLUGIN_ABI?&atmospherePlugin:nullptr;}
const std::vector<BuiltinPluginDescriptor>& builtinPlugins(){
    static const std::vector<BuiltinPluginDescriptor> entries=[] {
        std::vector<BuiltinPluginDescriptor> registered{
        {"official_content","Official Content Example","builtin-api1-content1","",false,false,builtinContent},
        {"official_atmosphere","Official Atmosphere Example","builtin-api1-content1","",true,false,builtinAtmosphere},
        {"official_heaven","Heaven","heaven-v9-content2-p1","plugins/official_heaven",false,true,builtinHeaven}};
#ifdef MINECRAFTC_CONTENT_PACKAGES_HEADER
        appendContentPackages(registered);
#endif
        return registered;
    }();
    return entries;
}
}
