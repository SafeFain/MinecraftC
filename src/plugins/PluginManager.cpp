#include "plugins/PluginManager.h"
#include "core/AssetStore.h"
#include "debug/Log.h"
#include "Config.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>
#include <stb_image.h>
#ifndef MINECRAFTC_PLUGIN_GAME_VERSION
#define MINECRAFTC_PLUGIN_GAME_VERSION "0.0.0"
#endif

namespace Plugins {
namespace {
std::string required(const char* s) {if(!s||!*s)throw std::runtime_error("Missing SDK string");return s;}
std::string optional(const char* s) {return s?s:"";}
PluginManager& manager(MC_PluginContext* c) {if(!c||!c->manager)throw std::runtime_error("Invalid plugin context");return *static_cast<PluginManager*>(c->manager);}
template<class F> int32_t checked(MC_PluginContext* c,F&& f) {
    try {if(!c)return -1;if(!manager(c).hostThread())return -1;c->error.clear();f();return 0;}catch(const std::exception& e){if(c)c->error=e.what();return -1;}
}
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void hudColor(const float* color) {
    check(color,"Missing HUD color");
    for(int i=0;i<4;++i)check(std::isfinite(color[i])&&color[i]>=0&&color[i]<=1,"Invalid HUD color");
}
std::vector<std::string> strings(const Json& j,const std::string& field) {
    std::vector<std::string> result;if(!j.has(field))return result;
    check(j.at(field).type==Json::Type::Array,"Expected JSON array");
    for(const auto& v:j.at(field).array)result.push_back(v.text());
    return result;
}
bool versionMatches(const std::string& version,const std::string& minimum,const std::string& maximum) {
    const auto v=Version::parse(version);
    return (minimum.empty()||!(v<Version::parse(minimum)))&&(maximum.empty()||v<Version::parse(maximum));
}
std::string fingerprint(const std::filesystem::path& root,const std::string& builtin) {
    uint64_t h=1469598103934665603ULL;
    auto hash=[&](const std::string& s){for(unsigned char c:s){h^=c;h*=1099511628211ULL;}};
    hash(builtin);
    if(!root.empty()) {
        std::vector<std::filesystem::path> files;
        for(const auto& file:std::filesystem::recursive_directory_iterator(root)) {
            check(!file.is_symlink(),"Plugin packages cannot contain symlinks");
            if(file.is_regular_file())files.push_back(file.path());
        }
        std::sort(files.begin(),files.end());
        for(const auto& path:files) {
            hash(path.lexically_relative(root).generic_string());
            std::ifstream file(path,std::ios::binary);check(static_cast<bool>(file),"Cannot hash plugin file");
            char buffer[8192];while(file.read(buffer,sizeof(buffer))||file.gcount())hash(std::string(buffer,static_cast<size_t>(file.gcount())));
        }
    }
    std::ostringstream out;out<<std::hex<<h;return out.str();
}
Json readJson(const std::filesystem::path& p) {check(std::filesystem::file_size(p)<=16*1024*1024,"Plugin JSON is too large");std::ifstream file(p,std::ios::binary);check(static_cast<bool>(file),"Cannot read plugin JSON");std::string data((std::istreambuf_iterator<char>(file)),{});return Json::parse(data);}
void writeText(const std::filesystem::path& path,const std::string& value) {
    std::filesystem::create_directories(path.parent_path());auto temporary=path;temporary+=".tmp";
    {std::ofstream f(temporary,std::ios::binary|std::ios::trunc);f<<value;f.flush();check(static_cast<bool>(f),"Cannot write plugin configuration");}
    std::error_code error;if(!Platform::replaceFileAtomically(temporary,path,error))throw std::runtime_error("Cannot replace plugin configuration: "+error.message());
}
}
Version Version::parse(const std::string& text) {
    check(!text.empty()&&text.size()<=128,"Invalid semantic version");
    const size_t suffix=text.find_first_of("-+");const std::string core=text.substr(0,suffix);
    std::vector<std::string> parts;std::istringstream stream(core);std::string part;
    while(std::getline(stream,part,'.'))parts.push_back(part);
    check(parts.size()==3&&!core.empty()&&core.back()!='.',"Version requires major.minor.patch");
    auto number=[](const std::string& value) {
        check(!value.empty()&&(value.size()==1||value[0]!='0'),"Invalid numeric version identifier");
        uint64_t n=0;for(char c:value){check(c>='0'&&c<='9',"Invalid numeric version identifier");n=n*10+static_cast<uint32_t>(c-'0');check(n<=UINT32_MAX,"Version component overflow");}
        return static_cast<uint32_t>(n);
    };
    Version result;result.major=number(parts[0]);result.minor=number(parts[1]);result.patch=number(parts[2]);
    auto identifiers=[&](const std::string& value,bool prerelease) {
        check(!value.empty()&&value.back()!='.',"Empty semantic version suffix");std::istringstream segments(value);
        while(std::getline(segments,part,'.')) {
            check(!part.empty(),"Empty semantic version identifier");bool numeric=true;
            for(char c:part){check((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-',"Invalid version suffix");numeric=numeric&&c>='0'&&c<='9';}
            if(prerelease){check(!numeric||part.size()==1||part[0]!='0',"Numeric prerelease has leading zero");result.prerelease.push_back(part);}
        }
    };
    if(suffix!=std::string::npos) {
        const size_t build=text.find('+',suffix);
        if(text[suffix]=='-')identifiers(text.substr(suffix+1,build==std::string::npos?std::string::npos:build-suffix-1),true);
        if(build!=std::string::npos)identifiers(text.substr(build+1),false);
    }
    return result;
}
bool Version::operator<(const Version& other) const {
    const auto core=std::tie(major,minor,patch),rhs=std::tie(other.major,other.minor,other.patch);
    if(core!=rhs)return core<rhs;
    if(prerelease.empty()||other.prerelease.empty())return !prerelease.empty()&&other.prerelease.empty();
    auto numeric=[](const std::string& s){return std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});};
    for(size_t i=0;i<std::min(prerelease.size(),other.prerelease.size());++i) {
        const auto& a=prerelease[i];const auto& b=other.prerelease[i];if(a==b)continue;
        const bool an=numeric(a),bn=numeric(b);if(an!=bn)return an;
        if(an&&a.size()!=b.size())return a.size()<b.size();
        return a<b;
    }
    return prerelease.size()<other.prerelease.size();
}
Manifest Manifest::parse(const Json& j) {
    Manifest m;m.id=j.at("id").text();m.name=j.text("name",m.id);m.version=j.at("version").text();Version::parse(m.version);
    check(validKey(m.id+":test")&&m.id!="minecraftc","Invalid/reserved plugin ID");
    check(j.numeric("api",0)==MC_PLUGIN_ABI,"Unsupported plugin API");
    m.visual=j.flag("visual_only",false);m.defaultEnabled=j.flag("enabled_by_default",true);
    m.gameMinimum=j.text("game_min");m.gameMaximum=j.text("game_max");m.data=j.text("data");
    if(j.has("native")) {
        const auto& native=j.at("native");std::string platform;
        switch(currentDesktopPlatform()) {case DesktopPlatform::Linux:platform="linux";break;case DesktopPlatform::Windows:platform="windows";break;case DesktopPlatform::MacOS:platform="macos";break;case DesktopPlatform::Android:platform="android";break;case DesktopPlatform::IOS:platform="ios";break;}
        m.entry=native.text(platform);
        if(m.entry.empty())m.entry="!unsupported";
    }
    if(j.has("dependencies")) {
        check(j.at("dependencies").type==Json::Type::Array,"Expected dependency array");
        for(const auto& d:j.at("dependencies").array) {
            Dependency dep{d.at("id").text(),d.text("min"),d.text("max"),d.flag("optional",false)};
            if(!dep.minimum.empty())Version::parse(dep.minimum);
            if(!dep.maximum.empty())Version::parse(dep.maximum);
            m.dependencies.push_back(std::move(dep));
        }
    }
    m.before=strings(j,"before");m.after=strings(j,"after");m.conflicts=strings(j,"conflicts");
    return m;
}
std::filesystem::path PluginManager::containedPath(const std::filesystem::path& root,const std::string& relative) {
    check(AssetStore::validPath(relative),"Plugin path must be relative and contained");
    const auto base=std::filesystem::weakly_canonical(root),path=std::filesystem::weakly_canonical(base/relative);
    auto a=base.begin(),b=path.begin();for(;a!=base.end();++a,++b)check(b!=path.end()&&*a==*b,"Plugin path escapes package");
    return path;
}
std::vector<size_t> PluginManager::resolve(std::vector<PluginInfo>& info) {
    std::map<std::string,std::vector<size_t>> byId;
    for(size_t i=0;i<info.size();++i)if(info[i].enabled&&info[i].status.empty())byId[info[i].manifest.id].push_back(i);
    for(const auto& entry:byId)if(entry.second.size()>1)for(size_t i:entry.second)info[i].status="Duplicate plugin ID: "+entry.first;
    for(auto& p:info)if(p.enabled&&p.status.empty()&&!versionMatches(MINECRAFTC_PLUGIN_GAME_VERSION,p.manifest.gameMinimum,p.manifest.gameMaximum))p.status="Incompatible game version";
    bool changed=true;
    while(changed) {
        changed=false;
        for(auto& p:info) {
            if(!p.enabled||!p.status.empty())continue;
            auto available=[&](const std::string& id)->const PluginInfo* {
                auto it=byId.find(id);if(it==byId.end()||it->second.size()!=1)return nullptr;
                const auto& dep=info[it->second[0]];return dep.enabled&&dep.status.empty()?&dep:nullptr;
            };
            for(const auto& dep:p.manifest.dependencies) {
                const auto* target=available(dep.id);
                if(!target&&dep.optional)continue;
                if(!target||!versionMatches(target->manifest.version,dep.minimum,dep.maximum)) {p.status="Missing/incompatible dependency: "+dep.id;changed=true;break;}
            }
            for(const auto& id:p.manifest.conflicts)if(available(id)){p.status="Conflicting plugin: "+id;changed=true;break;}
        }
    }
    std::map<std::string,size_t> ready;
    for(size_t i=0;i<info.size();++i)if(info[i].enabled&&info[i].status.empty())ready[info[i].manifest.id]=i;
    std::map<size_t,std::set<size_t>> edges;
    std::map<size_t,size_t> indegree;for(const auto& p:ready)indegree[p.second]=0;
    auto edge=[&](size_t a,size_t b){if(edges[a].insert(b).second)++indegree[b];};
    for(const auto& p:ready) {
        const auto& m=info[p.second].manifest;
        for(const auto& d:m.dependencies)if(ready.count(d.id))edge(ready.at(d.id),p.second);
        for(const auto& id:m.after)if(ready.count(id))edge(ready.at(id),p.second);
        for(const auto& id:m.before)if(ready.count(id))edge(p.second,ready.at(id));
    }
    std::vector<size_t> order;
    while(order.size()<ready.size()) {
        auto next=ready.end();for(auto it=ready.begin();it!=ready.end();++it)if(indegree[it->second]==0){next=it;break;}
        if(next==ready.end())break;
        const size_t i=next->second;order.push_back(i);indegree[i]=SIZE_MAX;
        for(size_t target:edges[i])--indegree[target];
    }
    for(const auto& entry:ready)if(indegree[entry.second]!=SIZE_MAX)info[entry.second].status="Dependency/order cycle or blocked by cycle";
    return order;
}
PluginManager::PluginManager(RuntimePaths paths):m_paths(std::move(paths)) {
    m_host.size=sizeof(m_host);m_host.abi=MC_PLUGIN_ABI;
    m_host.log=[](MC_PluginContext* c,uint32_t level,const char* msg){if(c&&msg){if(level>=2)LOG_WARN("Plugin: "<<msg);else LOG_INFO("Plugin: "<<msg);}};
    m_host.last_error=[](MC_PluginContext* c)->const char* {return c?c->error.c_str():"Invalid context";};
    m_host.register_block=[](MC_PluginContext* c,const MC_Block* v,uint16_t* id){const int32_t result=checked(c,[&]{check(v&&id,"Invalid block arguments");manager(c).block(*c,*v,*id);});if(result&&c)c->registrationFailed=true;return result;};
    m_host.register_item=[](MC_PluginContext* c,const MC_Item* v,uint16_t* id){const int32_t result=checked(c,[&]{check(v&&id,"Invalid item arguments");manager(c).item(*c,*v,*id);});if(result&&c)c->registrationFailed=true;return result;};
    m_host.register_material=[](MC_PluginContext* c,const MC_Material* v){const int32_t result=checked(c,[&]{check(v,"Invalid material");manager(c).material(*c,*v);});if(result&&c)c->registrationFailed=true;return result;};
    m_host.register_recipe=[](MC_PluginContext* c,const MC_Recipe* v){const int32_t result=checked(c,[&]{check(v,"Invalid recipe");manager(c).recipe(*c,*v);});if(result&&c)c->registrationFailed=true;return result;};
    m_host.subscribe=[](MC_PluginContext* c,uint32_t k,int32_t p,MC_EventCallback f,void* u){const int32_t result=checked(c,[&]{manager(c).subscribe(*c,k,p,f,u);});if(result&&c)c->registrationFailed=true;return result;};
    m_host.find_block=[](MC_PluginContext* c,const char* key)->uint16_t{uint16_t id=MC_INVALID_ID;checked(c,[&]{id=static_cast<uint16_t>(resolveBlock(required(key)));});return id;};
    m_host.find_item=[](MC_PluginContext* c,const char* key)->uint16_t{uint16_t id=MC_INVALID_ID;checked(c,[&]{id=static_cast<uint16_t>(resolveItem(required(key)));});return id;};
    m_host.player=[](MC_PluginContext* c,MC_PlayerSnapshot* out){return checked(c,[&]{check(out&&out->size>=sizeof(*out),"Invalid snapshot");auto& m=manager(c);check(m.operations.player&&m.operations.player(*out),"No active world");});};
    m_host.get_block=[](MC_PluginContext* c,int32_t x,int32_t y,int32_t z,uint16_t* id){return checked(c,[&]{auto& m=manager(c);check(id&&m.operations.getBlock&&m.operations.getBlock(x,y,z,*id),"Block is not loaded");});};
    m_host.set_block=[](MC_PluginContext* c,int32_t x,int32_t y,int32_t z,uint16_t id){return checked(c,[&]{auto& m=manager(c);check(validBlock(static_cast<BlockId>(id)),"Invalid block");m.enqueue(*c,[&m,x,y,z,id]{return m.operations.setBlock&&m.operations.setBlock(x,y,z,id);});});};
    m_host.give_item=[](MC_PluginContext* c,uint16_t id,uint32_t count){return checked(c,[&]{auto& m=manager(c);check(isValidItemId(static_cast<ItemId>(id))&&id&&count>0&&count<=4096,"Invalid item/count");m.enqueue(*c,[&m,id,count]{return m.operations.giveItem&&m.operations.giveItem(id,count);});});};
    m_host.register_command=[](MC_PluginContext* c,const char* name,MC_EventCallback f,void* u){const int32_t result=checked(c,[&]{manager(c).addCommand(*c,name,f,u);});if(result&&c)c->registrationFailed=true;return result;};
    m_host.hud_rect=[](MC_PluginContext* c,float x,float y,float w,float h,const float* color){return checked(c,[&]{auto& m=manager(c);hudColor(color);check(m.m_hudOperations++<Config::PLUGIN_MAX_HUD_COMMANDS,"HUD command budget exceeded");check(m.m_phase==MC_HUD&&color&&m.operations.hudRect,"HUD drawing outside HUD event");check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(w)&&std::isfinite(h)&&w>=0&&h>=0,"Invalid HUD bounds");m.operations.hudRect(x,y,w,h,{color[0],color[1],color[2],color[3]});});};
    m_host.hud_text=[](MC_PluginContext* c,float x,float y,const char* text,const float* color){return checked(c,[&]{auto& m=manager(c);hudColor(color);check(m.m_hudOperations++<Config::PLUGIN_MAX_HUD_COMMANDS,"HUD command budget exceeded");check(m.m_phase==MC_HUD&&color&&text&&m.operations.hudText,"HUD drawing outside HUD event");check(std::isfinite(x)&&std::isfinite(y)&&std::string(text).size()<=4096,"Invalid HUD text");m.operations.hudText(x,y,text,{color[0],color[1],color[2],color[3]});});};
    m_host.hud_image=[](MC_PluginContext* c,float x,float y,float w,float h,const char* key,const float* color){return checked(c,[&]{auto& m=manager(c);hudColor(color);check(m.m_hudOperations++<Config::PLUGIN_MAX_HUD_COMMANDS,"HUD command budget exceeded");check(m.m_phase==MC_HUD&&color&&m.operations.hudImage,"HUD drawing outside HUD event");check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(w)&&std::isfinite(h)&&w>=0&&h>=0,"Invalid HUD bounds");auto it=content().materials.find(required(key));check(it!=content().materials.end(),"Unknown HUD material");uint16_t tile=it->second.tile;m.operations.hudImage(x,y,w,h,tile,{color[0],color[1],color[2],color[3]});});};
    m_host.config_read=[](MC_PluginContext* c,char* out,uint32_t cap){return checked(c,[&]{manager(c).storage(*c,false,false,nullptr,out,cap);});};
    m_host.config_write=[](MC_PluginContext* c,const char* value){return checked(c,[&]{manager(c).storage(*c,false,true,value,nullptr,0);});};
    m_host.world_data_read=[](MC_PluginContext* c,char* out,uint32_t cap){return checked(c,[&]{manager(c).storage(*c,true,false,nullptr,out,cap);});};
    m_host.world_data_write=[](MC_PluginContext* c,const char* value){return checked(c,[&]{manager(c).storage(*c,true,true,value,nullptr,0);});};
}
PluginManager::~PluginManager(){shutdown();}
void PluginManager::owned(MC_PluginContext& ctx,const std::string& key) const {
    check(m_phase==MC_REGISTER&&!content().frozen,"Registration is closed");
    check(validKey(key)&&key.rfind(m_info.at(ctx.index).manifest.id+":",0)==0,"Content must use its plugin namespace");
    check(!m_info.at(ctx.index).manifest.visual,"Visual-only plugins cannot register gameplay content");
}
int32_t PluginManager::block(MC_PluginContext& ctx,const MC_Block& v,uint16_t& out) {
    check(v.size>=sizeof(v),"Block SDK size mismatch");BlockContent b;b.key=required(v.key);owned(ctx,b.key);b.name=required(v.name);
    check(v.solid<=1&&v.cross<=1&&v.layer<=2&&v.emission<=15&&v.preferred_tool<=5&&v.minimum_tier<=5,"Invalid block properties");
    check(std::isfinite(v.hardness)&&v.hardness>=0&&std::isfinite(v.alpha)&&v.alpha>=0&&v.alpha<=1,"Invalid block values");
    for(float color:v.color)check(std::isfinite(color)&&color>=0&&color<=1,"Invalid block color");
    b.properties.color={v.color[0],v.color[1],v.color[2]};b.properties.solid=v.solid!=0;b.properties.transparent=v.layer!=0;
    b.properties.shape=v.cross?RenderShape::Cross:RenderShape::Cube;b.properties.layer=static_cast<RenderLayer>(v.layer);b.properties.alpha=v.alpha;
    b.survival={v.hardness,static_cast<ToolKind>(v.preferred_tool),static_cast<ToolTier>(v.minimum_tier),v.unbreakable!=0};b.emission=static_cast<uint8_t>(v.emission);
    for(size_t i=0;i<6;++i)b.materials[i]=required(v.materials[i]);
    b.dropKey=optional(v.drop_item);
    out=static_cast<uint16_t>(registerBlock(std::move(b)));return 0;
}
int32_t PluginManager::item(MC_PluginContext& ctx,const MC_Item& v,uint16_t& out) {
    check(v.size>=sizeof(v),"Item SDK size mismatch");ItemContent item;item.key=required(v.key);owned(ctx,item.key);item.properties.name=required(v.name);
    check(v.kind<=static_cast<uint32_t>(ItemKind::SpawnEgg)&&v.category<static_cast<uint32_t>(CreativeItemCategory::Count)&&v.tool<=5&&v.tier<=5&&v.food<=20&&v.durability<=65535,"Invalid item properties");
    check(v.kind!=MC_ARMOR&&v.kind!=MC_SPAWN_EGG,"Armor and spawn eggs are not supported by ABI 1");
    check(v.max_stack>0&&v.max_stack<=64&&std::isfinite(v.saturation)&&v.saturation>=0&&std::isfinite(v.attack_damage)&&v.attack_damage>=0&&std::isfinite(v.attack_speed)&&v.attack_speed>=0,"Invalid item values");
    item.properties.kind=static_cast<ItemKind>(v.kind);item.properties.maxStack=static_cast<uint8_t>(v.max_stack);item.properties.maxDurability=static_cast<uint16_t>(v.durability);
    item.properties.tool=static_cast<ToolKind>(v.tool);item.properties.tier=static_cast<ToolTier>(v.tier);item.properties.food=static_cast<uint8_t>(v.food);item.properties.saturation=v.saturation;item.properties.attackDamage=v.attack_damage;item.properties.attackSpeed=v.attack_speed;
    item.material=required(v.material);item.category=static_cast<CreativeItemCategory>(v.category);
    if(v.placed_block&&*v.placed_block)item.properties.placedBlock=resolveBlock(v.placed_block);
    out=static_cast<uint16_t>(registerItem(std::move(item)));return 0;
}
int32_t PluginManager::material(MC_PluginContext& ctx,const MC_Material& v) {
    check(v.size>=sizeof(v)&&m_phase==MC_REGISTER&&!content().frozen,"Material registration is closed");MaterialContent m;m.key=required(v.key);m.replace=v.replace!=0;
    const auto& info=m_info.at(ctx.index);
    check(m.replace||(validKey(m.key)&&m.key.rfind(info.manifest.id+":",0)==0),"Material must use its plugin namespace");
    bool foundation=false;
    for(uint16_t i=0;i<static_cast<uint16_t>(BlockTexture::Count);++i)if(m.key==getBlockTextureAssetName(static_cast<BlockTexture>(i)))foundation=true;
    if(m.replace)check(foundation||content().materials.count(m.key),"Unknown replacement material");
    else check(!foundation&&!content().materials.count(m.key),"Duplicate material");
    m.replace=foundation;
    if(v.image&&*v.image)m.image=containedPath(info.root,v.image);
    if(v.normal&&*v.normal)m.normal=containedPath(info.root,v.normal);
    if(v.properties&&*v.properties)m.properties=containedPath(info.root,v.properties);
    for(const auto& image:{m.image,m.normal,m.properties}) {
        if(image.empty())continue;
        check(std::filesystem::file_size(image)<=1024*1024,"Material image exceeds size limit");
        std::ifstream file(image,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        check(bytes.size()>=8&&bytes[0]==137&&bytes[1]=='P'&&bytes[2]=='N'&&bytes[3]=='G',"Material must be PNG");
        int w=0,h=0,channels=0;stbi_uc* decoded=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&channels,4);
        const bool valid=decoded&&w==16&&h==16;stbi_image_free(decoded);check(valid,"Material must be a valid 16x16 PNG");
    }
    m.color={v.color[0],v.color[1],v.color[2],v.color[3]};
    for(size_t i=0;i<4;++i)check(std::isfinite(v.color[i])&&v.color[i]>=0&&v.color[i]<=1,"Invalid material color");
    if(m.replace)LOG_INFO("Plugin "<<info.manifest.id<<" replaces material "<<m.key);
    content().materials[m.key]=std::move(m);return 0;
}
int32_t PluginManager::recipe(MC_PluginContext& ctx,const MC_Recipe& v) {
    check(v.size>=sizeof(v)&&v.operation<=2,"Invalid recipe descriptor");const auto key=required(v.key);owned(ctx,key);
    auto& c=content();const std::string target=optional(v.target);
    if(v.operation) {
        check(validKey(target),"Recipe patch needs explicit target");
        if(v.smelting) {check(c.smelting.count(target)||isBuiltinRecipeKey(target,true),"Unknown recipe target");c.smelting.erase(target);c.removeSmelting.push_back(target);}
        else {check(c.crafting.count(target)||isBuiltinRecipeKey(target,false),"Unknown recipe target");c.crafting.erase(target);c.removeCrafting.push_back(target);}
        LOG_INFO("Plugin "<<m_info[ctx.index].manifest.id<<" patches recipe "<<target);
        if(v.operation==2)return 0;
    }
    check(v.count>0&&v.count<=64,"Invalid recipe output count");ItemStack output{resolveItem(required(v.output)),static_cast<uint8_t>(v.count),0};
    check(output.count<=getItemProps(output.id).maxStack,"Recipe output exceeds stack limit");
    if(v.smelting) {
        check(v.cook_ticks>0&&v.cook_ticks<=65535,"Invalid cook duration");
        check(c.smelting.emplace(key,SmeltingRecipe{resolveItem(required(v.ingredients[0])),output,static_cast<uint16_t>(v.cook_ticks)}).second,"Duplicate smelting recipe");
    } else {
        check(v.width>0&&v.width<=3&&v.height>0&&v.height<=3,"Invalid recipe dimensions");CraftingRecipe r;r.width=static_cast<uint8_t>(v.width);r.height=static_cast<uint8_t>(v.height);r.output=output;r.allowMirror=v.mirror!=0;
        for(uint32_t i=0;i<v.width*v.height;++i)if(v.ingredients[i]&&*v.ingredients[i])r.ingredients[i]=resolveItem(v.ingredients[i]);
        check(c.crafting.emplace(key,r).second,"Duplicate crafting recipe");
    }
    return 0;
}
int32_t PluginManager::subscribe(MC_PluginContext& c,uint32_t kind,int32_t priority,MC_EventCallback callback,void* data) {
    check(m_phase==MC_REGISTER&&!content().frozen&&kind<MC_EVENT_COUNT&&callback,"Invalid/late event subscription");
    if(m_info[c.index].manifest.visual)check(kind<=MC_SHUTDOWN||kind==MC_ENVIRONMENT||kind==MC_HUD,"Visual-only plugin requested gameplay event");
    m_listeners.push_back({c.index,kind,priority,callback,data});return 0;
}
int32_t PluginManager::addCommand(MC_PluginContext& c,const char* name,MC_EventCallback callback,void* data) {
    const std::string key=required(name);owned(c,key);check(callback&&m_commands.emplace(key,Command{c.index,callback,data}).second,"Invalid/duplicate command");return 0;
}
int32_t PluginManager::enqueue(MC_PluginContext& c,std::function<bool()> operation) {
    check(content().runtimeFault.empty(),"Plugin runtime requires restart");
    check(m_world&&m_phase!=MC_ENVIRONMENT&&m_phase!=MC_HUD&&m_phase!=MC_WORLD_CLOSE,"World mutations are unavailable in this phase");
    check(!m_info[c.index].manifest.visual,"Visual-only plugins cannot modify gameplay");
    check(m_queue.size()<Config::PLUGIN_MAX_OPERATIONS,"Plugin operation queue is full");m_queue.push_back(std::move(operation));return 0;
}
int32_t PluginManager::storage(MC_PluginContext& c,bool world,bool write,const char* input,char* output,uint32_t capacity) {
    const auto& info=m_info.at(c.index);std::filesystem::path path;
    if(world) {
        check(m_world&&operations.worldDirectory,"No active world");
        check(!info.manifest.visual,"Visual-only plugins have no persisted world state");
        path=operations.worldDirectory()/"plugin_data"/(info.manifest.id+".json");
    } else path=m_paths.dataRoot/"config"/"plugins"/(info.manifest.id+".json");
    if(write) {std::string text=required(input);check(text.size()<=65536,"Plugin state exceeds size limit");Json::parse(text);if(world)enqueue(c,[path,text]{writeText(path,text);return true;});else writeText(path,text);}
    else {check(output&&capacity,"Invalid read buffer");std::string text="{}";if(std::filesystem::exists(path)){check(std::filesystem::file_size(path)<=65536,"Plugin state exceeds size limit");std::ifstream file(path);check(static_cast<bool>(file),"Cannot read plugin state");text.assign(std::istreambuf_iterator<char>(file),{});}check(text.size()<capacity&&text.size()<=65536,"State buffer is too small");std::copy(text.begin(),text.end(),output);output[text.size()]='\0';}
    return 0;
}
void PluginManager::loadData(MC_PluginContext& ctx,const Json& data) {
    auto each=[&](const char* key,auto apply){if(!data.has(key))return;check(data.at(key).type==Json::Type::Array,"Content field must be an array");for(const auto& value:data.at(key).array)apply(value);};
    auto integer=[](const Json& j,const char* key,uint32_t fallback)->uint32_t {const double v=j.numeric(key,fallback);check(v>=0&&v<=65535&&std::floor(v)==v,"Expected bounded integer");return static_cast<uint32_t>(v);};
    each("materials",[&](const Json& j){MC_Material m{};m.size=sizeof(m);std::string key=j.at("key").text(),image=j.text("image"),normal=j.text("normal"),properties=j.text("properties");m.key=key.c_str();m.image=image.c_str();m.normal=normal.c_str();m.properties=properties.c_str();m.replace=j.flag("replace",false);std::fill(m.color,m.color+4,1.0f);if(j.has("color")){const auto& c=j.at("color");check(c.type==Json::Type::Array&&c.array.size()==4,"Material color requires four numbers");for(size_t i=0;i<4;++i){check(c.array[i].type==Json::Type::Number,"Invalid color");m.color[i]=static_cast<float>(c.array[i].number);}}material(ctx,m);});
    each("blocks",[&](const Json& j){MC_Block b{};b.size=sizeof(b);std::string key=j.at("key").text(),name=j.at("name").text(),drop=j.text("drop");b.key=key.c_str();b.name=name.c_str();b.drop_item=drop.c_str();b.hardness=static_cast<float>(j.numeric("hardness",1));b.alpha=static_cast<float>(j.numeric("alpha",1));b.solid=j.flag("solid",true);b.cross=j.flag("cross",false);b.layer=integer(j,"layer",0);b.emission=integer(j,"emission",0);b.preferred_tool=integer(j,"preferred_tool",integer(j,"tool",0));b.minimum_tier=integer(j,"minimum_tier",integer(j,"tier",0));b.unbreakable=j.flag("unbreakable",false);std::fill(b.color,b.color+3,1.0f);if(j.has("color")){const auto& c=j.at("color");check(c.type==Json::Type::Array&&c.array.size()==3,"Block color requires three numbers");for(size_t i=0;i<3;++i){check(c.array[i].type==Json::Type::Number,"Invalid block color");b.color[i]=static_cast<float>(c.array[i].number);}}std::array<std::string,6> faces;const auto& mats=j.at("materials");if(mats.type==Json::Type::String)faces.fill(mats.text());else {check(mats.type==Json::Type::Array&&mats.array.size()==6,"Block needs six materials");for(size_t i=0;i<6;++i)faces[i]=mats.array[i].text();}for(size_t i=0;i<6;++i)b.materials[i]=faces[i].c_str();uint16_t id=0;block(ctx,b,id);});
    each("items",[&](const Json& j){MC_Item item{};item.size=sizeof(item);std::string key=j.at("key").text(),name=j.at("name").text(),mat=j.at("material").text(),placed=j.text("placed_block");item.key=key.c_str();item.name=name.c_str();item.material=mat.c_str();item.placed_block=placed.c_str();item.kind=integer(j,"kind",placed.empty()?0:1);item.category=integer(j,"category",placed.empty()?6:0);item.max_stack=integer(j,"max_stack",64);item.durability=integer(j,"durability",0);item.tool=integer(j,"tool",0);item.tier=integer(j,"tier",0);item.food=integer(j,"food",0);item.saturation=static_cast<float>(j.numeric("saturation",0));item.attack_damage=static_cast<float>(j.numeric("attack_damage",0));item.attack_speed=static_cast<float>(j.numeric("attack_speed",0));uint16_t id=0;this->item(ctx,item,id);});
    each("recipes",[&](const Json& j){MC_Recipe r{};r.size=sizeof(r);std::string key=j.at("key").text(),target=j.text("target"),output=j.text("output");r.key=key.c_str();r.target=target.c_str();r.output=output.c_str();r.operation=integer(j,"operation",0);r.smelting=j.flag("smelting",false);r.width=integer(j,"width",1);r.height=integer(j,"height",1);r.mirror=j.flag("mirror",true);r.count=integer(j,"count",1);r.cook_ticks=integer(j,"cook_ticks",200);std::array<std::string,9> ingredients;if(j.has("ingredients")){const auto& a=j.at("ingredients");check(a.type==Json::Type::Array&&a.array.size()<=9,"Invalid ingredient array");for(size_t i=0;i<a.array.size();++i){ingredients[i]=a.array[i].text();r.ingredients[i]=ingredients[i].c_str();}}recipe(ctx,r);});
}
void PluginManager::validateContent() {
    auto materialExists=[](const std::string& key){if(content().materials.count(key))return true;for(uint16_t i=0;i<static_cast<uint16_t>(BlockTexture::Count);++i)if(key==getBlockTextureAssetName(static_cast<BlockTexture>(i)))return true;return false;};
    for(auto& entry:content().blocks) {
        for(const auto& key:entry.second.materials)check(materialExists(key),"Unknown block material");
        if(!entry.second.dropKey.empty())entry.second.drop=resolveItem(entry.second.dropKey);
    }
    for(const auto& entry:content().items)check(materialExists(entry.second.material),"Unknown item material");
}
void PluginManager::initialize(bool safeMode) {
    check(!m_initialized,"Plugin manager already initialized");m_initialized=true;
    const uint64_t revision=content().revision;content()=ContentState{};content().revision=revision;
    const auto config=m_paths.dataRoot/"plugins.json";
    if(std::filesystem::exists(config))try {const auto j=readJson(config);for(const auto& p:j.object){check(p.second.type==Json::Type::Boolean,"Plugin enable setting must be boolean");m_enabled[p.first]=p.second.boolean;}}catch(const std::exception& e){LOG_WARN("Ignoring invalid plugin enable settings: "<<e.what());}
    auto addBuiltin=[&](const char* id,const char* name,bool visual){PluginInfo p;p.manifest.id=id;p.manifest.name=name;p.manifest.version="1.0.0";p.manifest.builtin=true;p.manifest.visual=visual;p.manifest.defaultEnabled=false;p.enabled=!safeMode&&m_enabled.count(id)&&m_enabled.at(id);if(!p.enabled)p.status="Disabled";m_info.push_back(std::move(p));};
    addBuiltin("official_content","Official Content Example",false);addBuiltin("official_atmosphere","Official Atmosphere Example",true);
    const auto directory=m_paths.dataRoot/"mods";std::filesystem::create_directories(directory);
    std::vector<std::filesystem::path> directories;
    for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_directory())directories.push_back(entry.path());
    std::sort(directories.begin(),directories.end());
    for(const auto& root:directories) {
        PluginInfo p;p.root=root;p.manifest.id=root.filename().string();p.manifest.name=p.manifest.id;
        try {
            check(!std::filesystem::is_symlink(root),"Plugin package directory cannot be a symlink");
            p.manifest=Manifest::parse(readJson(containedPath(root,"mod.json")));
            check(p.manifest.id==root.filename().string(),"Package directory must match plugin ID");
            p.enabled=!safeMode&&(m_enabled.count(p.manifest.id)?m_enabled.at(p.manifest.id):p.manifest.defaultEnabled);
            if(!p.enabled)p.status="Disabled";
            else if(!p.manifest.entry.empty()&&(!Platform::DynamicLibrary::supported()||p.manifest.entry=="!unsupported"))p.status="Native entry unsupported on this platform";
        }catch(const std::exception& e){p.status=e.what();p.enabled=false;}
        m_info.push_back(std::move(p));
    }
    const auto order=resolve(m_info);m_loaded.resize(m_info.size());
    for(size_t i:order) {
        auto& info=m_info[i];bool dependenciesActive=true;
        for(const auto& dependency:info.manifest.dependencies) {
            auto it=std::find_if(m_info.begin(),m_info.end(),[&](const auto& p){return p.manifest.id==dependency.id&&p.active;});
            if(!dependency.optional&&it==m_info.end())dependenciesActive=false;
        }
        if(!dependenciesActive){info.status="Blocked by failed dependency";continue;}
        auto before=content();const size_t listeners=m_listeners.size();const auto commands=m_commands;
        auto& loaded=m_loaded[i];loaded.context=std::make_unique<MC_PluginContext>();loaded.context->manager=this;loaded.context->index=i;
        try {
            if(info.manifest.builtin)loaded.api=info.manifest.visual?builtinAtmosphere(MC_PLUGIN_ABI):builtinContent(MC_PLUGIN_ABI);
            else if(!info.manifest.entry.empty()) {
                loaded.library=std::make_unique<Platform::DynamicLibrary>(containedPath(info.root,info.manifest.entry));
                auto query=reinterpret_cast<MC_PluginQuery>(loaded.library->symbol("MCPlugin_Query"));check(query,"Missing MCPlugin_Query entry point");loaded.api=query(MC_PLUGIN_ABI);
            }
            if(loaded.api) {
                check(loaded.api->size>=sizeof(MC_Plugin)&&loaded.api->abi==MC_PLUGIN_ABI&&loaded.api->load,"Plugin ABI mismatch");
                check(required(loaded.api->id)==info.manifest.id&&required(loaded.api->version)==info.manifest.version,"Native metadata differs from manifest");
                loaded.loadEntered=true;
                check(loaded.api->load(&m_host,loaded.context.get())==0,"Native plugin initialization failed");
            } else check(info.manifest.entry.empty()&&!info.manifest.builtin,"Plugin rejected host ABI");
            info.active=true;
            MC_Event registration=event(MC_REGISTER);
            std::vector<Listener> registrationListeners(m_listeners.begin()+static_cast<std::ptrdiff_t>(listeners),m_listeners.end());
            std::stable_sort(registrationListeners.begin(),registrationListeners.end(),[](const auto& a,const auto& b){return a.priority>b.priority;});
            for(const auto& listener:registrationListeners)
                if(listener.kind==MC_REGISTER)check(listener.callback(loaded.context.get(),&registration,listener.userdata)==0,"Registration callback failed");
            if(!info.manifest.data.empty())loadData(*loaded.context,readJson(containedPath(info.root,info.manifest.data)));
            check(!loaded.context->registrationFailed,"Plugin ignored a failed registration");
            validateContent();
            if(!info.manifest.visual)content().requirements.push_back({info.manifest.id,info.manifest.version+":"+fingerprint(info.root,(info.manifest.builtin?std::string("builtin-api1-content1"):std::string{}) + ([&]{const auto config=m_paths.dataRoot/"config"/"plugins"/(info.manifest.id+".json");if(!std::filesystem::exists(config))return std::string{};std::ifstream file(config);return std::string(std::istreambuf_iterator<char>(file),{});}()))});
            info.status="Loaded";
        }catch(const std::exception& e) {
            info.active=false;info.status=e.what();if(!loaded.context->error.empty())info.status+=" ("+loaded.context->error+")";
            content()=std::move(before);m_listeners.resize(listeners);m_commands=commands;
            if(loaded.loadEntered&&loaded.api&&loaded.api->unload)loaded.api->unload(loaded.context.get());
            loaded.api=nullptr;loaded.library.reset();
            LOG_WARN("Plugin "<<info.manifest.id<<": "<<info.status);
        }
    }
    std::stable_sort(m_listeners.begin(),m_listeners.end(),[](const auto& a,const auto& b){return a.priority>b.priority;});
    freezeContent();m_phase=MC_INITIALIZE;dispatcher()=[this](MC_Event& e){dispatch(e);};commandDispatcher()=[this](const std::string& s){return command(s);};
    auto ready=event(MC_INITIALIZE);dispatch(ready);
}
void PluginManager::dispatch(MC_Event& e) {
    check(!m_dispatching,"Recursive plugin event dispatch");
    if(e.kind==MC_WORLD_CLOSE&&!m_world)return;
    if(e.kind==MC_WORLD_READY)m_world=true;
    if(e.kind==MC_HUD)m_hudOperations=0;
    const auto previous=m_phase;m_phase=e.kind;m_dispatching=true;
    const bool cancellable=e.kind==MC_BREAK_PRE||e.kind==MC_PLACE_PRE||e.kind==MC_USE_PRE||e.kind==MC_DAMAGE_PRE;
    for(const auto& listener:m_listeners) {
        if(listener.kind!=e.kind||!m_info[listener.plugin].active)continue;
        MC_Event candidate=e;const bool cancelled=e.cancelled!=0;
        try {
            const int32_t result=listener.callback(m_loaded[listener.plugin].context.get(),&candidate,listener.userdata);
            if(result!=0){
                auto& info=m_info[listener.plugin];info.active=false;info.status="Callback failed; restart required";
                content().runtimeFault="Plugin callback failed: "+info.manifest.id;
                LOG_ERROR(content().runtimeFault);discardOperations();if(cancellable)e.cancelled=1;continue;
            }
            if(cancellable)e.cancelled=cancelled||candidate.cancelled;
            if(e.kind==MC_DAMAGE_PRE&&std::isfinite(candidate.damage)&&candidate.damage>=0)e.damage=candidate.damage;
            if(e.kind==MC_ENVIRONMENT) {
                bool valid=true;const auto& env=candidate.environment;
                for(const float* color:{env.zenith,env.horizon,env.fog,env.ambient_color,env.direct_color})for(int i=0;i<3;++i)valid=valid&&std::isfinite(color[i])&&color[i]>=0&&color[i]<=16;
                for(float intensity:{env.ambient_intensity,env.direct_intensity})valid=valid&&std::isfinite(intensity)&&intensity>=0&&intensity<=16;
                if(valid)e.environment=candidate.environment;
            }
        }catch(const std::exception& error){
            auto& info=m_info[listener.plugin];info.active=false;info.status="Exception across plugin ABI; restart required";
            content().runtimeFault="Plugin exception: "+info.manifest.id+": "+error.what();
            LOG_ERROR(content().runtimeFault);discardOperations();if(cancellable)e.cancelled=1;
        }
    }
    m_dispatching=false;m_phase=previous;
    if(!content().runtimeFault.empty()&&cancellable)e.cancelled=1;
    if(e.kind==MC_WORLD_CLOSE){m_world=false;discardOperations();}
}
bool PluginManager::command(const std::string& line) {
    const auto split=line.find(' ');std::string name=line.substr(0,split);if(!name.empty()&&name[0]=='/')name.erase(0,1);
    auto it=m_commands.find(name);if(it==m_commands.end())return false;
    check(m_world&&!m_dispatching,"Plugin command requires a world");
    const std::string arguments=split==std::string::npos?"":line.substr(split+1);auto e=event(MC_COMMAND);e.command=name.c_str();e.arguments=arguments.c_str();
    const auto phase=m_phase;m_phase=MC_COMMAND;m_dispatching=true;
    int32_t result=-1;
    try {result=it->second.callback(m_loaded[it->second.plugin].context.get(),&e,it->second.userdata);}catch(const std::exception& error){LOG_ERROR(error.what());}
    m_dispatching=false;m_phase=phase;
    if(result){auto& info=m_info[it->second.plugin];info.active=false;info.status="Command callback failed; restart required";content().runtimeFault="Plugin command failed: "+name;discardOperations();LOG_ERROR(content().runtimeFault);}
    return true;
}
void PluginManager::flushOperations() {
    check(!m_dispatching,"Cannot flush within plugin callback");auto queue=std::move(m_queue);m_queue.clear();
    for(auto& operation:queue)try{if(!operation())LOG_WARN("Plugin operation rejected by session");}catch(const std::exception& e){LOG_WARN("Plugin operation failed: "<<e.what());}
}
void PluginManager::discardOperations(){m_queue.clear();}
void PluginManager::setEnabled(size_t i,bool enabled) {
    const auto& p=m_info.at(i);m_enabled[p.manifest.id]=enabled;
    writeText(m_paths.dataRoot/"plugins.json",nlohmann::json(m_enabled).dump(2)+"\n");
    m_info[i].enabled=enabled;m_info[i].status="Restart required";
}
void PluginManager::shutdown() {
    if(m_shutdown)return;
    m_shutdown=true;
    if(m_initialized){auto e=event(MC_SHUTDOWN);dispatch(e);}
    dispatcher()={};commandDispatcher()={};discardOperations();m_listeners.clear();m_commands.clear();operations={};
    for(auto it=m_loaded.rbegin();it!=m_loaded.rend();++it){if(it->api&&it->api->unload)it->api->unload(it->context.get());it->api=nullptr;it->library.reset();}
}
} // namespace Plugins
