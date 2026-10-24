#include "plugins/PluginManager.h"
#include "Config.h"
#include "model/GltfLoader.h"
#include "plugins/EnvironmentPresentation.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <nlohmann/json.hpp>

namespace Plugins {
namespace {
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
std::string str(const char* s){require(s&&*s,"Missing capability key");return s;}
std::string optional(const char* s){return s?s:"";}
void number(float n,float lo,float hi){require(std::isfinite(n)&&n>=lo&&n<=hi,"Capability number out of range");}
PluginManager& manager(MC_PluginContext* c){return *static_cast<PluginManager*>(c->manager);}
template<class F> int32_t call(MC_PluginContext* c,F f,bool registration=false){
    if(!c||!c->manager||!manager(c).hostThread())return -1;
    try{c->error.clear();f();return 0;}catch(const std::exception& e){c->error=e.what();if(registration)c->registrationFailed=true;return -1;}
}
void position(const double* p){require(p,"Missing position");for(int i=0;i<3;++i)require(std::isfinite(p[i])&&std::abs(p[i])<=30000000,"Invalid position");}
void actor(uint64_t id){require(context().player==0||context().player==id,"Guest cannot act for another player");}
}
void PluginManager::reference(MC_PluginContext& c,const std::string& key) const {
    require(validKey(key),"Invalid capability reference");const auto space=key.substr(0,key.find(':'));
    if(space=="minecraftc"||space==m_info.at(c.index).manifest.id)return;
    const auto& deps=m_info.at(c.index).manifest.dependencies;
    require(std::any_of(deps.begin(),deps.end(),[&](const Dependency& d){return d.id==space;}),"Cross-package reference requires dependency");
}
void PluginManager::blockBehavior(MC_PluginContext& c,const MC_BlockBehavior& d){
    require(d.size>=sizeof(d),"Invalid block behavior size");owned(c,str(d.block));
    const auto id=static_cast<uint16_t>(resolveBlock(d.block));
    require(d.climbable<=1&&d.replaceable<=1&&d.support<=2&&d.fire_encouragement<=100&&d.fire_flammability<=100&&d.harvest_count<=64&&d.harvest_reset_ticks<=72000,"Invalid block behavior");number(d.contact_damage,0,1000);
    BlockBehaviorContent b;b.climbable=d.climbable;b.replaceable=d.replaceable;b.support=d.support;b.encouragement=d.fire_encouragement;b.flammability=d.fire_flammability;b.damage=d.contact_damage;
    b.harvest=optional(d.harvest_item);b.harvestCount=d.harvest_count;b.replacement=optional(d.harvest_replacement);b.resetTicks=d.harvest_reset_ticks;b.action=optional(d.action);
    for(const auto& key:{b.harvest,b.replacement,b.action})if(!key.empty())reference(c,key);
    require((b.harvest.empty()&&b.harvestCount==0&&b.replacement.empty()&&b.resetTicks==0)||(!b.harvest.empty()&&b.harvestCount&& !b.replacement.empty()),"Harvest needs item, count and replacement");
    require(content().extensions.blockBehaviors.emplace(id,std::move(b)).second,"Duplicate block behavior");
}
void PluginManager::itemBehavior(MC_PluginContext& c,const MC_ItemBehavior& d){
    require(d.size>=sizeof(d),"Invalid item behavior size");owned(c,str(d.item));
    ItemBehaviorContent b{optional(d.action),optional(d.dimension)};
    if(!b.action.empty())reference(c,b.action);
    if(!b.dimension.empty())reference(c,b.dimension);
    require(content().extensions.itemBehaviors.emplace(static_cast<uint16_t>(resolveItem(d.item)),b).second,"Duplicate item behavior");
}
void PluginManager::blockBehaviorV2(MC_PluginContext& c,const MC_BlockBehaviorV2& d){
    require(d.size>=sizeof(d)&&d.fields<=15&&d.light_dampening<=15&&d.tags<=511&&d.substrate_tags<=511&&d.drop_count<=16&&(!d.drop_count||d.drops),"Invalid block semantics");
    std::vector<BlockBehaviorContent::Drop> drops;
    for(uint32_t i=0;i<d.drop_count;++i){const auto& r=d.drops[i];reference(c,str(r.item));require(r.minimum<=r.maximum&&r.maximum<=64&&r.denominator&&r.numerator<=r.denominator,"Invalid break drop");drops.push_back({r.item,r.minimum,r.maximum,r.numerator,r.denominator});}
    blockBehavior(c,d.base);
    auto& b=content().extensions.blockBehaviors.at(static_cast<uint16_t>(resolveBlock(d.base.block)));
    b.fields=d.fields;b.light=d.light_dampening;b.tags=d.tags;b.substrate=d.substrate_tags;b.drops=std::move(drops);
}
void PluginManager::itemBehaviorV2(MC_PluginContext& c,const MC_ItemBehaviorV2& d){
    require(d.size>=sizeof(d)&&d.fields<=3&&d.fuel_ticks<=65535&&d.tags<=255,"Invalid item semantics");
    itemBehavior(c,d.base);auto& b=content().extensions.itemBehaviors.at(static_cast<uint16_t>(resolveItem(d.base.item)));
    b.fields=d.fields;b.fuel=d.fuel_ticks;b.tags=d.tags;
}
void PluginManager::lootTable(MC_PluginContext& c,const MC_LootTable& d){
    require(d.size>=sizeof(d)&&d.entries&&d.count&&d.count<=256,"Invalid loot table");owned(c,str(d.key));
    LootContent l;l.key=d.key;l.revision=str(d.revision);require(l.revision.size()<=128,"Loot revision too long");
    for(uint32_t i=0;i<d.count;++i){const auto& e=d.entries[i];const auto key=str(e.item);reference(c,key);require(e.minimum<=e.maximum&&e.maximum<=64&&e.chance<=100,"Invalid loot entry");l.entries.push_back({key,e.minimum,e.maximum,e.chance});}
    require(content().extensions.loot.size()<256,"Too many loot tables");
    require(content().extensions.loot.emplace(l.key,std::move(l)).second,"Duplicate loot table");
}
void PluginManager::structure(MC_PluginContext& c,const MC_StructureDefinition& d){
    require(d.size>=sizeof(d),"Invalid structure size");owned(c,str(d.key));
    require(validDimension(d.dimension)&&d.cell_size>=16&&d.cell_size<=1024&&d.chance<=100&&d.radius<=64&&d.height&&d.height<=128&&d.terrain_tolerance<=128,"Invalid structure bounds/policy");
    require((d.build&&!d.count)||(!d.build&&d.blocks&&d.count&&d.count<=MC_MAX_STRUCTURE_BLOCKS),"Structure needs template or callback");
    reference(c,dimensionContent(d.dimension).key);
    StructureContent s;s.namespaces={"minecraftc",m_info.at(c.index).manifest.id};for(const auto& dep:m_info.at(c.index).manifest.dependencies)s.namespaces.push_back(dep.id);s.key=d.key;s.name=str(d.name);s.revision=str(d.revision);require(s.name.size()<=256&&s.revision.size()<=128,"Structure string too long");
    s.dimension=d.dimension;s.cell=d.cell_size;s.chance=d.chance;s.radius=d.radius;s.height=d.height;s.tolerance=d.terrain_tolerance;s.domain=d.seed_domain;s.build=d.build;s.userdata=d.userdata;
    std::set<std::tuple<int32_t,int32_t,int32_t>> positions;
    for(uint32_t i=0;i<d.count;++i){const auto& b=d.blocks[i];require(positions.emplace(b.x,b.y,b.z).second,"Duplicate structure voxel");require(std::abs(int64_t(b.x))<=s.radius&&std::abs(int64_t(b.z))<=s.radius&&b.y>=0&&uint32_t(b.y)<s.height&&validBlock(static_cast<BlockId>(b.block)),"Template block outside bounds");std::string loot=optional(b.loot);reference(c,blockKey(static_cast<BlockId>(b.block)));if(!loot.empty()){reference(c,loot);require(b.block==static_cast<uint16_t>(BlockId::CHEST),"Loot requires chest");}s.blocks.push_back({b.x,b.y,b.z,b.block,loot});}
    require(content().extensions.structures.size()<128,"Too many structures");
    require(content().extensions.structures.emplace(s.key,std::move(s)).second,"Duplicate structure");
}
void PluginManager::structureV2(MC_PluginContext& c,const MC_StructureDefinitionV2& d){
    require(d.size>=sizeof(d)&&d.placements&&d.build,"Structure V2 requires placement and build callbacks");
    owned(c,str(d.key));reference(c,dimensionContent(d.dimension).key);
    require(d.cell_size>=16&&d.cell_size<=1024&&d.radius<=64&&
        d.min_y>=1-Config::CHUNK_SIZE_Y&&d.max_y<Config::CHUNK_SIZE_Y&&d.min_y<=d.max_y&&
        int64_t(d.max_y)-d.min_y<Config::CHUNK_SIZE_Y,"Invalid structure V2 bounds");
    StructureContent s;s.key=d.key;s.name=str(d.name);s.revision=str(d.revision);
    require(s.name.size()<=256&&s.revision.size()<=128,"Structure string too long");
    s.namespaces={"minecraftc",m_info.at(c.index).manifest.id};
    for(const auto& dep:m_info.at(c.index).manifest.dependencies)s.namespaces.push_back(dep.id);
    s.dimension=d.dimension;s.cell=d.cell_size;s.radius=d.radius;s.minY=d.min_y;s.maxY=d.max_y;s.order=d.order;
    s.placements=d.placements;s.buildV2=d.build;s.userdata=d.userdata;
    require(content().extensions.structures.size()<128,"Too many structures");
    require(content().extensions.structures.emplace(s.key,std::move(s)).second,"Duplicate structure");
}
void PluginManager::structureCoarseLod(MC_PluginContext& c,const char* key,uint32_t policy) {
    owned(c,str(key));require(policy<=1,"Invalid structure coarse LOD policy");
    auto found=content().extensions.structures.find(key);require(found!=content().extensions.structures.end(),"Unknown structure LOD binding");
    found->second.omitCoarseLod=policy!=0;
}
void PluginManager::playerAction(MC_PluginContext& c,const MC_PlayerAction& d){
    require(d.size>=sizeof(d)&&d.kind<=MC_ACTION_TRAVEL&&d.durability<=65535,"Invalid action");owned(c,str(d.key));number(d.range,.1f,256);number(d.cooldown,0,3600);
    ActionContent a;a.key=d.key;a.dimension=optional(d.dimension);a.kind=d.kind;a.range=d.range;a.cooldown=d.cooldown;a.durability=d.durability;
    require(d.kind!=MC_ACTION_TRAVEL||!a.dimension.empty(),"Travel requires target dimension");if(!a.dimension.empty())reference(c,a.dimension);
    require(d.kind!=MC_ACTION_TRAVEL||!d.target,"Travel uses dimension entry policy");
    if(d.target){const auto f=d.target;const auto u=d.userdata;const auto index=c.index;a.target=[this,f,u,index](const MC_ActionView& v,MC_ActionTarget& t){if(!m_info[index].active)return false;const auto previous=m_phase;const auto readonly=m_readOnly;m_phase=MC_HUD;m_readOnly=true;bool ok=false;try{if(f(&v,&t,u))callbackFault(index,"Action target callback failed");else ok=true;}catch(...){callbackFault(index,"Exception across action ABI");}m_phase=previous;m_readOnly=readonly;return ok;};}
    require(content().extensions.actions.size()<256,"Too many player actions");
    require(content().extensions.actions.emplace(a.key,std::move(a)).second,"Duplicate action");
}
void PluginManager::playerActionV2(MC_PluginContext& c,const MC_PlayerActionV2& d){
    require(d.size>=sizeof(d)&&d.target&&!d.base.target&&d.base.kind==MC_ACTION_TELEPORT&&d.range_from_eye<=1&&d.tool_pose<=1&&d.invalid_marker<=1,"Invalid action V2");number(d.use_cooldown,0,3600);
    playerAction(c,d.base);auto& a=content().extensions.actions.at(d.base.key);
    a.rangeFromEye=d.range_from_eye!=0;a.toolPose=d.tool_pose!=0;a.invalidMarker=d.invalid_marker!=0;a.useCooldown=d.use_cooldown;
    const auto f=d.target;const auto u=d.base.userdata;const auto index=c.index;
    a.targetV2=[this,f,u,index](const MC_ActionViewV2& v,MC_ActionTargetV2& t){if(!m_info[index].active)return false;const auto phase=m_phase;const auto readonly=m_readOnly;m_phase=MC_HUD;m_readOnly=true;bool ok=false;try{if(f(&v,&t,u))callbackFault(index,"Action V2 callback failed");else ok=true;}catch(...){callbackFault(index,"Exception across action V2 ABI");}m_phase=phase;m_readOnly=readonly;return ok;};
}
void PluginManager::playerActionV3(MC_PluginContext& c,const MC_PlayerActionV3& d){
    require(d.size>=sizeof(d)&&d.target&&!d.base.target,"Invalid action V3");
    auto policy=d.base;policy.target=[](const MC_ActionViewV2*,MC_ActionTargetV2*,void*)->int32_t{return -1;};
    playerActionV2(c,policy);auto& a=content().extensions.actions.at(d.base.base.key);a.targetV2={};
    const auto f=d.target;const auto u=d.base.base.userdata;const auto index=c.index;
    a.targetV3=[this,f,u,index](const MC_ActionViewV3& v,MC_ActionTargetV2& t){
        if(!m_info[index].active)return false;
        const auto phase=m_phase;const auto readonly=m_readOnly;m_phase=MC_HUD;m_readOnly=true;bool ok=false;
        try{if(f(&v,&t,u))callbackFault(index,"Action V3 callback failed");else ok=true;}catch(...){callbackFault(index,"Exception across action V3 ABI");}
        m_phase=phase;m_readOnly=readonly;return ok;
    };
}
void PluginManager::environmentProfile(MC_PluginContext& c,const MC_EnvironmentProfile& d){
    require(d.size>=sizeof(d)&&d.parameters.size>=sizeof(d.parameters)&&validDimension(d.dimension),"Invalid environment profile");
    require(m_phase==MC_REGISTER&&!content().frozen,"Environment registration closed");const auto key=str(d.key);require(validKey(key)&&key.rfind(m_info.at(c.index).manifest.id+":",0)==0,"Environment key must use own namespace");
    const auto& p=d.parameters;require(p.sky_style<=1&&p.clouds<=1,"Invalid sky/cloud policy");number(p.sun_size,0,8);number(p.moon_size,0,8);
    auto validated=p;
    if(!(d.override_parameters&1)){for(auto* color:{validated.colors.zenith,validated.colors.horizon,validated.colors.fog,validated.colors.ambient_color,validated.colors.direct_color})std::fill(color,color+3,1);validated.colors.ambient_intensity=validated.colors.direct_intensity=1;}
    if(!(d.override_parameters&2)){validated.sun_direction[1]=validated.light_direction[1]=1;validated.moon_direction[1]=-1;}
    require(validEnvironment(validated),"Invalid static environment values");
    EnvironmentContent e;e.key=key;e.parameters=p;e.parameters.dimension=d.dimension;e.overrides=d.override_parameters;require(e.overrides<=7,"Invalid environment overrides");
    // Compatibility for the V1 layered-style selector; V2 supplies explicit flags.
    if(p.sky_style==1)e.skyFlags=MC_SKY_NO_ATMOSPHERE|MC_SKY_NO_LOW_CLOUDS|MC_SKY_NO_VOXEL_CLOUDS|MC_SKY_NO_LIGHT_SHAFTS;
    if(d.callback){const auto f=d.callback;const auto u=d.userdata;const auto index=c.index;auto* ctx=&c;e.callback=[this,f,u,index,ctx](MC_EnvironmentParameters& p){if(!m_info[index].active)return;const auto previous=m_phase;m_phase=MC_ENVIRONMENT;try{if(f(ctx,&p,u))callbackFault(index,"Environment callback failed");}catch(...){callbackFault(index,"Exception across environment ABI");}m_phase=previous;};}
    require(content().extensions.environments.emplace(d.dimension,std::move(e)).second,"Conflicting dimension environment");
}
void PluginManager::environmentProfileV2(MC_PluginContext& c,const MC_EnvironmentProfileV2& d) {
    require(d.size>=sizeof(d)&&d.sky.size>=sizeof(d.sky),"Invalid environment V2 descriptor");
    for(const auto& v:d.sky.layers)for(float value:v)number(value,-1024,1024);
    for(int index:{0,6}) {
        number(d.sky.layers[index][3],0,1);
        if(d.sky.layers[index][3]>0) {
            const auto& mask=d.sky.layers[index+1];require(mask[0]<mask[1]&&mask[2]<mask[3],"Invalid sky layer mask");
            number(d.sky.layers[index+5][index==0?0:1],.001f,16);
        }
    }
    require(d.override_particle_intensity<=1,"Invalid particle intensity override");number(d.particle_intensity,0,1);
    if(d.sky_flags>15)throw std::runtime_error("Invalid sky policy flags: "+std::to_string(d.sky_flags));
    number(d.grade_strength,0,1);for(float value:d.grade_color)number(value,0,4);
    environmentProfile(c,d.base);auto& profile=content().extensions.environments.at(d.base.dimension);
    if(d.override_particle_intensity)profile.particleIntensity=d.particle_intensity;
    profile.skyFlags=d.sky_flags;profile.gradeStrength=d.grade_strength;
    if(d.grade_strength>0)std::copy(std::begin(d.grade_color),std::end(d.grade_color),profile.gradeColor.begin());
    for(size_t i=0;i<MC_SKY_LAYER_VECTORS;++i)std::copy(std::begin(d.sky.layers[i]),std::end(d.sky.layers[i]),profile.skyLayers[i].begin());
}
void PluginManager::particleType(MC_PluginContext& c,const MC_ParticleDefinition& d){
    require(d.size>=sizeof(d),"Invalid particle descriptor");require(m_phase==MC_REGISTER&&!content().frozen,"Particle registration closed");const auto key=str(d.key);require(validKey(key)&&key.rfind(m_info.at(c.index).manifest.id+":",0)==0,"Particle key must use own namespace");
    number(d.lifetime,.01f,60);number(d.scale,.001f,4);number(d.gravity,-100,100);for(float x:d.color)number(x,0,1);for(float x:d.velocity)number(x,-100,100);
    ParticleContent p;p.key=key;p.material=str(d.material);p.definition=d;p.definition.key=p.definition.material=nullptr;p.owner=c.index;
    require(content().extensions.particles.size()<256&&content().extensions.particles.emplace(key,p).second,"Duplicate/too many particles");
}
void PluginManager::particleTypeV2(MC_PluginContext& c,const MC_ParticleDefinitionV2& d) {
    require(d.size>=sizeof(d)&&d.shape<=3&&d.collision<=1,"Invalid particle V2 descriptor");
    for(const auto& axis:d.drift){for(int i=0;i<3;++i)number(axis[i],-100,100);require(axis[3]==0||axis[3]==1,"Invalid drift wave");}
    particleType(c,d.base);auto& p=content().extensions.particles.at(d.base.key);p.advanced=true;p.collision=d.collision!=0;p.shape=d.shape;
    for(int axis=0;axis<3;++axis)std::copy(std::begin(d.drift[axis]),std::end(d.drift[axis]),p.drift[size_t(axis)].begin());
}
void PluginManager::ambientDefinition(MC_PluginContext& c,const MC_AmbientDefinition& d) {
    require(d.size>=sizeof(d)&&validDimension(d.dimension)&&d.state_bytes<=4096&&d.replace_native<=1&&d.update,"Invalid ambient descriptor");
    const auto key=str(d.key);owned(c,key);reference(c,dimensionContent(d.dimension).key);
    AmbientContent a;a.owner=c.index;a.key=key;a.dimension=d.dimension;a.stateBytes=d.state_bytes;a.replaceNative=d.replace_native!=0;
    const auto callback=d.update;const auto userdata=d.userdata;const auto index=c.index;
    a.update=[this,callback,userdata,index](const MC_AmbientContext& view,void* state) {
        if(!m_info[index].active||!content().runtimeFault.empty())return false;
        const auto previous=m_phase;m_phase=MC_ENVIRONMENT;bool success=false;
        try{success=callback(&view,state,userdata)==0;if(!success)callbackFault(index,"Ambient callback failed");}
        catch(...){callbackFault(index,"Exception across ambient ABI");}
        m_phase=previous;return success;
    };
    require(content().extensions.ambient.size()<64&&content().extensions.ambient.emplace(key,std::move(a)).second,"Duplicate/too many ambient emitters");
}
void PluginManager::heldModel(MC_PluginContext& c,const MC_HeldModel& d){
    require(d.size>=sizeof(d),"Invalid held model");owned(c,str(d.item));const auto id=static_cast<uint16_t>(resolveItem(d.item));
    HeldModelContent m;m.path=containedPath(m_info.at(c.index).root,str(d.path));require(std::filesystem::is_regular_file(m.path)&&(m.path.extension()==".glb"||m.path.extension()==".gltf"),"Held model must be glTF/GLB");
    const auto& package=m_info.at(c.index);
    const auto modelRoot=std::filesystem::weakly_canonical(package.root);
    const auto& loaded=m_modelAssets->load(model::AssetSource{package.manifest.id,modelRoot,{}},m.path.lexically_relative(modelRoot));
    require(bool(loaded),loaded.error.c_str());m.asset=loaded.asset;m_modelAssets->alias(m.path,loaded);
    for(int i=0;i<16;++i){number(d.first_person[i],-100,100);number(d.third_person[i],-100,100);m.first[i]=d.first_person[i];m.third[i]=d.third_person[i];}
    require(content().extensions.heldModels.emplace(id,m).second,"Duplicate held model");
}
void PluginManager::playlist(MC_PluginContext& c,const MC_MusicPlaylist& d){
    require(d.size>=sizeof(d)&&d.menu<=1&&d.random<=1&&validDimension(d.dimension)&&d.tracks&&d.count&&d.count<=32,"Invalid playlist");require(m_phase==MC_REGISTER&&!content().frozen,"Playlist registration closed");const auto key=str(d.key);require(validKey(key)&&key.rfind(m_info.at(c.index).manifest.id+":",0)==0,"Playlist key must use own namespace");number(d.gap_seconds,0,600);
    PlaylistContent p;p.key=key;p.dimension=d.dimension;p.menu=d.menu;p.random=d.random;p.gap=d.gap_seconds;
    for(uint32_t i=0;i<d.count;++i){const auto path=containedPath(m_info.at(c.index).root,str(d.tracks[i]));require(std::filesystem::is_regular_file(path)&&path.extension()==".wav"&&std::filesystem::file_size(path)<=64*1024*1024,"Playlist requires bounded WAV files");p.tracks.push_back(path);}
    for(const auto& pair:content().extensions.playlists)require(!(pair.second.menu==p.menu&&(p.menu||pair.second.dimension==p.dimension)),"Conflicting playlist binding");
    require(content().extensions.playlists.emplace(key,std::move(p)).second,"Duplicate playlist");
}
void PluginManager::validateCapabilities(){
    const auto& e=content().extensions;
    for(const auto& pair:e.blockBehaviors)validateDefinition("block_behaviors",blockKey(static_cast<BlockId>(pair.first)),[&] {
        for(const auto& d:pair.second.drops)require(resolveItem(d.item)!=ItemId::EMPTY&&d.maximum<=getItemProps(resolveItem(d.item)).maxStack,"Invalid break_drops item/count");
        const auto& b=pair.second;if(!b.harvest.empty()){require(resolveItem(b.harvest)!=ItemId::EMPTY,"Empty harvest_item");require(b.harvestCount<=getItemProps(resolveItem(b.harvest)).maxStack,"harvest_count too large");const auto replacement=resolveBlock(b.replacement);require(!b.resetTicks||replacement!=static_cast<BlockId>(pair.first),"Reset requires distinct harvest_replacement");}if(!b.action.empty())require(e.actions.count(b.action),"Unknown block action field action");
    });
    for(const auto& pair:e.loot)validateDefinition("loot_tables",pair.first,[&]{for(const auto& b:pair.second.entries){const auto id=resolveItem(b.item);require(id!=ItemId::EMPTY&&b.maximum<=getItemProps(id).maxStack,"Invalid loot entries item/count");}});
    for(const auto& pair:e.structures)validateDefinition("structures",pair.first,[&]{for(const auto& b:pair.second.blocks)if(!b.loot.empty())require(e.loot.count(b.loot),"Unknown structure blocks/loot table");});
    for(const auto& pair:e.itemBehaviors)validateDefinition("item_behaviors",itemKey(static_cast<ItemId>(pair.first)),[&]{if(!pair.second.action.empty())require(e.actions.count(pair.second.action),"Unknown item action field action");if(!pair.second.dimension.empty())resolveDimension(pair.second.dimension);});
    for(const auto& pair:e.actions)validateDefinition("player_actions",pair.first,[&]{if(!pair.second.dimension.empty())resolveDimension(pair.second.dimension);});
    for(const auto& pair:e.particles)validateDefinition("particles",pair.first,[&]{const auto& key=pair.second.material;bool found=content().materials.count(key);for(uint16_t i=0;i<static_cast<uint16_t>(BlockTexture::Count);++i)found=found||key==getBlockTextureAssetName(static_cast<BlockTexture>(i));require(found,"Unknown particle material field material");});
}
void PluginManager::describeCapabilities(){
    const auto& e=content().extensions;auto& n=content().networkDescription;using J=nlohmann::json;
    for(const auto& pair:e.blockBehaviors){const auto& b=pair.second;J drops=J::array();for(const auto& d:b.drops)drops.push_back({d.item,d.minimum,d.maximum,d.numerator,d.denominator});if(b.fields)n["semantics/block/"+blockKey(static_cast<BlockId>(pair.first))]=J::array({b.fields,b.light,b.tags,b.substrate,drops}).dump();}
    for(const auto& pair:e.itemBehaviors){const auto& b=pair.second;if(b.fields)n["semantics/item/"+itemKey(static_cast<ItemId>(pair.first))]=J::array({b.fields,b.fuel,b.tags}).dump();}
    for(const auto& pair:e.blockBehaviors){const auto& b=pair.second;n["behavior/block/"+blockKey(static_cast<BlockId>(pair.first))]=J::array({b.climbable,b.replaceable,b.support,b.encouragement,b.flammability,b.damage,b.harvest,b.harvestCount,b.replacement,b.resetTicks,b.action}).dump();}
    for(const auto& pair:e.itemBehaviors)n["behavior/item/"+itemKey(static_cast<ItemId>(pair.first))]=J::array({pair.second.action,pair.second.dimension}).dump();
    for(const auto& pair:e.structures)if(pair.second.omitCoarseLod)n["structure-lod/"+pair.first]="omit-coarse-v1";
    for(const auto& pair:e.environments)n["sky-layers/"+pair.second.key]=J::array({pair.second.skyLayers,pair.second.particleIntensity,pair.second.skyFlags,pair.second.gradeColor,pair.second.gradeStrength}).dump();
    for(const auto& pair:e.ambient)n["ambient/"+pair.first]=J::array({dimensionContent(pair.second.dimension).key,pair.second.stateBytes,pair.second.replaceNative}).dump();
    for(const auto& pair:e.particles)if(pair.second.advanced)n["particle-v2/"+pair.first]=J::array({pair.second.shape,pair.second.collision,pair.second.drift}).dump();
    for(const auto& pair:e.loot){J entries=J::array();for(const auto& b:pair.second.entries)entries.push_back({b.item,b.minimum,b.maximum,b.chance});n["loot/"+pair.first]=J::array({pair.second.revision,entries}).dump();}
    for(const auto& pair:e.structures){const auto& s=pair.second;J blocks=J::array();for(const auto& b:s.blocks)blocks.push_back({b.x,b.y,b.z,blockKey(static_cast<BlockId>(b.id)),b.loot});n["structure/"+pair.first]=J::array({dimensionContent(s.dimension).key,s.revision,s.cell,s.chance,s.radius,s.height,s.tolerance,s.domain,blocks,s.build!=nullptr}).dump();if(s.placements){auto description=J::parse(n["structure/"+pair.first]);description.push_back(J::array({"placements-v2",s.minY,s.maxY,s.order,s.buildV2!=nullptr}));n["structure/"+pair.first]=description.dump();}}
    for(const auto& pair:e.actions){const auto& a=pair.second;if(a.contextual())n[(a.targetV3?"action-v3/":"action-v2/")+pair.first]=J::array({a.rangeFromEye,a.toolPose,a.invalidMarker,a.useCooldown}).dump();n["action/"+pair.first]=J::array({a.kind,a.dimension,a.range,a.cooldown,a.durability,bool(a.target)}).dump();}
}
uint64_t PluginManager::submitAction(MC_PluginContext& c,const MC_ActionRequest& d){
    require(!m_readOnly,"Read-only callback cannot request an action");
    require(d.size>=sizeof(d)&&content().extensions.actions.count(str(d.action)),"Unknown action");actor(d.player_id);
    require(m_actionResults.size()<4096,"Action receipts full; consume completed results");const auto id=m_nextAction++;const std::string key=d.action;const auto player=d.player_id;reference(c,key);const auto state=actionState?actionState(player):0;require(state,"Player unavailable");
    enqueue(c,[this,id,player,key,state]{m_actionResults.at(id).status=MC_ACTION_FAILED;const bool ok=actionState&&actionState(player)==state&&executeAction&&executeAction(player,key);const auto it=m_actionResults.find(id);if(it!=m_actionResults.end())it->second.status=ok?MC_ACTION_SUCCEEDED:MC_ACTION_FAILED;return ok;});m_actionResults.emplace(id,ActionReceipt{c.index,MC_ACTION_PENDING});return id;
}
MC_ActionResult PluginManager::actionResult(MC_PluginContext& c,uint64_t id) const{const auto it=m_actionResults.find(id);require(it!=m_actionResults.end()&&it->second.owner==c.index,"Unknown action receipt");return {sizeof(MC_ActionResult),it->second.status,id};}
const void* PluginManager::capability(const char* name,uint32_t version){
    if(name&&version==3&&std::string(name)==MC_PLAYER_ACTIONS_EXTENSION){
        static const MC_PlayerActionsV3 actions{sizeof(MC_PlayerActionsV3),3,
            [](MC_PluginContext* c,const MC_PlayerActionV3* d){return call(c,[&]{require(d,"Missing action V3");manager(c).playerActionV3(*c,*d);},true);},
            static_cast<const MC_PlayerActionsV2*>(capability(MC_PLAYER_ACTIONS_EXTENSION,2))};return &actions;
    }
    if(name&&version==2&&std::string(name)==MC_PLAYER_ACTIONS_EXTENSION){
        static const MC_PlayerActionsV2 actions{sizeof(MC_PlayerActionsV2),2,
            [](MC_PluginContext* c,const MC_PlayerActionV2* d){return call(c,[&]{require(d,"Missing action V2");manager(c).playerActionV2(*c,*d);},true);},
            static_cast<const MC_PlayerActionsV1*>(capability(MC_PLAYER_ACTIONS_EXTENSION,1))};return &actions;
    }
    if(name&&version==2&&std::string(name)==MC_BEHAVIORS_EXTENSION){
        static const MC_BehaviorsV2 behaviors{sizeof(MC_BehaviorsV2),2,
            [](MC_PluginContext* c,const MC_BlockBehaviorV2* d){return call(c,[&]{require(d,"Missing behavior V2");manager(c).blockBehaviorV2(*c,*d);},true);},
            [](MC_PluginContext* c,const MC_ItemBehaviorV2* d){return call(c,[&]{require(d,"Missing item behavior V2");manager(c).itemBehaviorV2(*c,*d);},true);},
            static_cast<const MC_BehaviorsV1*>(capability(MC_BEHAVIORS_EXTENSION,1))};return &behaviors;
    }
    if(name&&version==2&&std::string(name)==MC_STRUCTURES_EXTENSION) {
        static const MC_StructuresV2 structures{sizeof(MC_StructuresV2),2,
            [](MC_PluginContext* c,const MC_StructureDefinitionV2* d){return call(c,[&]{require(d,"Missing structure V2");manager(c).structureV2(*c,*d);},true);},
            static_cast<const MC_StructuresV1*>(capability(MC_STRUCTURES_EXTENSION,1))};
        return &structures;
    }
    if(name&&version==2&&std::string(name)==MC_PRESENTATION_EXTENSION) {
        static const MC_PresentationV2 presentation{sizeof(MC_PresentationV2),2,
            [](MC_PluginContext* c,const MC_ParticleDefinitionV2* d){return call(c,[&]{require(d,"Missing particle V2");manager(c).particleTypeV2(*c,*d);},true);},
            [](MC_PluginContext* c,const MC_ParticleInstanceV2* d){return call(c,[&]{auto& m=manager(c);require(d&&d->size>=sizeof(*d)&&validDimension(d->dimension)&&d->particle,"Invalid particle V2 emission");const auto found=content().extensions.particles.find(d->particle);require(found!=content().extensions.particles.end()&&found->second.owner==c->index,"Unknown/foreign particle");require(!m.readOnly()&&(m.phase()==MC_HUD||m.phase()==MC_ENVIRONMENT||m.phase()==MC_UPDATE_PRE||m.phase()==MC_UPDATE_POST),"Local particles require frame callback");require(m.emitParticleV2&&m.emitParticleV2(*d),"Particle V2 emission unavailable");});},
            [](MC_PluginContext* c,const MC_AmbientDefinition* d){return call(c,[&]{require(d,"Missing ambient definition");manager(c).ambientDefinition(*c,*d);},true);},
            static_cast<const MC_PresentationV1*>(capability(MC_PRESENTATION_EXTENSION,1)),
            [](MC_PluginContext* c,const MC_ItemIcon* d){return call(c,[&]{require(d,"Missing item icon");manager(c).itemIcon(*c,*d);},true);}};return &presentation;
    }
    if(name&&version==2&&std::string(name)==MC_ENVIRONMENT_EXTENSION) {
        static const MC_EnvironmentV2 environment{sizeof(MC_EnvironmentV2),2,
            [](MC_PluginContext* c,const MC_EnvironmentProfileV2* d){return call(c,[&]{require(d,"Missing environment V2");manager(c).environmentProfileV2(*c,*d);},true);},
            static_cast<const MC_EnvironmentV1*>(capability(MC_ENVIRONMENT_EXTENSION,1))};return &environment;
    }
    if(name&&version==1&&std::string(name)==MC_MATERIAL_SEQUENCES_EXTENSION) {
        static const MC_MaterialSequencesV1 materials{sizeof(MC_MaterialSequencesV1),1,
            [](MC_PluginContext* c,const MC_MaterialSequenceDefinition* d){return call(c,[&]{require(d,"Missing material sequence");manager(c).materialSequence(*c,*d);},true);}};
        return &materials;
    }
    if(name&&version==3&&std::string(name)==MC_STRUCTURES_EXTENSION) {
        static const MC_StructuresV3 structures{sizeof(MC_StructuresV3),3,
            [](MC_PluginContext* c,const char* key,uint32_t policy){return call(c,[&]{manager(c).structureCoarseLod(*c,key,policy);},true);},
            static_cast<const MC_StructuresV2*>(capability(MC_STRUCTURES_EXTENSION,2))};return &structures;
    }
    if(!name||version!=1)return nullptr;
    static const MC_BehaviorsV1 behaviors{sizeof(MC_BehaviorsV1),1,
        [](MC_PluginContext* c,const MC_BlockBehavior* d){return call(c,[&]{require(d,"Missing behavior");manager(c).blockBehavior(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_ItemBehavior* d){return call(c,[&]{require(d,"Missing behavior");manager(c).itemBehavior(*c,*d);},true);}};
    static const MC_StructuresV1 structures{sizeof(MC_StructuresV1),1,
        [](MC_PluginContext* c,const MC_StructureDefinition* d){return call(c,[&]{require(d,"Missing structure");manager(c).structure(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_LootTable* d){return call(c,[&]{require(d,"Missing loot");manager(c).lootTable(*c,*d);},true);},
        [](MC_PluginContext* c,uint32_t dim,const char* key,int32_t x,int32_t z,uint32_t range,MC_LocatedStructure* out){return call(c,[&]{require(out&&out->size>=sizeof(*out)&&validDimension(dim)&&range<=8192&&std::abs(int64_t(x))<=30000000&&std::abs(int64_t(z))<=30000000,"Invalid locate query");auto& m=manager(c);require(m.locateStructure&&m.locateStructure(dim,str(key),x,z,range,*out),"Structure not found/unavailable");});}};
    static const MC_PlayerActionsV1 actions{sizeof(MC_PlayerActionsV1),1,
        [](MC_PluginContext* c,const MC_PlayerAction* d){return call(c,[&]{require(d,"Missing action");manager(c).playerAction(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_ActionRequest* d,uint64_t* id){return call(c,[&]{require(d&&id,"Missing action request");*id=manager(c).submitAction(*c,*d);});},
        [](MC_PluginContext* c,uint64_t id,MC_ActionResult* out){return call(c,[&]{require(out&&out->size>=sizeof(*out),"Invalid result buffer");auto& m=manager(c);*out=m.actionResult(*c,id);if(out->status!=MC_ACTION_PENDING)m.m_actionResults.erase(id);});},
        [](MC_PluginContext* c,uint64_t id,const char* key,MC_ActionTarget* out){return call(c,[&]{actor(id);require(out&&out->size>=sizeof(*out),"Invalid target buffer");auto& m=manager(c);require(!m.readOnly(),"Nested target preview unavailable");require(m.actionTarget&&m.actionTarget(id,str(key),true,*out),"Target unavailable");});},
        [](MC_PluginContext* c,uint64_t id,const char* key,float* out){return call(c,[&]{actor(id);auto& m=manager(c);require(out&&m.actionCooldown&&m.actionCooldown(id,str(key),*out),"Cooldown unavailable");});},
        [](MC_PluginContext* c,uint64_t id,uint32_t dim,const double* p){return call(c,[&]{actor(id);position(p);require(validDimension(dim),"Unknown dimension");const std::array<double,3> copy{{p[0],p[1],p[2]}};auto& m=manager(c);m.enqueue(*c,[&m,id,dim,copy]{return m.teleportPlayer&&m.teleportPlayer(id,dim,copy.data());});});}};
    static const MC_EnvironmentV1 environment{sizeof(MC_EnvironmentV1),1,
        [](MC_PluginContext* c,const MC_EnvironmentProfile* d){return call(c,[&]{require(d,"Missing environment");manager(c).environmentProfile(*c,*d);},true);},
        [](MC_PluginContext* c,uint32_t dim,MC_DimensionTime* out){return call(c,[&]{auto& m=manager(c);require(out&&out->size>=sizeof(*out)&&m.dimensionTime&&m.dimensionTime(dim,*out),"Dimension time unavailable");});},
        [](MC_PluginContext* c,const MC_DimensionTime* d){return call(c,[&]{require(d&&d->size>=sizeof(*d)&&validDimension(d->dimension)&&d->advancing<=2&&d->cycle_seconds<=86400000,"Invalid time policy");number(d->phase,0,std::nextafter(1.0f,0.0f));const auto copy=*d;auto& m=manager(c);m.enqueue(*c,[&m,copy]{return m.setDimensionTime&&m.setDimensionTime(copy);});});}};
    static const MC_PresentationV1 presentation{sizeof(MC_PresentationV1),1,
        [](MC_PluginContext* c,const MC_ParticleDefinition* d){return call(c,[&]{require(d,"Missing particle");manager(c).particleType(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_ParticleEmission* d){return call(c,[&]{require(d&&d->size>=sizeof(*d)&&validDimension(d->dimension)&&d->count&&d->count<=128&&d->local_only<=1,"Invalid particle emission");position(d->position);const std::string key=str(d->particle);const auto found=content().extensions.particles.find(key);require(found!=content().extensions.particles.end()&&found->second.owner==c->index,"Unknown/foreign particle");auto copy=*d;auto& m=manager(c);require(!m.readOnly(),"Read-only target cannot emit particles");require(m.m_particleEmitted+d->count<=128,"Particle frame budget exceeded");m.m_particleEmitted+=d->count;if(d->local_only){require(m.phase()==MC_HUD||m.phase()==MC_ENVIRONMENT||m.phase()==MC_UPDATE_PRE||m.phase()==MC_UPDATE_POST,"Local particles require frame callback");require(m.emitParticle&&m.emitParticle(copy),"Particles unavailable");}else m.enqueue(*c,[&m,copy,key]()mutable{copy.particle=key.c_str();return m.emitParticle&&m.emitParticle(copy);});});},
        [](MC_PluginContext* c,const MC_HeldModel* d){return call(c,[&]{require(d,"Missing model");manager(c).heldModel(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_MusicPlaylist* d){return call(c,[&]{require(d,"Missing playlist");manager(c).playlist(*c,*d);},true);}};
    const std::string key=name;
    if(key==MC_BEHAVIORS_EXTENSION)return &behaviors;
    if(key==MC_STRUCTURES_EXTENSION)return &structures;
    if(key==MC_PLAYER_ACTIONS_EXTENSION)return &actions;
    if(key==MC_ENVIRONMENT_EXTENSION)return &environment;
    if(key==MC_PRESENTATION_EXTENSION)return &presentation;
    return nullptr;
}
} // namespace Plugins
