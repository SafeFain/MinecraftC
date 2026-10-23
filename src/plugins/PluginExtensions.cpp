#include "plugins/PluginManager.h"
#include "Config.h"
#include "model/GltfLoader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

namespace Plugins {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string text(const char* s){require(s&&*s,"Missing extension string");return s;}
PluginManager& owner(MC_PluginContext* c){require(c&&c->manager,"Invalid plugin context");return *static_cast<PluginManager*>(c->manager);}
template<class F> int32_t call(MC_PluginContext* c,F f,bool registration=false) {
    if(!c||!c->manager)return -1;
    // Do not mutate the context on the wrong thread.
    if(!owner(c).hostThread())return -1;
    try{c->error.clear();f();return 0;}
    catch(const std::exception& e){c->error=e.what();if(registration)c->registrationFailed=true;return -1;}
}
}
int32_t PluginManager::blockFamily(MC_PluginContext& ctx,const MC_BlockFamily& d,uint16_t& id) {
    require(d.size>=sizeof(d)&&d.kind>=MC_SLAB_FAMILY&&d.kind<=MC_BUTTON_FAMILY,"Invalid block family descriptor");
    require(d.manual_open<=1&&d.villager_open<=1&&d.arrow_trigger<=1,"Invalid family flags");
    require(d.kind!=MC_BUTTON_FAMILY||(d.release_ticks>0&&d.release_ticks<=65535),"Button duration must be 1..65535 ticks");
    require(!d.block.cross&&d.block.solid==(d.kind==MC_BUTTON_FAMILY?0u:1u),"Invalid template collision flags");
    const auto key=text(d.block.key);owned(ctx,key);
    const std::string full=d.full_block?d.full_block:"";
    require(d.kind!=MC_SLAB_FAMILY||validKey(full),"Slab requires full_block key");
    const uint32_t count=d.kind==MC_SLAB_FAMILY?2:d.kind==MC_STAIR_FAMILY?8:d.kind==MC_DOOR_FAMILY?64:24;
    require(FIRST_CONTENT_ID+content().blocks.size()+count<=INVALID_CONTENT_ID,"Block registry full");
    for(uint32_t n=0;n<count;++n) {
        const auto stateKey=n?key+"/state/"+std::to_string(n):key;
        require(validKey(stateKey),"State key exceeds key limit");
        for(const auto& b:content().blocks)require(b.second.key!=stateKey,"Duplicate state key");
    }
    for(uint32_t n=0;n<count;++n) {
        MC_Block b=d.block;const std::string stateKey=n?key+"/state/"+std::to_string(n):key;b.key=stateKey.c_str();
        uint16_t raw;block(ctx,b,raw);if(!n)id=raw;
        auto& state=content().blocks.at(raw);state.family=id;state.familyKind=d.kind;state.stateIndex=n;
        state.properties.shape=d.kind==MC_SLAB_FAMILY?RenderShape::Slab:d.kind==MC_STAIR_FAMILY?RenderShape::Stair:d.kind==MC_DOOR_FAMILY?RenderShape::Door:RenderShape::Button;
        state.fullBlock=full;state.manualOpen=d.manual_open;state.villagerOpen=d.villager_open;
        state.releaseTicks=d.release_ticks;state.arrowTrigger=d.arrow_trigger;
    }
    return 0;
}
int32_t PluginManager::blockGeometry(MC_PluginContext& ctx,const MC_BlockGeometry& d) {
    require(d.size>=sizeof(d),"Invalid geometry descriptor");owned(ctx,text(d.block));
    auto id=resolveBlock(d.block);auto& b=content().blocks.at(static_cast<uint16_t>(id));
    require(!b.family,"Template geometry cannot be overridden");
    auto group=[](const MC_BlockBox* boxes,uint32_t count)->std::optional<BlockCollisionBoxes> {
        require(count<=MC_MAX_BLOCK_BOXES&&(!count||boxes),"Invalid box count/buffer");
        if(!boxes)return {};
        BlockCollisionBoxes out;out.count=static_cast<uint8_t>(count);
        for(uint32_t i=0;i<count;++i)for(int axis=0;axis<3;++axis) {
            const float lo=boxes[i].min[axis],hi=boxes[i].max[axis];
            require(std::isfinite(lo)&&std::isfinite(hi)&&lo>=0&&hi<=1&&lo<hi,"Invalid block box bounds");
            out.boxes[i].min[axis]=lo;out.boxes[i].max[axis]=hi;
        }
        return out;
    };
    auto collision=group(d.collision,d.collision_count),selection=group(d.selection,d.selection_count),model=group(d.model,d.model_count);
    if(collision&&collision->count>1) {
        // Full-volume unions must obey the same support, spawning and power rules as cubes.
        std::array<std::vector<float>,3> cuts;
        for(int axis=0;axis<3;++axis){cuts[axis]={0,1};for(uint8_t i=0;i<collision->count;++i){cuts[axis].push_back(collision->boxes[i].min[axis]);cuts[axis].push_back(collision->boxes[i].max[axis]);}std::sort(cuts[axis].begin(),cuts[axis].end());cuts[axis].erase(std::unique(cuts[axis].begin(),cuts[axis].end()),cuts[axis].end());}
        bool full=true;
        for(size_t x=0;full&&x+1<cuts[0].size();++x)for(size_t y=0;full&&y+1<cuts[1].size();++y)for(size_t z=0;full&&z+1<cuts[2].size();++z) {
            const glm::dvec3 point((double(cuts[0][x])+cuts[0][x+1])*.5,(double(cuts[1][y])+cuts[1][y+1])*.5,(double(cuts[2][z])+cuts[2][z+1])*.5);
            bool covered=false;for(uint8_t i=0;i<collision->count;++i){const auto& box=collision->boxes[i];covered=covered||(glm::all(glm::greaterThanEqual(point,glm::dvec3(box.min)))&&glm::all(glm::lessThanEqual(point,glm::dvec3(box.max))));}
            full=covered;
        }
        if(full){BlockCollisionBoxes cube;cube.count=1;collision=cube;}
    }
    require(!model||model->count,"Box model cannot be empty");
    if(collision){b.collision=collision;b.properties.solid=collision->count!=0;}
    if(selection)b.selection=selection;
    if(model){b.model=model;b.properties.shape=RenderShape::Boxes;}
    return 0;
}
namespace {
MC_BlockState describeState(BlockId id) {
    require(validBlock(id),"Unknown block");MC_BlockState out{};out.size=sizeof(out);
    DoorState door;ButtonState button;ArchitecturalBlockState arch;
    if(decodeDoor(id,door)) {
        out.kind=MC_DOOR_FAMILY;auto base=door;base.direction=BedDirection::North;base.rightHinge=base.open=base.powered=base.upper=false;
        out.family=static_cast<uint16_t>(doorBlock(base));out.direction=static_cast<uint8_t>(door.direction);out.half=door.upper;
        out.right_hinge=door.rightHinge;out.open=door.open;out.powered=door.powered;
    } else if(decodeButton(id,button)) {
        out.kind=MC_BUTTON_FAMILY;auto base=button;base.direction=BedDirection::North;base.attachment=FaceDir::TOP;base.pressed=false;
        out.family=static_cast<uint16_t>(buttonBlock(base));out.direction=static_cast<uint8_t>(button.direction);out.attachment=static_cast<uint8_t>(button.attachment);out.pressed=button.pressed;
    } else if(decodeArchitecturalBlock(id,arch)) {
        out.kind=arch.shape==RenderShape::Slab?MC_SLAB_FAMILY:MC_STAIR_FAMILY;auto base=arch;base.half=BlockHalf::Bottom;base.direction=BedDirection::North;
        out.family=static_cast<uint16_t>(architecturalStateBlock(base));out.half=arch.half==BlockHalf::Top;out.direction=static_cast<uint8_t>(arch.direction);
    } else require(false,"Block has no state family");
    return out;
}
BlockId composeState(const MC_BlockState& s) {
    require(s.size>=sizeof(s)&&s.direction<4&&s.half<=1&&s.right_hinge<=1&&s.open<=1&&s.powered<=1&&s.attachment<6&&s.pressed<=1,"Invalid block state");
    const auto base=describeState(static_cast<BlockId>(s.family));require(base.family==s.family&&base.kind==s.kind,"Invalid family handle/kind");
    const auto id=static_cast<BlockId>(s.family);DoorState door;ButtonState button;ArchitecturalBlockState arch;
    if(decodeDoor(id,door)) {
        require(!s.attachment&&!s.pressed,"Unused door fields must be zero");door.direction=static_cast<BedDirection>(s.direction);door.upper=s.half;door.rightHinge=s.right_hinge;door.open=s.open;door.powered=s.powered;return doorBlock(door);
    }
    if(decodeButton(id,button)) {
        require(!s.half&&!s.right_hinge&&!s.open&&!s.powered,"Unused button fields must be zero");button.direction=static_cast<BedDirection>(s.direction);button.attachment=static_cast<FaceDir>(s.attachment);button.pressed=s.pressed;return buttonBlock(button);
    }
    require(!s.right_hinge&&!s.open&&!s.powered&&!s.attachment&&!s.pressed&&(s.kind!=MC_SLAB_FAMILY||!s.direction),"Unused architectural fields must be zero");
    decodeArchitecturalBlock(id,arch);arch.half=static_cast<BlockHalf>(s.half);arch.direction=static_cast<BedDirection>(s.direction);return architecturalStateBlock(arch);
}
}
void PluginManager::callbackFault(size_t i,const std::string& reason) {
    m_info.at(i).active=false;m_info.at(i).status=reason+"; restart required";
    content().runtimeFault="Plugin "+m_info.at(i).manifest.id+": "+reason;
    discardOperations();
}
int32_t PluginManager::entity(MC_PluginContext& ctx,const MC_EntityDefinition& d,uint16_t& id) {
    require(d.size>=sizeof(d),"Entity descriptor size mismatch");
    EntityContent e;e.key=text(d.key);owned(ctx,e.key);e.name=text(d.name);
    require(d.template_type<MC_BUILTIN_ENTITY_COUNT&&d.template_type!=0&&d.template_type!=9&&d.template_type!=10,"Invalid entity AI template");
    require(d.hostile<=1&&d.natural_spawn<=1&&d.drop_count<=64,"Invalid entity flags/count");
    require(std::isfinite(d.health)&&d.health>0&&d.health<=10000&&std::isfinite(d.speed)&&d.speed>=0&&d.speed<=32&&
        std::isfinite(d.attack_damage)&&d.attack_damage>=0&&d.attack_damage<=1000,"Invalid entity attributes");
    for(float x:d.size_xyz)require(std::isfinite(x)&&x>0&&x<=16,"Invalid entity bounds");
    e.base=static_cast<uint16_t>(d.template_type);e.hostile=d.hostile;e.natural=d.natural_spawn;
    e.health=d.health;e.speed=d.speed;e.damage=d.attack_damage;e.stateVersion=d.state_version;
    std::copy(d.size_xyz,d.size_xyz+3,e.size.begin());e.drop=d.drop_item?d.drop_item:"";e.dropCount=d.drop_count;

    if(d.model&&*d.model){e.model=containedPath(m_info.at(ctx.index).root,d.model);require(std::filesystem::is_regular_file(e.model),"Missing entity model");
        const auto& package=m_info.at(ctx.index);
        const auto modelRoot=std::filesystem::weakly_canonical(package.root);
        const auto& loadedModel=m_modelAssets->load(model::AssetSource{package.manifest.id,modelRoot,{}},e.model.lexically_relative(modelRoot));
        require(bool(loadedModel),loadedModel.error.c_str());
        e.asset=loadedModel.asset;
    }
    auto& entries=content().extensions.entities;
    for(const auto& it:entries)require(it.second.key!=e.key,"Duplicate entity key");
    if(d.target&&*d.target) {
        id=resolveEntity(d.target);require(id<MC_BUILTIN_ENTITY_COUNT&&id!=0&&id!=9&&id!=10,"Only builtin mobs may be overridden");
        require(!entries.count(id),"Conflicting entity override");
    } else {
        uint32_t next=FIRST_CONTENT_ID;while(entries.count(static_cast<uint16_t>(next))&&next<INVALID_CONTENT_ID)++next;
        require(next<INVALID_CONTENT_ID,"Entity registry full");id=static_cast<uint16_t>(next);
    }
    e.id=id;
    if(d.callback) {
        auto f=d.callback;auto u=d.userdata;auto* c=&ctx;const size_t i=ctx.index;
        e.callback=[this,f,u,c,i](uint32_t kind,const MC_EntitySnapshot& s,MC_EntityIntent& intent){
            if(!m_info[i].active||!content().runtimeFault.empty())return;
            const auto previous=m_phase;m_phase=MC_UPDATE_PRE;
            try {if(f(c,kind,&s,&intent,u))callbackFault(i,"Entity callback failed");}
            catch(const std::exception&){callbackFault(i,"Exception across entity ABI");}
            catch(...){callbackFault(i,"Exception across entity ABI");}
            m_phase=previous;
        };
    }
    entries.emplace(id,std::move(e));return 0;
}
int32_t PluginManager::dimensionDefinition(MC_PluginContext& ctx,const MC_DimensionDefinition& d,uint32_t& id) {
    require(d.size>=sizeof(d),"Dimension descriptor size mismatch");owned(ctx,text(d.key));
    DimensionContent value;value.key=d.key;value.name=text(d.name);value.revision=text(d.revision);
    require(value.name.size()<=256&&value.revision.size()<=128,"Dimension name/revision too long");
    value.generation.generator=text(d.generator);value.generation.revision=text(d.generator_revision);
    value.generation.parameters=d.parameters?d.parameters:"{}";
    auto reference=[&](const std::string& key){require(validKey(key),"Invalid dimension reference");const auto name=key.substr(0,key.find(':'));if(name=="minecraftc"||name==m_info.at(ctx.index).manifest.id)return;const auto& dependencies=m_info.at(ctx.index).manifest.dependencies;require(std::any_of(dependencies.begin(),dependencies.end(),[&](const Dependency& dependency){return dependency.id==name;}),"Cross-plugin dimension reference requires dependency");};
    reference(value.generation.generator);
    require(d.modifier_count<=64&&(!d.modifier_count||d.modifiers),"Invalid default modifiers");
    for(uint32_t i=0;i<d.modifier_count;++i){const auto key=text(d.modifiers[i].key);reference(key);value.generation.modifiers.emplace_back(key,text(d.modifiers[i].revision));}

    require(d.weather<=MC_WEATHER_THUNDER&&d.disable_natural_spawning<=1&&d.spawn<=MC_SPAWN_FIXED,"Invalid dimension policy");
    value.weather=d.weather;value.natural=!d.disable_natural_spawning;value.spawn=d.spawn;
    for(int axis=0;axis<3;++axis){require(std::isfinite(d.spawn_position[axis])&&std::abs(d.spawn_position[axis])<=30000000,"Invalid spawn position");value.position[axis]=d.spawn_position[axis];}
    if(value.spawn==MC_SPAWN_FIXED)require(value.position[1]>=Config::WORLD_MIN_Y&&value.position[1]<Config::WORLD_MAX_Y-2,"Spawn outside build height");
    value.deathTarget=d.death_target?d.death_target:"";value.voidTarget=d.void_target?d.void_target:"";
    if(!value.deathTarget.empty())reference(value.deathTarget);
    if(!value.voidTarget.empty())reference(value.voidTarget);
    auto& entries=content().extensions.dimensions;
    for(const auto& entry:entries)require(entry.second.key!=value.key,"Duplicate dimension");
    require(entries.size()<Config::PLUGIN_MAX_DIMENSIONS,"Too many dimensions");
    const auto& provider=m_info.at(ctx.index).manifest;
    if(provider.builtin&&provider.id=="official_heaven"&&value.key=="minecraftc:heaven")id=1;
    else {id=2;while(entries.count(id))++id;}
    require(id<Config::PLUGIN_MAX_DIMENSIONS&&!entries.count(id),"Dimension handle limit/duplicate reservation");
    entries.emplace(id,std::move(value));return 0;
}
int32_t PluginManager::dimensionDefinitionV2(MC_PluginContext& ctx,const MC_DimensionDefinitionV2& d,uint32_t& id) {
    require(d.size>=sizeof(d)&&d.sample_spawn,"Dimension V2 requires spawn callback");
    auto base=d.base;base.spawn=MC_SPAWN_SAFE_SURFACE;
    dimensionDefinition(ctx,base,id);
    auto& value=content().extensions.dimensions.at(id);
    value.spawn=MC_SPAWN_SAMPLED;value.sampleSpawn=d.sample_spawn;value.spawnUserdata=d.userdata;
    return 0;
}
int32_t PluginManager::dimensionDefinitionV3(MC_PluginContext& ctx,const MC_DimensionDefinitionV3& d,uint32_t& id) {
    require(d.size>=sizeof(d),"Dimension V3 descriptor size mismatch");
    const auto name=text(d.name_key);const auto prefix=m_info.at(ctx.index).manifest.id+":";
    require(validKey(name)&&name.rfind(prefix,0)==0,"Dimension name key must use own namespace");
    if(d.base.sample_spawn)dimensionDefinitionV2(ctx,d.base,id);
    else dimensionDefinition(ctx,d.base.base,id);
    auto& value=content().extensions.dimensions.at(id);value.nameKey=name;value.seedDomain=d.seed_domain;
    return 0;
}
void PluginManager::bedTravel(MC_PluginContext& ctx,const MC_DimensionTravelEntry& d) {
    require(d.size>=sizeof(d)&&validDimension(d.source)&&validDimension(d.target)&&d.source!=d.target,"Invalid bed travel entry");
    const auto key=text(d.key),name=text(d.name_key);owned(ctx,key);
    const auto prefix=m_info.at(ctx.index).manifest.id+":";
    require(validKey(name)&&name.rfind(prefix,0)==0,"Travel name key must use own namespace");
    reference(ctx,dimensionContent(d.source).key);reference(ctx,dimensionContent(d.target).key);
    require(content().extensions.bedTravel.size()<128&&content().extensions.bedTravel.emplace(key,DimensionTravelContent{d.source,d.target,key,name}).second,"Duplicate/too many travel entries");
}
void PluginManager::localizationBundle(MC_PluginContext& ctx,const char* directory) {
    require(m_phase==MC_REGISTER&&!content().frozen,"Localization registration closed");
    const auto& info=m_info.at(ctx.index);const auto root=containedPath(info.root,text(directory));
    require(std::filesystem::is_directory(root),"Localization bundle must be a directory");
    const auto prefix=info.manifest.id+":";bool english=false;size_t entries=0;
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
        require(!entry.is_symlink(),"Language files cannot be symlinks");
        if(!entry.is_regular_file()||entry.path().extension()!=".json")continue;
        const auto language=entry.path().stem().string();
        require(language.size()==5&&language[2]=='_'&&std::all_of(language.begin(),language.end(),[](char c){return (c>='a'&&c<='z')||c=='_';}),"Invalid language code");
        require(std::filesystem::file_size(entry.path())<=1024*1024,"Language bundle file too large");
        std::ifstream file(entry.path(),std::ios::binary);require(bool(file),"Cannot read language bundle");
        const auto data=Json::parse(std::string(std::istreambuf_iterator<char>(file),{}));require(data.type==Json::Type::Object,"Language dictionary must be an object");
        for(const auto& value:data.object) {
            require(++entries<=65536&&validKey(value.first)&&value.first.rfind(prefix,0)==0,"Language key must use own namespace/entry limit");
            require(value.second.type==Json::Type::String&&value.second.string.size()<=16384,"Invalid language value");
            require(content().extensions.translations[language].emplace(value.first,value.second.string).second,"Duplicate language key");
        }
        english=english||language=="en_us";
    }
    require(english,"Localization bundle requires en_us.json");
}
void PluginManager::localizedName(MC_PluginContext& ctx,const char* contentKey,const char* nameKey) {
    const auto key=text(contentKey),name=text(nameKey);owned(ctx,key);
    require(validKey(name)&&name.rfind(m_info.at(ctx.index).manifest.id+":",0)==0,"Localized name must use own namespace");
    reference(ctx,key);require(content().extensions.nameKeys.emplace(key,name).second,"Duplicate localized content name");
}
int32_t PluginManager::generator(MC_PluginContext& ctx,const MC_GeneratorDefinition& d) {
    require(d.size>=sizeof(d),"Generator descriptor size mismatch");owned(ctx,text(d.key));
    require(validDimension(d.dimension)&&d.column&&d.voxel,"Generator requires column and voxel callbacks");
    GeneratorContent g;g.key=d.key;g.name=text(d.name);g.revision=text(d.revision);
    require(g.revision.size()<=128,"Generator revision too long");g.dimension=d.dimension;g.column=d.column;g.voxel=d.voxel;g.userdata=d.userdata;
    require(content().extensions.generators.emplace(g.key,g).second,"Duplicate generator");return 0;
}
int32_t PluginManager::modifier(MC_PluginContext& ctx,const MC_ModifierDefinition& d) {
    require(d.size>=sizeof(d),"Modifier descriptor size mismatch");owned(ctx,text(d.key));
    require(validDimension(d.dimension)&&d.radius<=64&&d.decorate,"Invalid modifier");
    ModifierContent m;m.key=d.key;m.revision=text(d.revision);require(m.revision.size()<=128,"Modifier revision too long");
    m.dimension=d.dimension;m.radius=d.radius;m.decorate=d.decorate;m.userdata=d.userdata;
    require(content().extensions.modifiers.emplace(m.key,m).second,"Duplicate modifier");return 0;
}
int32_t PluginManager::generatorV2(MC_PluginContext& ctx,const MC_GeneratorDefinitionV2& d) {
    require(d.size>=sizeof(d),"Generator V2 descriptor size mismatch");
    const std::string revision=(d.terrain_lod||d.decoration_lod)?text(d.lod_revision):"";
    require(revision.size()<=128,"LOD revision too long");
    generator(ctx,d.base);
    auto& g=content().extensions.generators.at(d.base.key);
    g.batch=d.batch;g.terrainLod=d.terrain_lod;g.decorationLod=d.decoration_lod;g.lodRevision=revision;
    return 0;
}
int32_t PluginManager::generatorV3(MC_PluginContext& ctx,const MC_GeneratorDefinitionV3& d) {
    require(d.size>=sizeof(d)&&d.layers,"Invalid generator V3");
    generatorV2(ctx,d.base);content().extensions.generators.at(d.base.base.key).layers=d.layers;return 0;
}
int32_t PluginManager::modifierV2(MC_PluginContext& ctx,const MC_ModifierDefinitionV2& d) {
    require(d.size>=sizeof(d),"Modifier V2 descriptor size mismatch");
    const std::string revision=d.decoration_lod?text(d.lod_revision):"";
    require(revision.size()<=128,"LOD revision too long");
    modifier(ctx,d.base);
    auto& m=content().extensions.modifiers.at(d.base.key);
    m.decorationLod=d.decoration_lod;m.lodRevision=revision;
    return 0;
}
int32_t PluginManager::renderResource(MC_PluginContext& ctx,const MC_RenderResource& d,MC_RenderHandle& id) {
    require(d.size>=sizeof(d)&&d.kind<=MC_RENDER_PIPELINE,"Invalid render resource");
    const auto key=text(d.key);require(m_phase==MC_REGISTER&&!content().frozen,"Render registration is closed");
    require(validKey(key)&&key.rfind(m_info.at(ctx.index).manifest.id+":",0)==0,"Render key must use plugin namespace");
    auto& resources=content().extensions.renderResources;
    require(resources.size()<256,"Too many render resources");
    for(const auto& r:resources)require(r.second.key!=key,"Duplicate render resource");
    RenderResourceContent r;r.key=key;r.owner=ctx.index;r.descriptor=d;r.descriptor.key=nullptr;r.descriptor.path=nullptr;r.descriptor.data=nullptr;r.descriptor.indices=nullptr;
    require(d.data_bytes<=16*1024*1024&&d.index_count<=1024*1024,"Render resource too large");
    if(d.kind==MC_RENDER_SHADER) {
        const auto path=containedPath(m_info.at(ctx.index).root,text(d.path));
        const auto bytes=std::filesystem::file_size(path);require(bytes>=20&&bytes<=1024*1024&&bytes%4==0,"Invalid SPIR-V size");
        std::ifstream file(path,std::ios::binary);r.data.resize(bytes);file.read(reinterpret_cast<char*>(r.data.data()),static_cast<std::streamsize>(bytes));
        require(static_cast<bool>(file),"Cannot read SPIR-V");uint32_t magic=0;std::memcpy(&magic,r.data.data(),4);require(magic==0x07230203,"Invalid SPIR-V magic");
        std::vector<uint32_t> words(r.data.size()/4);std::memcpy(words.data(),r.data.data(),r.data.size());
        bool entry=false;
        for(size_t i=5;i<words.size();) {
            const uint32_t length=words[i]>>16,op=words[i]&65535;
            require(length&&length<=words.size()-i,"Malformed SPIR-V instruction");
            if(op==15){require(length>=5&&(words[i+1]==0||words[i+1]==4),"Only vertex/fragment shaders are supported");
                const char* name=reinterpret_cast<const char*>(&words[i+3]);const size_t bytes=(length-3)*4;
                require(std::memchr(name,0,bytes)&&std::string(name)=="main"&&!entry,"Shader requires one main entry point");r.shaderStage=words[i+1];entry=true;}
            if(op==71&&length>=4){const auto decoration=words[i+2];
                if(decoration==34)require(words[i+3]==0,"Shader descriptor set must be zero");
                if(decoration==33)require(words[i+3]<=1,"Shader bindings must be texture 0 or uniforms 1");
                if(decoration==30)require(words[i+3]<=1,"Shader supports position/UV and one color output");}
            i+=length;
        }
        require(entry,"Shader has no entry point");
    } else if(d.data_bytes) {
        require(d.data,"Missing render data");const auto* bytes=static_cast<const uint8_t*>(d.data);r.data.assign(bytes,bytes+d.data_bytes);
    }
    if(d.kind==MC_RENDER_TEXTURE||d.kind==MC_RENDER_TARGET)require(d.width&&d.height&&d.width<=8192&&d.height<=8192,"Invalid render dimensions");
    if(d.kind==MC_RENDER_TEXTURE)require(uint64_t(d.width)*d.height*4==r.data.size(),"Invalid texture byte count");
    if(d.kind==MC_RENDER_MESH){require(!r.data.empty()&&r.data.size()%sizeof(MC_RenderVertex)==0&&d.indices&&d.index_count&&d.index_count%3==0,"Invalid mesh data");
        r.indices.assign(d.indices,d.indices+d.index_count);for(auto index:r.indices)require(index<r.data.size()/sizeof(MC_RenderVertex),"Mesh index out of range");
        for(size_t offset=0;offset<r.data.size();offset+=4){float value;std::memcpy(&value,r.data.data()+offset,4);require(std::isfinite(value),"Nonfinite vertex");}}
    if(d.kind==MC_RENDER_BUFFER)require(!r.data.empty()&&r.data.size()<=16384,"Invalid uniform buffer");
    auto reference=[&](MC_RenderHandle handle,uint32_t kind){auto it=resources.find(handle);require(it!=resources.end()&&it->second.owner==ctx.index&&it->second.descriptor.kind==kind,"Invalid/foreign render reference");};
    if(d.kind==MC_RENDER_PIPELINE){reference(d.vertex_shader,MC_RENDER_SHADER);reference(d.fragment_shader,MC_RENDER_SHADER);require(resources.at(d.vertex_shader).shaderStage==0&&resources.at(d.fragment_shader).shaderStage==4,"Shader stage mismatch");if(d.texture&&d.texture!=MC_RENDER_SCENE_COLOR){auto it=resources.find(d.texture);require(it!=resources.end()&&it->second.owner==ctx.index&&(it->second.descriptor.kind==MC_RENDER_TEXTURE||it->second.descriptor.kind==MC_RENDER_TARGET),"Invalid/foreign pipeline texture");}require(d.depth_test<=1&&d.blend<=1,"Invalid pipeline flags");}
    size_t payload=r.data.size()+r.indices.size()*4;
    uint64_t targets=d.kind==MC_RENDER_TARGET?uint64_t(d.width)*d.height*4:0;
    for(const auto& previous:resources){payload+=previous.second.data.size()+previous.second.indices.size()*4;
        if(previous.second.descriptor.kind==MC_RENDER_TARGET)targets+=uint64_t(previous.second.descriptor.width)*previous.second.descriptor.height*4;}
    require(payload<=Config::PLUGIN_RENDER_DATA_BUDGET&&targets<=Config::PLUGIN_RENDER_TARGET_BUDGET,"Plugin render resource budget exceeded");
    id=resources.empty()?1:resources.rbegin()->first+1;resources.emplace(id,std::move(r));return 0;
}
int32_t PluginManager::renderPass(MC_PluginContext& ctx,const MC_RenderPass& d) {
    require(d.size>=sizeof(d)&&d.stage<MC_RENDER_STAGE_COUNT&&d.replace<=1&&d.callback,"Invalid render pass");
    const auto key=text(d.key);require(m_phase==MC_REGISTER&&!content().frozen,"Render registration is closed");
    require(validKey(key)&&key.rfind(m_info.at(ctx.index).manifest.id+":",0)==0,"Render key must use plugin namespace");
    for(const auto& p:content().extensions.renderPasses){require(p.key!=key,"Duplicate render pass");require(!(d.replace&&p.replace&&p.stage==d.stage),"Conflicting render replacement");}
    auto f=d.callback;auto u=d.userdata;auto* c=&ctx;const size_t i=ctx.index;
    RenderPassContent p;p.key=key;p.owner=i;p.stage=d.stage;p.replace=d.replace;p.priority=d.priority;
    p.callback=[this,f,u,c,i](const MC_RenderFrame& frame){
        if(!m_info[i].active||!content().runtimeFault.empty())return;
        const auto previous=m_phase;m_phase=MC_HUD;
        try{if(f(c,&frame,u))callbackFault(i,"Render callback failed");}
        catch(...){callbackFault(i,"Exception across render ABI");}
        m_phase=previous;
    };content().extensions.renderPasses.push_back(std::move(p));return 0;
}
const void* PluginManager::extension(MC_PluginContext*,const char* name,uint32_t version) {
    if(const auto* result=capability(name,version))return result;
    if(!name)return nullptr;
    if(version==1&&std::string(name)==MC_LOCALIZATION_EXTENSION) {
        static const MC_LocalizationV1 localization{sizeof(MC_LocalizationV1),1,
            [](MC_PluginContext* c,const char* directory){return call(c,[&]{owner(c).localizationBundle(*c,directory);},true);},
            [](MC_PluginContext* c,const char* key,const char* nameKey){return call(c,[&]{owner(c).localizedName(*c,key,nameKey);},true);}};
        return &localization;
    }
    if(version==3&&std::string(name)==MC_DIMENSIONS_EXTENSION) {
        static const MC_DimensionsV3 dimensions{sizeof(MC_DimensionsV3),3,
            [](MC_PluginContext* c,const MC_DimensionDefinitionV3* d,uint32_t* id){return call(c,[&]{require(d&&id,"Missing dimension V3 arguments");owner(c).dimensionDefinitionV3(*c,*d,*id);},true);},
            [](MC_PluginContext* c,const MC_DimensionTravelEntry* d){return call(c,[&]{require(d,"Missing travel entry");owner(c).bedTravel(*c,*d);},true);},
            static_cast<const MC_DimensionsV2*>(extension(nullptr,MC_DIMENSIONS_EXTENSION,2))};
        return &dimensions;
    }
    if(version==2&&std::string(name)==MC_DIMENSIONS_EXTENSION) {
        static const MC_DimensionsV2 dimensions{sizeof(MC_DimensionsV2),2,
            [](MC_PluginContext* c,const MC_DimensionDefinitionV2* d,uint32_t* id){return call(c,[&]{require(d&&id,"Missing dimension V2 arguments");owner(c).dimensionDefinitionV2(*c,*d,*id);},true);},
            static_cast<const MC_DimensionsV1*>(extension(nullptr,MC_DIMENSIONS_EXTENSION,1))};
        return &dimensions;
    }
    if(version==3&&std::string(name)==MC_WORLDGEN_EXTENSION) {
        static const MC_WorldgenV3 worldgen{sizeof(MC_WorldgenV3),3,
            [](MC_PluginContext* c,const MC_GeneratorDefinitionV3* d){return call(c,[&]{require(d,"Missing generator V3");owner(c).generatorV3(*c,*d);},true);},
            static_cast<const MC_WorldgenV2*>(extension(nullptr,MC_WORLDGEN_EXTENSION,2))};
        return &worldgen;
    }
    if(version==2&&std::string(name)==MC_WORLDGEN_EXTENSION) {
        static const MC_WorldgenV2 worldgen{sizeof(MC_WorldgenV2),2,
            [](MC_PluginContext* c,const MC_GeneratorDefinitionV2* d){return call(c,[&]{require(d,"Missing generator");owner(c).generatorV2(*c,*d);},true);},
            [](MC_PluginContext* c,const MC_ModifierDefinitionV2* d){return call(c,[&]{require(d,"Missing modifier");owner(c).modifierV2(*c,*d);},true);}};
        return &worldgen;
    }
    if(version!=1)return nullptr;
    static const MC_BlockShapesV1 shapes{sizeof(MC_BlockShapesV1),1,
        [](MC_PluginContext* c,const MC_BlockFamily* d,uint16_t* id){return call(c,[&]{require(d&&id,"Missing family arguments");owner(c).blockFamily(*c,*d,*id);},true);},
        [](MC_PluginContext* c,const MC_BlockGeometry* d){return call(c,[&]{require(d,"Missing geometry arguments");owner(c).blockGeometry(*c,*d);},true);},
        [](MC_PluginContext* c,uint16_t id,MC_BlockState* out){return call(c,[&]{require(out&&out->size>=sizeof(*out),"Invalid state output");*out=describeState(static_cast<BlockId>(id));});},
        [](MC_PluginContext* c,const MC_BlockState* state,uint16_t* out){return call(c,[&]{require(state&&out,"Missing state arguments");*out=static_cast<uint16_t>(composeState(*state));});},
        [](MC_PluginContext* c,uint16_t id,uint32_t group,MC_BlockBox* out,uint32_t capacity,uint32_t* count){return call(c,[&]{require(validBlock(static_cast<BlockId>(id))&&group<=2&&count,"Invalid boxes query");const auto block=static_cast<BlockId>(id);const auto boxes=group==0?blockCollisionBoxes(block):group==1?blockSelectionBoxes(block):blockModelBoxes(block);*count=boxes.count;if(!out)return;require(capacity>=boxes.count,"Box buffer too small");for(uint32_t i=0;i<boxes.count;++i)for(int axis=0;axis<3;++axis){out[i].min[axis]=boxes.boxes[i].min[axis];out[i].max[axis]=boxes.boxes[i].max[axis];}});},
        [](MC_PluginContext* c,uint32_t dim,int32_t x,int32_t y,int32_t z,uint32_t open){return call(c,[&]{require(validDimension(dim)&&open<=1&&Config::isValidWorldY(y),"Invalid door operation");auto& m=owner(c);m.enqueue(*c,[&m,dim,x,y,z,open]{return m.blockInteraction&&m.blockInteraction(dim,x,y,z,false,open);});});},
        [](MC_PluginContext* c,uint32_t dim,int32_t x,int32_t y,int32_t z){return call(c,[&]{require(validDimension(dim)&&Config::isValidWorldY(y),"Invalid button operation");auto& m=owner(c);m.enqueue(*c,[&m,dim,x,y,z]{return m.blockInteraction&&m.blockInteraction(dim,x,y,z,true,true);});});}};
    static const MC_EntitiesV1 entities{sizeof(MC_EntitiesV1),1,
        [](MC_PluginContext* c,const MC_EntityDefinition* d,uint16_t* id){return call(c,[&]{require(d&&id,"Missing entity arguments");owner(c).entity(*c,*d,*id);},true);},
        [](MC_PluginContext* c,const char* key)->uint16_t{uint16_t id=MC_INVALID_ID;call(c,[&]{id=resolveEntity(text(key));});return id;},
        [](MC_PluginContext* c,uint32_t dim,uint16_t type,const double* p){return call(c,[&]{require(dim<=1&&validEntity(type)&&p,"Invalid entity spawn");std::array<double,3> copy;for(size_t i=0;i<3;++i){require(std::isfinite(p[i])&&std::abs(p[i])<=30000000,"Invalid entity position");copy[i]=p[i];}
            auto& m=owner(c);m.enqueue(*c,[&m,dim,type,copy]{return m.spawnEntity&&m.spawnEntity(dim,type,copy.data());});});},
        [](MC_PluginContext* c,uint32_t dim,uint64_t id,MC_EntitySnapshot* out,char* state,uint32_t cap){return call(c,[&]{require(dim<=1&&out&&out->size>=sizeof(*out),"Invalid entity snapshot arguments");auto& m=owner(c);std::string data;
            require(m.entitySnapshot&&m.entitySnapshot(dim,id,*out,data),"Entity unavailable");require(state&&cap>data.size(),"Entity state buffer too small");std::memcpy(state,data.c_str(),data.size()+1);out->state=state;});},
        [](MC_PluginContext* c,uint32_t dim,uint64_t id){return call(c,[&]{require(validDimension(dim),"Invalid dimension");auto& m=owner(c);m.enqueue(*c,[&m,dim,id]{return m.removeEntity&&m.removeEntity(dim,id);});});},
        [](MC_PluginContext* c,uint32_t dim,uint64_t id,const MC_EntityIntent* intent){return call(c,[&]{require(dim<=1&&intent&&intent->size>=sizeof(*intent),"Invalid entity intent");auto copy=*intent;std::string state=intent->state?intent->state:"";require(state.size()<=MC_ENTITY_STATE_LIMIT,"Entity state too large");if(!state.empty())Json::parse(state);
            for(float v:copy.movement)require(std::isfinite(v)&&std::abs(v)<=32,"Invalid entity movement");
            require(copy.jump<=1,"Invalid jump flag");auto& m=owner(c);
            m.enqueue(*c,[&m,dim,id,copy,state]()mutable{copy.state=state.empty()?nullptr:state.c_str();return m.entityIntent&&m.entityIntent(dim,id,copy);});});}
        ,[](MC_PluginContext* c,const MC_SpawnEggDefinition* d,uint16_t* id){return call(c,[&]{require(d&&d->size>=sizeof(*d)&&id,"Invalid spawn egg descriptor");
            const auto type=resolveEntity(text(d->entity));require(type!=0&&type!=9&&type!=10,"Invalid spawn egg mob");
            auto item=d->item;item.kind=MC_MATERIAL;owner(c).item(*c,item,*id);
            auto& entry=content().items.at(*id);entry.properties.kind=ItemKind::SpawnEgg;entry.spawnEntity=type;
        },true);}
    };
    static const MC_WorldgenV1 worldgen{sizeof(MC_WorldgenV1),1,
        [](MC_PluginContext* c,const MC_GeneratorDefinition* d){return call(c,[&]{require(d,"Missing generator");owner(c).generator(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_ModifierDefinition* d){return call(c,[&]{require(d,"Missing modifier");owner(c).modifier(*c,*d);},true);}};
    static const MC_RenderV1 render{sizeof(MC_RenderV1),1,
        [](MC_PluginContext* c,const MC_RenderResource* d,MC_RenderHandle* id){return call(c,[&]{require(d&&id,"Missing render arguments");owner(c).renderResource(*c,*d,*id);},true);},
        [](MC_PluginContext* c,const MC_RenderPass* d){return call(c,[&]{require(d,"Missing render pass");owner(c).renderPass(*c,*d);},true);},
        [](MC_PluginContext* c,const MC_RenderDraw* d){return call(c,[&]{auto& m=owner(c);require(d&&d->size>=sizeof(*d)&&m.renderDraw&&m.renderDraw(c->index,*d),"Render draw unavailable/invalid");});},
        [](MC_PluginContext* c,uint64_t index,MC_RenderHandle pipeline){return call(c,[&]{auto& m=owner(c);require(m.renderScene&&m.renderScene(c->index,index,pipeline),"Scene submission unavailable/invalid");});},
        [](MC_PluginContext* c,MC_RenderHandle buffer,const void* data,uint32_t size){return call(c,[&]{auto& m=owner(c);require(data&&m.renderBuffer&&m.renderBuffer(c->index,buffer,data,size),"Render buffer unavailable/invalid");});}};
    static const MC_DimensionsV1 dimensions{sizeof(MC_DimensionsV1),1,
        [](MC_PluginContext* c,const MC_DimensionDefinition* d,uint32_t* id){return call(c,[&]{require(d&&id,"Missing dimension arguments");owner(c).dimensionDefinition(*c,*d,*id);},true);},
        [](MC_PluginContext* c,const char* key)->uint32_t {uint32_t result=MC_INVALID_DIMENSION;call(c,[&]{result=resolveDimension(text(key));});return result;},
        [](MC_PluginContext* c,uint32_t id,MC_DimensionDefinition* out){return call(c,[&]{require(content().frozen&&out&&out->size>=sizeof(*out),"Dimension query requires frozen registry and sized output");const auto& d=dimensionContent(id);
            *out={};out->size=sizeof(*out);out->key=d.key.c_str();out->name=d.name.c_str();out->revision=d.revision.c_str();out->generator=d.generation.generator.c_str();out->generator_revision=d.generation.revision.c_str();out->parameters=d.generation.parameters.c_str();out->weather=d.weather;out->disable_natural_spawning=!d.natural;out->spawn=d.spawn;std::copy(d.position.begin(),d.position.end(),out->spawn_position);out->death_target=d.deathTarget.c_str();out->void_target=d.voidTarget.c_str();if(d.modifierView.empty())for(const auto& m:d.generation.modifiers)d.modifierView.push_back({m.first.c_str(),m.second.c_str()});out->modifiers=d.modifierView.data();out->modifier_count=static_cast<uint32_t>(d.modifierView.size());});},
        [](MC_PluginContext* c,uint64_t player,uint32_t dim){return call(c,[&]{require(validDimension(dim),"Invalid dimension");auto& m=owner(c);require(context().player==0||context().player==player,"Cannot travel another player as guest");m.enqueue(*c,[&m,player,dim]{return m.travelPlayer&&m.travelPlayer(player,dim);});});}};
    const std::string key=name;
    if(key==MC_BLOCK_SHAPES_EXTENSION)return &shapes;
    if(key==MC_DIMENSIONS_EXTENSION)return &dimensions;
    if(key==MC_ENTITIES_EXTENSION)return &entities;
    if(key==MC_WORLDGEN_EXTENSION)return &worldgen;
    if(key==MC_RENDER_EXTENSION)return &render;
    return nullptr;
}
} // namespace Plugins
