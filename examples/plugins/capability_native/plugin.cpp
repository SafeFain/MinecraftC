#include "minecraftc/plugin.hpp"
#include <cmath>
namespace {
uint16_t stoneId=0;
const MC_Host* api=nullptr;
MC_PluginContext* targetContext=nullptr;
int32_t column(const MC_GenerationContext*,int32_t,int32_t,MC_SurfaceSample* out,void*){out->height=64;out->water_level=-65;out->biome=1;return 0;}
int32_t voxel(const MC_GenerationContext*,int32_t,int32_t y,int32_t,uint16_t* out,void*){*out=y<=64?stoneId:0;return 0;}
uint16_t chestId=0;
int32_t build(const MC_StructureContext*,MC_StructureBlock* out,uint32_t cap,uint32_t* count,void*){if(cap<36)return -1;*count=0;for(int x=-17;x<=17;++x)out[(*count)++]={x,0,0,stoneId,nullptr};out[(*count)++]={17,1,0,chestId,"capability_native:loot"};return 0;}
int32_t target(const MC_ActionView* v,MC_ActionTarget* t,void*){t->dimension=v->dimension;t->valid=1;for(int i=0;i<3;++i)t->position[i]=v->position[i];t->position[0]+=2;return 0;}
int32_t targetV2(const MC_ActionViewV2* v,MC_ActionTargetV2* t,void*){
    if(v->size<sizeof(*v)||!v->layers||!v->block_flags||v->generation.dimension!=v->base.dimension)return -1;
    if(v->sneaking>1||v->source>1||v->phase>1||v->hit_valid>1)return -1;
    if(v->hit_valid){uint16_t block=0;if(v->base.block(v->base.reader,v->hit[0],v->hit[1],v->hit[2],&block)||block!=v->hit_block||std::abs(v->face[0])+std::abs(v->face[1])+std::abs(v->face[2])!=1)return -1;}
    if(api->give_item(targetContext,v->base.item,1)==0)return -1;
    MC_ActionLayer layers[384]{};uint32_t count=0;if(v->layers(v->base.reader,0,0,layers,384,&count)||!count)return -1;
    t->handled=1;t->base.valid=1;for(int i=0;i<3;++i)t->base.position[i]=v->base.position[i];t->base.position[0]+=v->sneaking?-2:2;return 0;
}
int32_t environment(MC_PluginContext*,MC_EnvironmentParameters* p,void*){p->star_intensity=.5f;p->colors.zenith[0]*=.6f;return 0;}
int32_t hud(MC_PluginContext* c,MC_Event* e,void*){const minecraftc::Host h(api,c);MC_PlayerSnapshot player{};player.size=sizeof(player);if(api->player(c,&player))return 0;MC_ParticleEmission p{};p.size=sizeof(p);p.dimension=e->dimension;p.local_only=1;p.count=1;p.particle="capability_native:spark";for(int i=0;i<3;++i)p.position[i]=player.position[i];p.position[1]+=1.5;h.presentation()->emit(c,&p);return 0;}
int32_t load(const MC_Host* host,MC_PluginContext* c){
    api=host;targetContext=c;stoneId=host->find_block(c,"minecraftc:stone");chestId=host->find_block(c,"minecraftc:chest");const minecraftc::Host h(host,c);const auto* behaviors=h.behaviors();const auto* structures=h.structures();const auto* actions=h.playerActions();const auto* dims=h.dimensions();const auto* gen=h.worldgen();const auto* env=h.environment();const auto* presentation=h.presentation();if(!behaviors||!structures||!actions||!dims||!gen||!env||!presentation)return -1;
    MC_Material mat{};mat.size=sizeof(mat);mat.key="capability_native:blue";mat.color[0]=.3f;mat.color[1]=.7f;mat.color[2]=mat.color[3]=1;if(host->register_material(c,&mat))return -1;
    MC_Block b{};b.size=sizeof(b);b.key="capability_native:vine";b.name="Native climbing vine";b.cross=1;b.layer=1;b.alpha=1;b.hardness=.5f;for(auto& m:b.materials)m=mat.key;uint16_t block=0;if(host->register_block(c,&b,&block))return -1;
    MC_BlockBehavior behavior{};behavior.size=sizeof(behavior);behavior.block=b.key;behavior.climbable=behavior.replaceable=1;if(behaviors->register_block(c,&behavior))return -1;
    MC_Item item{};item.size=sizeof(item);item.key="capability_native:staff";item.name="Native blink staff";item.material=mat.key;item.kind=MC_TOOL;item.max_stack=1;item.durability=64;uint16_t id;if(host->register_item(c,&item,&id))return -1;
    MC_PlayerAction a{};a.size=sizeof(a);a.key="capability_native:blink";a.range=32;a.cooldown=2;a.durability=1;a.target=target;if(actions->register_action(c,&a))return -1;
    MC_ItemBehavior ib{sizeof(ib),item.key,a.key,nullptr};if(behaviors->register_item(c,&ib))return -1;
    const auto* semantics=static_cast<const MC_BehaviorsV2*>(h.extension(MC_BEHAVIORS_EXTENSION,2));
    const auto* actions2=static_cast<const MC_PlayerActionsV2*>(h.extension(MC_PLAYER_ACTIONS_EXTENSION,2));if(!semantics||!actions2)return -1;
    item.key="capability_native:plank";item.name="Native semantic plank";item.kind=MC_MATERIAL;item.max_stack=64;item.durability=0;if(host->register_item(c,&item,&id))return -1;
    MC_ItemBehaviorV2 ip{};ip.size=sizeof(ip);ip.base={sizeof(ip.base),item.key,nullptr,nullptr};ip.fields=MC_ITEM_FUEL|MC_ITEM_TAGS;ip.tags=MC_TAG_PLANKS;if(semantics->register_item(c,&ip))return -1;
    b.key="capability_native:ore";b.name="Native semantic ore";b.cross=0;b.layer=0;b.solid=1;b.preferred_tool=MC_PICKAXE;b.minimum_tier=2;if(host->register_block(c,&b,&block))return -1;
    MC_BreakDrop drop{item.key,1,3,1,2};MC_BlockBehaviorV2 bp{};bp.size=sizeof(bp);bp.base.size=sizeof(bp.base);bp.base.block=b.key;bp.fields=MC_BLOCK_LIGHT|MC_BLOCK_TAGS|MC_BLOCK_DROPS;bp.light_dampening=3;bp.tags=MC_TAG_LEAVES|MC_TAG_FERTILE;bp.drops=&drop;bp.drop_count=1;if(semantics->register_block(c,&bp))return -1;
    item.key="capability_native:context_staff";item.name="Context staff";item.kind=MC_TOOL;item.max_stack=1;item.durability=64;if(host->register_item(c,&item,&id))return -1;
    MC_PlayerActionV2 action2{};action2.size=sizeof(action2);action2.base.size=sizeof(action2.base);action2.base.key="capability_native:context";action2.base.range=32;action2.target=targetV2;if(actions2->register_action(c,&action2))return -1;
    ib={sizeof(ib),item.key,action2.base.key,nullptr};if(behaviors->register_item(c,&ib))return -1;
    MC_DimensionDefinition dim{};dim.size=sizeof(dim);dim.key="capability_native:flat";dim.name="Native capability plateau";dim.revision="1";dim.generator="capability_native:terrain";dim.generator_revision="1";uint32_t dimension;if(dims->register_dimension(c,&dim,&dimension))return -1;
    MC_GeneratorDefinition g{sizeof(g),dim.generator,"Native plateau","1",dimension,column,voxel,nullptr};if(gen->register_generator(c,&g))return -1;
    MC_LootEntry entry{"minecraftc:coal",1,3,100};MC_LootTable loot{sizeof(loot),"capability_native:loot","1",&entry,1};if(structures->register_loot(c,&loot))return -1;
    MC_StructureDefinition s{};s.size=sizeof(s);s.dimension=dimension;s.key="capability_native:bridge";s.name="Native bridge";s.revision="1";s.cell_size=128;s.chance=100;s.radius=17;s.height=2;s.build=build;if(structures->register_structure(c,&s))return -1;
    MC_EnvironmentProfile profile{};profile.size=sizeof(profile);profile.dimension=dimension;profile.key="capability_native:sky";profile.parameters.size=sizeof(profile.parameters);profile.parameters.clouds=1;profile.parameters.sun_size=profile.parameters.moon_size=1;profile.callback=environment;if(env->register_profile(c,&profile))return -1;
    MC_ParticleDefinition particle{};particle.size=sizeof(particle);particle.key="capability_native:spark";particle.material=mat.key;for(float& v:particle.color)v=1;particle.lifetime=1;particle.scale=.1f;particle.velocity[1]=.2f;if(presentation->register_particle(c,&particle))return -1;
    item.key="capability_native:staff";
    MC_HeldModel model{};model.size=sizeof(model);model.item=item.key;model.path="staff.glb";for(int i=0;i<16;++i)model.first_person[i]=model.third_person[i]=i%5==0?1.0f:0.0f;if(presentation->register_held_model(c,&model))return -1;
    const char* tracks[]={"chime.wav"};MC_MusicPlaylist music{sizeof(music),dimension,0,1,"capability_native:music",tracks,1,2};if(presentation->register_playlist(c,&music))return -1;
    return h.on(MC_HUD,hud)?0:-1;
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"capability_native","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi){return abi==MC_PLUGIN_ABI?&plugin:nullptr;}
