#include "plugins/PluginManager.h"
#include "core/AssetStore.h"
#include "model/GltfLoader.h"
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
std::filesystem::path materializeBuiltinResources(const std::filesystem::path& package,
                                                 const std::filesystem::path& dataRoot,
                                                 const std::string& id) {
    const auto prefix=package.generic_string()+"/";
    auto manifest=AssetStore::readTitleBinary(prefix+"resources.sha256.json");
    check(manifest.size()<=2*1024*1024,"Builtin resource manifest exceeds size limit");
    const auto parsed=Json::parse(std::string(manifest.begin(),manifest.end()));
    check(parsed.numeric("version",0)==1,"Unsupported builtin resource manifest");
    const auto& entries=parsed.at("sha256");
    check(entries.type==Json::Type::Object&&entries.object.size()<=4096,"Invalid builtin resource inventory");
    // Read the complete immutable package before publishing any cache files.
    std::map<std::string,std::vector<uint8_t>> files;
    size_t total=manifest.size();
    for(const auto& entry:entries.object) {
        check(AssetStore::validPath(entry.first)&&entry.first.find(':')==std::string::npos&&entry.first!="resources.sha256.json"&&
              std::filesystem::path(entry.first).lexically_normal().generic_string()==entry.first,
              "Invalid builtin resource path");
        const auto digest=entry.second.text();
        check(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==std::string::npos,
              "Invalid builtin resource digest");
        auto bytes=AssetStore::readTitleBinary(prefix+entry.first);
        check(bytes.size()<=32*1024*1024,"Builtin resource exceeds size limit");
        total+=bytes.size();check(total<=128*1024*1024,"Builtin package exceeds size limit");
        files.emplace(entry.first,std::move(bytes));
    }
    uint64_t hash=1469598103934665603ULL;
    for(const auto byte:manifest){hash^=byte;hash*=1099511628211ULL;}
    std::ostringstream key;key<<std::hex<<hash;
    // Resolve trusted system aliases (e.g. macOS /var), but never cache symlinks.
    const auto trustedRoot=std::filesystem::canonical(dataRoot);
    const auto root=trustedRoot/".cache"/"builtin_plugins"/id/key.str();
    files.emplace("resources.sha256.json",std::move(manifest));
    for(const auto& file:files) {
        const auto destination=root/file.first;
        for(auto parent=destination;parent!=trustedRoot;parent=parent.parent_path()) {
            check(!std::filesystem::is_symlink(std::filesystem::symlink_status(parent)),
                  "Builtin resource cache cannot contain symlinks");
        }
        if(std::filesystem::is_regular_file(destination)&&std::filesystem::file_size(destination)==file.second.size()) {
            std::ifstream existing(destination,std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(existing)),{});
            if(bytes==file.second)continue;
        }
        std::filesystem::create_directories(destination.parent_path());
        auto temporary=destination;temporary+=".tmp";
        check(!std::filesystem::is_symlink(std::filesystem::symlink_status(temporary)),"Invalid builtin cache temporary file");
        {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output.write(reinterpret_cast<const char*>(file.second.data()),static_cast<std::streamsize>(file.second.size()));
            output.flush();check(static_cast<bool>(output),"Cannot write builtin resource cache");}
        std::error_code error;
        check(Platform::replaceFileAtomically(temporary,destination,error),"Cannot publish builtin resource cache");
    }
    return root;
}
void validateMaterialImage(const std::filesystem::path& image) {
        if(image.empty())return;
        check(std::filesystem::file_size(image)<=1024*1024,"Material image exceeds size limit");
        std::ifstream file(image,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        check(bytes.size()>=8&&bytes[0]==137&&bytes[1]=='P'&&bytes[2]=='N'&&bytes[3]=='G',"Material must be PNG");
        int w=0,h=0,channels=0;stbi_uc* decoded=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&channels,4);
        const bool valid=decoded&&w==16&&h==16;stbi_image_free(decoded);check(valid,"Material must be a valid 16x16 PNG");
    }
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
    if(j.has("description"))m.readDescription(j.at("description"));
    check(validKey(m.id+":test")&&m.id!="minecraftc","Invalid/reserved plugin ID");
    check(j.numeric("api",0)==MC_PLUGIN_ABI,"Unsupported plugin API");
    m.networkCompatibility=j.text("network_compatibility");
    check(m.networkCompatibility.size()<=128,"Network compatibility identifier is too long");
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
void Manifest::readDescription(const Json& value) {
    std::map<std::string,std::string> entries;
    auto add=[&](const std::string& language,const Json& text) {
        const auto description=text.text();
        check(description.size()<=4096,"Plugin description is too long");
        entries.emplace(language,description);
    };
    if(value.type==Json::Type::String)add("en_us",value);
    else {
        check(value.type==Json::Type::Object&&value.object.size()<=32,"Invalid localized plugin description");
        check(value.has("en_us"),"Localized plugin description requires en_us");
        for(const auto& translation:value.object)add(translation.first,translation.second);
    }
    descriptions=std::move(entries);
}
std::string Manifest::description(const std::string& language) const {
    auto translation=descriptions.find(language);
    if(translation==descriptions.end()||translation->second.empty())translation=descriptions.find("en_us");
    return translation==descriptions.end()?std::string{}:translation->second;
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
PluginManager::PluginManager(RuntimePaths paths,std::shared_ptr<model::ModelAssetCache> modelAssets):m_paths(std::move(paths)),m_modelAssets(modelAssets?std::move(modelAssets):std::make_shared<model::ModelAssetCache>()) {
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
    m_host.hud_rect=[](MC_PluginContext* c,float x,float y,float w,float h,const float* color){return checked(c,[&]{auto& m=manager(c);hudColor(color);check(m.m_hudOperations++<Config::PLUGIN_MAX_HUD_COMMANDS,"HUD command budget exceeded");check(!m.m_readOnly&&m.m_phase==MC_HUD&&color&&m.operations.hudRect,"HUD drawing outside HUD event");check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(w)&&std::isfinite(h)&&w>=0&&h>=0,"Invalid HUD bounds");m.operations.hudRect(x,y,w,h,{color[0],color[1],color[2],color[3]});});};
    m_host.hud_text=[](MC_PluginContext* c,float x,float y,const char* text,const float* color){return checked(c,[&]{auto& m=manager(c);hudColor(color);check(m.m_hudOperations++<Config::PLUGIN_MAX_HUD_COMMANDS,"HUD command budget exceeded");check(!m.m_readOnly&&m.m_phase==MC_HUD&&color&&text&&m.operations.hudText,"HUD drawing outside HUD event");check(std::isfinite(x)&&std::isfinite(y)&&std::string(text).size()<=4096,"Invalid HUD text");m.operations.hudText(x,y,text,{color[0],color[1],color[2],color[3]});});};
    m_host.hud_image=[](MC_PluginContext* c,float x,float y,float w,float h,const char* key,const float* color){return checked(c,[&]{auto& m=manager(c);hudColor(color);check(m.m_hudOperations++<Config::PLUGIN_MAX_HUD_COMMANDS,"HUD command budget exceeded");check(!m.m_readOnly&&m.m_phase==MC_HUD&&color&&m.operations.hudImage,"HUD drawing outside HUD event");check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(w)&&std::isfinite(h)&&w>=0&&h>=0,"Invalid HUD bounds");auto it=content().materials.find(required(key));check(it!=content().materials.end(),"Unknown HUD material");uint16_t tile=it->second.tile;m.operations.hudImage(x,y,w,h,tile,{color[0],color[1],color[2],color[3]});});};
    m_host.config_read=[](MC_PluginContext* c,char* out,uint32_t cap){return checked(c,[&]{manager(c).storage(*c,false,false,nullptr,out,cap);});};
    m_host.config_write=[](MC_PluginContext* c,const char* value){return checked(c,[&]{manager(c).storage(*c,false,true,value,nullptr,0);});};
    m_host.world_data_read=[](MC_PluginContext* c,char* out,uint32_t cap){return checked(c,[&]{manager(c).storage(*c,true,false,nullptr,out,cap);});};
    m_host.world_data_write=[](MC_PluginContext* c,const char* value){return checked(c,[&]{manager(c).storage(*c,true,true,value,nullptr,0);});};
    m_host.query_extension=[](MC_PluginContext* c,const char* name,uint32_t version)->const void* {
        static const MC_MultiplayerV1 api{
            sizeof(MC_MultiplayerV1),1,
            [](MC_PluginContext* ctx,uint32_t* out){return checked(ctx,[&]{check(out,"Missing role output");*out=context().role;});},
            [](MC_PluginContext* ctx,uint64_t* out,uint32_t capacity,uint32_t* count){return checked(ctx,[&]{
                auto& m=manager(ctx);check(count&&m.operations.players,"No active player list");const auto ids=m.operations.players();
                *count=static_cast<uint32_t>(ids.size());if(out){check(capacity>=ids.size(),"Player buffer is too small");std::copy(ids.begin(),ids.end(),out);}});},
            [](MC_PluginContext* ctx,uint64_t id,MC_PlayerSnapshot* out,uint32_t* dimension){return checked(ctx,[&]{
                auto& m=manager(ctx);check(out&&out->size>=sizeof(*out)&&dimension&&m.operations.playerById&&m.operations.playerById(id,*out,*dimension),"Player is unavailable");});},
            [](MC_PluginContext* ctx,uint64_t id,uint16_t item,uint32_t count){return checked(ctx,[&]{
                auto& m=manager(ctx);check(isValidItemId(static_cast<ItemId>(item))&&item&&count&&count<=4096,"Invalid item/count");
                MC_PlayerSnapshot snapshot{};snapshot.size=sizeof(snapshot);uint32_t dimension=0;
                check(m.operations.playerById&&m.operations.playerById(id,snapshot,dimension),"Player is unavailable");
                auto actor=context();actor.player=id;actor.dimension=dimension;ActorScope scope(actor);
                m.enqueue(*ctx,[&m,item,count]{return m.operations.giveItem&&m.operations.giveItem(item,count);});});},
            [](MC_PluginContext* ctx,uint32_t dimension,int32_t x,int32_t y,int32_t z,uint16_t* id){return checked(ctx,[&]{
                check(validDimension(dimension),"Invalid dimension");auto actor=context();actor.dimension=dimension;ActorScope scope(actor);
                auto& m=manager(ctx);check(id&&m.operations.getBlock&&m.operations.getBlock(x,y,z,*id),"Block is unavailable");});},
            [](MC_PluginContext* ctx,uint32_t dimension,int32_t x,int32_t y,int32_t z,uint16_t id){return checked(ctx,[&]{
                check(validDimension(dimension)&&validBlock(static_cast<BlockId>(id)),"Invalid dimension/block");
                auto actor=context();actor.dimension=dimension;ActorScope scope(actor);auto& m=manager(ctx);
                m.enqueue(*ctx,[&m,x,y,z,id]{return m.operations.setBlock&&m.operations.setBlock(x,y,z,id);});});},
            [](MC_PluginContext* ctx,const char* name,uint32_t flags,MC_EventCallback callback,void* data){
                const auto result=checked(ctx,[&]{manager(ctx).addCommand(*ctx,name,callback,data,flags);});
                if(result&&ctx)ctx->registrationFailed=true;
                return result;}
        };
        const void* result=nullptr;
        checked(c,[&]{check(name,"Missing extension name");if(std::string(name)==MC_MULTIPLAYER_EXTENSION&&version==1)result=&api;else result=manager(c).extension(c,name,version);});
        return result;
    };
}
PluginManager::~PluginManager(){shutdown();}
void PluginManager::owned(MC_PluginContext& ctx,const std::string& key) const {
    check(m_phase==MC_REGISTER&&!content().frozen,"Registration is closed");
    const auto& info=m_info.at(ctx.index);
    const auto block=builtinKeyId(key,"minecraftc:block_",static_cast<uint16_t>(BlockId::COUNT));
    const auto item=builtinKeyId(key,"minecraftc:item_",static_cast<uint16_t>(ItemId::COUNT));
    const bool reserved=info.manifest.builtin&&info.manifest.id=="official_heaven"&&
        (key=="minecraftc:heaven"||(block!=INVALID_CONTENT_ID&&heavenBlockSlot(static_cast<BlockId>(block)))||(item!=INVALID_CONTENT_ID&&heavenItemSlot(static_cast<ItemId>(item))));
    check(validKey(key)&&(key.rfind(info.manifest.id+":",0)==0||reserved),"Content must use its plugin namespace or trusted reserved slot");
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
    for(const auto& image:{m.image,m.normal,m.properties})validateMaterialImage(image);
    m.color={v.color[0],v.color[1],v.color[2],v.color[3]};
    for(size_t i=0;i<4;++i)check(std::isfinite(v.color[i])&&v.color[i]>=0&&v.color[i]<=1,"Invalid material color");
    if(m.replace)LOG_INFO("Plugin "<<info.manifest.id<<" replaces material "<<m.key);
    content().materials[m.key]=std::move(m);return 0;
}
void PluginManager::materialSequence(MC_PluginContext& ctx,const MC_MaterialSequenceDefinition& d) {
    check(d.size>=sizeof(d)&&d.variants>=1&&d.variants<=4&&d.frames>=1&&d.frames<=16&&d.variants*d.frames<=64&&d.fps_milli<=60000&&(d.frames==1||d.fps_milli)&&d.images,"Invalid material sequence");
    const auto& root=m_info.at(ctx.index).root;
    std::vector<std::filesystem::path> images,normals,properties;
    for(uint32_t i=0;i<d.variants*d.frames;++i) {
        const auto image=containedPath(root,required(d.images[i]));validateMaterialImage(image);images.push_back(image);
        const auto normal=d.normals&&d.normals[i]?containedPath(root,required(d.normals[i])):std::filesystem::path{};validateMaterialImage(normal);normals.push_back(normal);
        const auto property=d.properties&&d.properties[i]?containedPath(root,required(d.properties[i])):std::filesystem::path{};validateMaterialImage(property);properties.push_back(property);
    }
    auto base=d.base;base.image=d.images[0];base.normal=d.normals?d.normals[0]:nullptr;base.properties=d.properties?d.properties[0]:nullptr;
    material(ctx,base);auto& value=content().materials.at(d.base.key);value.variants=d.variants;value.frames=d.frames;value.fpsMilli=d.fps_milli;value.seed=d.seed;
    value.images=std::move(images);value.normals=std::move(normals);value.propertyFrames=std::move(properties);
}
void PluginManager::itemIcon(MC_PluginContext& ctx,const MC_ItemIcon& d) {
    check(d.size>=sizeof(d),"Invalid item icon descriptor");const auto key=required(d.item);owned(ctx,key);
    const auto id=resolveItem(key);auto& item=content().items.at(static_cast<uint16_t>(id));
    item.icon=containedPath(m_info.at(ctx.index).root,required(d.path));validateMaterialImage(item.icon);
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
int32_t PluginManager::addCommand(MC_PluginContext& c,const char* name,MC_EventCallback callback,void* data,uint32_t flags) {
    const std::string key=required(name);owned(c,key);check(flags<=MC_COMMAND_ALLOW_GUEST,"Invalid command permission");check(callback&&m_commands.emplace(key,Command{c.index,callback,data,flags}).second,"Invalid/duplicate command");return 0;
}
int32_t PluginManager::enqueue(MC_PluginContext& c,std::function<bool()> operation) {
    check(!m_readOnly,"Read-only callback cannot mutate state");
    check(content().runtimeFault.empty(),"Plugin runtime requires restart");
    check(context().role!=MC_CLIENT,"Client gameplay mutations are unavailable");
    check(m_world&&m_phase!=MC_ENVIRONMENT&&m_phase!=MC_HUD&&m_phase!=MC_WORLD_CLOSE,"World mutations are unavailable in this phase");
    check(!m_info[c.index].manifest.visual,"Visual-only plugins cannot modify gameplay");
    check(m_queue.size()<Config::PLUGIN_MAX_OPERATIONS,"Plugin operation queue is full");const auto actor=context();
    m_queue.push_back([actor,operation=std::move(operation)]{ActorScope scope(actor);return operation();});return 0;
}
int32_t PluginManager::storage(MC_PluginContext& c,bool world,bool write,const char* input,char* output,uint32_t capacity) {
    const auto& info=m_info.at(c.index);std::filesystem::path path;
    if(world) {
        check(m_world&&operations.worldDirectory,"No active world");
        check(!info.manifest.visual,"Visual-only plugins have no persisted world state");
        check(context().role!=MC_CLIENT,"Client world data is unavailable");
        const auto root=operations.worldDirectory();check(!root.empty(),"No writable world");
        path=root/"plugin_data"/(info.manifest.id+".json");
    } else path=m_paths.dataRoot/"config"/"plugins"/(info.manifest.id+".json");
    if(write) {check(!m_readOnly,"Read-only callback cannot write state");std::string text=required(input);check(text.size()<=65536,"Plugin state exceeds size limit");Json::parse(text);if(world)enqueue(c,[path,text]{writeText(path,text);return true;});else writeText(path,text);}
    else {check(output&&capacity,"Invalid read buffer");std::string text="{}";if(std::filesystem::exists(path)){check(std::filesystem::file_size(path)<=65536,"Plugin state exceeds size limit");std::ifstream file(path);check(static_cast<bool>(file),"Cannot read plugin state");text.assign(std::istreambuf_iterator<char>(file),{});}check(text.size()<capacity&&text.size()<=65536,"State buffer is too small");std::copy(text.begin(),text.end(),output);output[text.size()]='\0';}
    return 0;
}
void PluginManager::loadData(MC_PluginContext& ctx,const Json& data) {
    auto each=[&](const char* key,auto apply){if(!data.has(key))return;check(data.at(key).type==Json::Type::Array,"Content field must be an array");for(const auto& value:data.at(key).array)apply(value);};
    auto integer=[](const Json& j,const char* key,uint32_t fallback)->uint32_t {const double v=j.numeric(key,fallback);check(v>=0&&v<=65535&&std::floor(v)==v,"Expected bounded integer");return static_cast<uint32_t>(v);};
    each("materials",[&](const Json& j){MC_Material m{};m.size=sizeof(m);std::string key=j.at("key").text(),image=j.text("image"),normal=j.text("normal"),properties=j.text("properties");m.key=key.c_str();m.image=image.c_str();m.normal=normal.c_str();m.properties=properties.c_str();m.replace=j.flag("replace",false);std::fill(m.color,m.color+4,1.0f);if(j.has("color")){const auto& c=j.at("color");check(c.type==Json::Type::Array&&c.array.size()==4,"Material color requires four numbers");for(size_t i=0;i<4;++i){check(c.array[i].type==Json::Type::Number,"Invalid color");m.color[i]=static_cast<float>(c.array[i].number);}}material(ctx,m);});
    auto loadBlock=[&](const Json& j,bool family){MC_Block b{};b.size=sizeof(b);std::string key=j.at("key").text(),name=j.at("name").text(),drop=j.text("drop");b.key=key.c_str();b.name=name.c_str();b.drop_item=drop.c_str();b.hardness=static_cast<float>(j.numeric("hardness",1));b.alpha=static_cast<float>(j.numeric("alpha",1));b.solid=j.flag("solid",true);b.cross=j.flag("cross",false);b.layer=integer(j,"layer",0);b.emission=integer(j,"emission",0);b.preferred_tool=integer(j,"preferred_tool",integer(j,"tool",0));b.minimum_tier=integer(j,"minimum_tier",integer(j,"tier",0));b.unbreakable=j.flag("unbreakable",false);std::fill(b.color,b.color+3,1.0f);if(j.has("color")){const auto& c=j.at("color");check(c.type==Json::Type::Array&&c.array.size()==3,"Block color requires three numbers");for(size_t i=0;i<3;++i){check(c.array[i].type==Json::Type::Number,"Invalid block color");b.color[i]=static_cast<float>(c.array[i].number);}}std::array<std::string,6> faces;const auto& mats=j.at("materials");if(mats.type==Json::Type::String)faces.fill(mats.text());else {check(mats.type==Json::Type::Array&&mats.array.size()==6,"Block needs six materials");for(size_t i=0;i<6;++i)faces[i]=mats.array[i].text();}for(size_t i=0;i<6;++i)b.materials[i]=faces[i].c_str();uint16_t id=0;
        if(!family)block(ctx,b,id);
        else {
            MC_BlockFamily d{};d.size=sizeof(d);d.block=b;const auto kind=j.at("kind").text();
            d.kind=kind=="slab"?MC_SLAB_FAMILY:kind=="stair"?MC_STAIR_FAMILY:kind=="door"?MC_DOOR_FAMILY:kind=="button"?MC_BUTTON_FAMILY:0;
            const auto full=j.text("full_block");d.full_block=full.c_str();d.manual_open=j.flag("manual_open",true);d.villager_open=j.flag("villager_open",true);
            d.release_ticks=integer(j,"release_ticks",30);d.arrow_trigger=j.flag("arrow_trigger",true);
            if(!j.has("solid"))d.block.solid=d.kind==MC_BUTTON_FAMILY?0:1;
            blockFamily(ctx,d,id);
        }
    };
    each("blocks",[&](const Json& j){loadBlock(j,false);});
    each("block_families",[&](const Json& j){loadBlock(j,true);});
    each("block_geometry",[&](const Json& j){
        const auto key=j.at("block").text();MC_BlockGeometry d{};d.size=sizeof(d);d.block=key.c_str();
        std::array<std::vector<MC_BlockBox>,3> groups;MC_BlockBox empty{};
        const char* names[]={"collision","selection","model"};
        for(size_t group=0;group<3;++group)if(j.has(names[group])) {
            const auto& boxes=j.at(names[group]);check(boxes.type==Json::Type::Array&&boxes.array.size()<=MC_MAX_BLOCK_BOXES,"Invalid box group");
            for(const auto& box:boxes.array) {
                check(box.type==Json::Type::Array&&box.array.size()==6,"Box requires six bounds");MC_BlockBox b{};
                for(size_t axis=0;axis<3;++axis){check(box.array[axis].type==Json::Type::Number&&box.array[axis+3].type==Json::Type::Number,"Invalid box coordinate");b.min[axis]=static_cast<float>(box.array[axis].number);b.max[axis]=static_cast<float>(box.array[axis+3].number);}groups[group].push_back(b);
            }
            const auto* buffer=groups[group].empty()?&empty:groups[group].data();const auto count=static_cast<uint32_t>(groups[group].size());
            if(group==0){d.collision=buffer;d.collision_count=count;}else if(group==1){d.selection=buffer;d.selection_count=count;}else{d.model=buffer;d.model_count=count;}
        }
        blockGeometry(ctx,d);
    });
    each("items",[&](const Json& j){MC_Item item{};item.size=sizeof(item);std::string key=j.at("key").text(),name=j.at("name").text(),mat=j.at("material").text(),placed=j.text("placed_block");item.key=key.c_str();item.name=name.c_str();item.material=mat.c_str();item.placed_block=placed.c_str();item.kind=integer(j,"kind",placed.empty()?0:1);item.category=integer(j,"category",placed.empty()?6:0);item.max_stack=integer(j,"max_stack",64);item.durability=integer(j,"durability",0);item.tool=integer(j,"tool",0);item.tier=integer(j,"tier",0);item.food=integer(j,"food",0);item.saturation=static_cast<float>(j.numeric("saturation",0));item.attack_damage=static_cast<float>(j.numeric("attack_damage",0));item.attack_speed=static_cast<float>(j.numeric("attack_speed",0));uint16_t id=0;this->item(ctx,item,id);});
    each("recipes",[&](const Json& j){MC_Recipe r{};r.size=sizeof(r);std::string key=j.at("key").text(),target=j.text("target"),output=j.text("output");r.key=key.c_str();r.target=target.c_str();r.output=output.c_str();r.operation=integer(j,"operation",0);r.smelting=j.flag("smelting",false);r.width=integer(j,"width",1);r.height=integer(j,"height",1);r.mirror=j.flag("mirror",true);r.count=integer(j,"count",1);r.cook_ticks=integer(j,"cook_ticks",200);std::array<std::string,9> ingredients;if(j.has("ingredients")){const auto& a=j.at("ingredients");check(a.type==Json::Type::Array&&a.array.size()<=9,"Invalid ingredient array");for(size_t i=0;i<a.array.size();++i){ingredients[i]=a.array[i].text();r.ingredients[i]=ingredients[i].c_str();}}recipe(ctx,r);});
    each("dimensions",[&](const Json& j){
        MC_DimensionDefinition d{};d.size=sizeof(d);
        const auto key=j.at("key").text(),name=j.text("name",key),revision=j.at("revision").text();
        const auto generator=j.at("generator").text(),genRevision=j.at("generator_revision").text();
        const auto parameters=j.text("parameters","{}"),death=j.text("death_target"),voidTarget=j.text("void_target");
        d.key=key.c_str();d.name=name.c_str();d.revision=revision.c_str();d.generator=generator.c_str();d.generator_revision=genRevision.c_str();d.parameters=parameters.c_str();
        d.weather=integer(j,"weather",0);d.disable_natural_spawning=!j.flag("natural_spawning",true);d.spawn=integer(j,"spawn",0);d.death_target=death.c_str();d.void_target=voidTarget.c_str();
        if(j.has("spawn_position")){const auto& a=j.at("spawn_position");check(a.type==Json::Type::Array&&a.array.size()==3,"Spawn requires three coordinates");for(int i=0;i<3;++i){check(a.array[i].type==Json::Type::Number,"Invalid spawn coordinate");d.spawn_position[i]=a.array[i].number;}}
        std::vector<std::string> modifierKeys,modifierRevisions;std::vector<MC_DimensionModifier> modifiers;
        if(j.has("modifiers")){const auto& values=j.at("modifiers");check(values.type==Json::Type::Array&&values.array.size()<=64,"Invalid default modifiers");for(const auto& m:values.array){modifierKeys.push_back(m.at("key").text());modifierRevisions.push_back(m.at("revision").text());}for(size_t i=0;i<modifierKeys.size();++i)modifiers.push_back({modifierKeys[i].c_str(),modifierRevisions[i].c_str()});}
        d.modifiers=modifiers.data();d.modifier_count=static_cast<uint32_t>(modifiers.size());
        uint32_t id;dimensionDefinition(ctx,d,id);
    });
    if(data.has("entities"))check(data.at("entities").type==Json::Type::Array,"Expected entity array");
    if(data.has("generators"))check(data.at("generators").type==Json::Type::Array,"Expected generator array");
    if(data.has("entities"))for(const auto& value:data.at("entities").array) {
        MC_EntityDefinition d{};d.size=sizeof(d);const auto key=value.at("key").text(),name=value.text("name",key),target=value.text("target"),model=value.text("model"),drop=value.text("drop_item");
        d.key=key.c_str();d.name=name.c_str();d.target=target.c_str();d.model=model.c_str();d.drop_item=drop.c_str();
        d.template_type=integer(value,"template_type",1);d.health=static_cast<float>(value.numeric("health",10));
        d.speed=static_cast<float>(value.numeric("speed",2));d.attack_damage=static_cast<float>(value.numeric("attack_damage",2));
        d.hostile=value.flag("hostile",false);d.natural_spawn=value.flag("natural_spawn",false);d.drop_count=integer(value,"drop_count",drop.empty()?0:1);
        d.state_version=integer(value,"state_version",0);
        for(int i=0;i<3;++i)d.size_xyz[i]=i==1?1:.6f;
        if(value.has("size")){const auto& values=value.at("size");check(values.type==Json::Type::Array,"Expected size array");const auto& size=values.array;check(size.size()==3,"Entity size requires three values");for(size_t i=0;i<3;++i){check(size[i].type==Json::Type::Number,"Expected entity size number");d.size_xyz[i]=static_cast<float>(size[i].number);}}
        uint16_t id=0;entity(ctx,d,id);
    }
    if(data.has("generators"))for(const auto& value:data.at("generators").array) {
        GeneratorContent g;g.key=value.at("key").text();owned(ctx,g.key);g.name=value.text("name",g.key);g.revision=value.at("revision").text();
        g.dimension=value.has("dimension")&&value.at("dimension").type==Json::Type::String?resolveDimension(value.at("dimension").text()):integer(value,"dimension",0);
        auto coordinate=[&](const char* field,int fallback){const double n=value.numeric(field,fallback);check(std::isfinite(n)&&n>=Config::WORLD_MIN_Y-1&&n<Config::WORLD_MAX_Y&&std::floor(n)==n,"Invalid generator height");return static_cast<int32_t>(n);};
        g.height=coordinate("height",64);g.water=coordinate("water_level",62);
        check(validDimension(g.dimension)&&!g.revision.empty()&&g.revision.size()<=128&&Config::isValidWorldY(g.height)&&g.water>=Config::WORLD_MIN_Y-1&&g.water<Config::WORLD_MAX_Y,"Invalid data generator");
        g.surface=static_cast<uint16_t>(resolveBlock(value.text("surface","minecraftc:grass")));g.fill=static_cast<uint16_t>(resolveBlock(value.text("fill","minecraftc:stone")));
        check(content().extensions.generators.emplace(g.key,g).second,"Duplicate generator");
    }


    loadCapabilities(ctx,data);
    loadRenderData(ctx,data);
}
void PluginManager::validateContent() {
    validateCapabilities();
    for(const auto& entry:content().extensions.dimensions) {
        const auto& d=entry.second;
        if(entry.first!=0&&entry.first!=2){check(!d.generation.generator.empty(),"Dimension needs a default generator");validateGeneration(d.generation,entry.first);}
        if(!d.deathTarget.empty())resolveDimension(d.deathTarget);
        if(!d.voidTarget.empty())resolveDimension(d.voidTarget);
    }
    const auto english=content().extensions.translations.find("en_us");
    const auto translated=[&](const std::string& key){return english!=content().extensions.translations.end()&&english->second.count(key);};
    for(const auto& entry:content().extensions.nameKeys)check(translated(entry.second),"Localized content name requires English translation");
    for(const auto& entry:content().extensions.dimensions)if(entry.first&&!entry.second.nameKey.empty())check(translated(entry.second.nameKey),"Localized dimension name requires English translation");
    for(const auto& entry:content().extensions.bedTravel)check(translated(entry.second.nameKey),"Travel label requires English translation");
    auto materialExists=[](const std::string& key){if(content().materials.count(key))return true;for(uint16_t i=0;i<static_cast<uint16_t>(BlockTexture::Count);++i)if(key==getBlockTextureAssetName(static_cast<BlockTexture>(i)))return true;return false;};
    for(auto& entry:content().blocks) {
        if(entry.second.familyKind==MC_SLAB_FAMILY) {
            const auto full=resolveBlock(entry.second.fullBlock);check(isFullCollisionBlock(full)&&getBlockProps(full).shape==RenderShape::Cube,"Slab full_block must be a full cube");
        }
        for(const auto& key:entry.second.materials)check(materialExists(key),"Unknown block material");
        if(!entry.second.dropKey.empty())entry.second.drop=resolveItem(entry.second.dropKey);
    }
    for(const auto& entry:content().items)check(materialExists(entry.second.material),"Unknown item material");
    for(const auto& entry:content().extensions.entities)if(!entry.second.drop.empty())resolveItem(entry.second.drop);
}
void PluginManager::buildNetworkDescription() {
    describeCapabilities();
    for(const auto& entry:content().extensions.dimensions) {
        const auto& d=entry.second;
        content().networkDescription["dimension/"+d.key]=nlohmann::json::array({d.name,d.revision,d.generation.generator,d.generation.revision,d.generation.parameters,d.weather,d.natural,d.spawn,d.position,d.deathTarget,d.voidTarget,d.generation.modifiers,d.seedDomain,d.nameKey}).dump();
        if(d.sampleSpawn) {auto description=nlohmann::json::parse(content().networkDescription["dimension/"+d.key]);description.push_back("sampled-v2");content().networkDescription["dimension/"+d.key]=description.dump();}
    }
    for(const auto& entry:content().extensions.bedTravel) {
        const auto& t=entry.second;
        content().networkDescription["bed-travel/"+entry.first]=nlohmann::json::array({dimensionContent(static_cast<DimensionId>(t.source)).key,dimensionContent(static_cast<DimensionId>(t.target)).key,t.nameKey}).dump();
    }
    auto& state=content();
    for(const auto& entry:state.blocks) {
        const auto& b=entry.second;
        state.networkDescription["block/"+b.key]=nlohmann::json::array({b.properties.solid,b.properties.transparent,
            static_cast<int>(b.properties.shape),static_cast<int>(b.properties.layer),b.survival.hardness,
            static_cast<int>(b.survival.preferredTool),static_cast<int>(b.survival.minimumHarvestTier),
            b.survival.unbreakable,b.emission,itemKey(b.drop)}).dump();
        auto boxes=[](const std::optional<BlockCollisionBoxes>& group){nlohmann::json result=nullptr;if(group){result=nlohmann::json::array();for(uint8_t i=0;i<group->count;++i){const auto& box=group->boxes[i];result.push_back({box.min.x,box.min.y,box.min.z,box.max.x,box.max.y,box.max.z});}}return result;};
        if(b.family||b.collision||b.selection||b.model)
            state.networkDescription["shape/"+b.key]=nlohmann::json::array({b.familyKind,b.family?blockKey(static_cast<BlockId>(b.family)):"",b.stateIndex,b.fullBlock,b.manualOpen,b.villagerOpen,b.releaseTicks,b.arrowTrigger,boxes(b.collision),boxes(b.selection),boxes(b.model)}).dump();
    }
    for(const auto& entry:state.items) {
        const auto& i=entry.second.properties;
        state.networkDescription["item/"+entry.second.key]=nlohmann::json::array({static_cast<int>(i.kind),i.maxStack,
            i.maxDurability,static_cast<int>(i.tool),static_cast<int>(i.tier),i.attackDamage,i.attackSpeed,i.food,
            i.saturation,i.placedBlock?blockKey(*i.placedBlock):std::string{},entry.second.spawnEntity==MC_INVALID_ID?std::string{}:entityKey(entry.second.spawnEntity)}).dump();
    }
    auto stack=[](const ItemStack& value){return nlohmann::json::array({itemKey(value.id),value.count,value.damage});};
    for(const auto& entry:state.crafting) {
        const auto& r=entry.second;auto inputs=nlohmann::json::array();
        for(auto id:r.ingredients)inputs.push_back(itemKey(id));
        state.networkDescription["crafting/"+entry.first]=nlohmann::json::array({r.width,r.height,r.allowMirror,r.allowPlankVariants,inputs,stack(r.output)}).dump();
    }
    for(const auto& entry:state.smelting) {
        const auto& r=entry.second;
        state.networkDescription["smelting/"+entry.first]=nlohmann::json::array({itemKey(r.input),stack(r.output),r.cookTicks}).dump();
    }
    for(const auto& command:m_commands)state.networkDescription["command/"+command.first]=std::to_string(command.second.flags);
    std::map<uint32_t,nlohmann::json> callbacks;
    for(const auto& listener:m_listeners)if(!m_info[listener.plugin].manifest.visual) {
        if(!callbacks.count(listener.kind))callbacks[listener.kind]=nlohmann::json::array();
        callbacks[listener.kind].push_back(nlohmann::json::array({m_info[listener.plugin].manifest.id,listener.priority}));
    }
    for(const auto& entry:callbacks)state.networkDescription["callbacks/"+std::to_string(entry.first)]=entry.second.dump();
    auto removed=state.removeCrafting;std::sort(removed.begin(),removed.end());
    if(!removed.empty())state.networkDescription["removed/crafting"]=nlohmann::json(removed).dump();
    removed=state.removeSmelting;std::sort(removed.begin(),removed.end());
    if(!removed.empty())state.networkDescription["removed/smelting"]=nlohmann::json(removed).dump();
    for(const auto& entry:content().extensions.entities) {
        const auto& e=entry.second;
        content().networkDescription["entity/"+entityKey(entry.first)]=nlohmann::json::array({e.key,e.base,e.name,e.health,e.size,e.speed,e.damage,e.hostile,e.natural,e.drop,e.dropCount,e.stateVersion,static_cast<bool>(e.callback)}).dump();
    }
    for(const auto& entry:content().extensions.generators) {
        const auto& g=entry.second;content().networkDescription["generator/"+g.key]=nlohmann::json::array({dimensionContent(g.dimension).key,g.revision,g.height,g.water,blockKey(static_cast<BlockId>(g.surface)),blockKey(static_cast<BlockId>(g.fill))}).dump();
        if(g.batch||g.terrainLod||g.decorationLod)content().networkDescription["generator/"+g.key]=nlohmann::json::array({content().networkDescription["generator/"+g.key],g.lodRevision,bool(g.batch),bool(g.terrainLod),bool(g.decorationLod),bool(g.layers)}).dump();
    }
    for(const auto& entry:content().extensions.modifiers) {
        const auto& m=entry.second;content().networkDescription["modifier/"+m.key]=nlohmann::json::array({dimensionContent(m.dimension).key,m.revision,m.radius}).dump();
        if(m.decorationLod)content().networkDescription["modifier/"+m.key]=nlohmann::json::array({content().networkDescription["modifier/"+m.key],m.lodRevision,true}).dump();
    }

}
void PluginManager::initialize(bool safeMode) {
    check(!m_initialized,"Plugin manager already initialized");m_initialized=true;
    {std::lock_guard<std::mutex> lock(generationFaultMutex());generationFault().clear();}
    const uint64_t revision=content().revision;content()=ContentState{};content().revision=revision;
    const auto config=m_paths.dataRoot/"plugins.json";
    if(std::filesystem::exists(config))try {const auto j=readJson(config);for(const auto& p:j.object){check(p.second.type==Json::Type::Boolean,"Plugin enable setting must be boolean");m_enabled[p.first]=p.second.boolean;}}catch(const std::exception& e){LOG_WARN("Ignoring invalid plugin enable settings: "<<e.what());}
    Json builtinDescriptions;
    try {
        // Presentation metadata is outside gameplay package roots and their fingerprints.
        if(m_paths.assetRoot.empty()) {
            const auto bytes=AssetStore::readTitleBinary("plugins/descriptions.json");
            builtinDescriptions=Json::parse(std::string(bytes.begin(),bytes.end()));
        } else builtinDescriptions=Json::parse(AssetStore::readTextPath(m_paths.assetRoot/"plugins/descriptions.json"));
        check(builtinDescriptions.type==Json::Type::Object,"Invalid builtin description catalog");
    }catch(const std::exception& e){LOG_WARN("Cannot read builtin plugin descriptions: "<<e.what());}
    for(const auto& descriptor:builtinPlugins()){
        PluginInfo p;p.manifest.id=descriptor.id;p.manifest.name=descriptor.name;p.manifest.version="1.0.0";
        if(builtinDescriptions.has(descriptor.id))try {
            p.manifest.readDescription(builtinDescriptions.at(descriptor.id));
        }catch(const std::exception& e){LOG_WARN("Invalid plugin description for "<<descriptor.id<<": "<<e.what());}
        p.manifest.builtin=true;p.manifest.visual=descriptor.visual;p.manifest.defaultEnabled=descriptor.enabled;
        if(*descriptor.resources)p.root=m_paths.assetRoot/descriptor.resources;
        p.enabled=!safeMode&&(m_enabled.count(descriptor.id)?m_enabled.at(descriptor.id):descriptor.enabled);
        if(!p.enabled)p.status="Disabled";m_info.push_back(std::move(p));
    }
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
            check(std::none_of(builtinPlugins().begin(),builtinPlugins().end(),[&](const auto& d){return p.manifest.id==d.id;}),"Reserved built-in plugin identity");
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
        const auto builtinRevision=[&](){for(const auto& d:builtinPlugins())if(info.manifest.id==d.id&&info.manifest.builtin)return std::string(d.revision);return std::string{};}();
        try {
            if(info.manifest.builtin&&m_paths.assetRoot.empty()&&!info.root.empty())
                info.root=materializeBuiltinResources(info.root,m_paths.dataRoot,info.manifest.id);
            if(info.manifest.builtin){const auto& entries=builtinPlugins();auto descriptor=std::find_if(entries.begin(),entries.end(),[&](const auto& d){return info.manifest.id==d.id;});check(descriptor!=entries.end(),"Unknown built-in plugin");loaded.api=descriptor->query(MC_PLUGIN_ABI);}
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
            if(!info.manifest.visual)content().requirements.push_back({info.manifest.id,info.manifest.version+":"+fingerprint(info.root,(info.manifest.builtin?builtinRevision:std::string{}) + ([&]{const auto config=m_paths.dataRoot/"config"/"plugins"/(info.manifest.id+".json");if(!std::filesystem::exists(config))return std::string{};std::ifstream file(config);return std::string(std::istreambuf_iterator<char>(file),{});}()))});
            if(!info.manifest.visual) {
                const auto& m=info.manifest;
                const auto config=m_paths.dataRoot/"config"/"plugins"/(m.id+".json");
                const auto value=std::filesystem::exists(config)?([&]{readJson(config);std::ifstream file(config);return nlohmann::json::parse(file);}()):nlohmann::json::object();
                const std::string behavior=m.builtin?builtinRevision+(info.root.empty()?std::string{}:":"+fingerprint(info.root,"")):m.entry.empty()?"data-api1":
                    !m.networkCompatibility.empty()?"declared:"+m.networkCompatibility:"package:"+fingerprint(info.root,"");
                content().networkDescription["plugin/"+m.id]=nlohmann::json::array({m.version,behavior}).dump();
                content().networkDescription["config/"+m.id]=value.dump();
            }
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
    std::stable_sort(content().extensions.renderPasses.begin(),content().extensions.renderPasses.end(),
        [](const auto& a,const auto& b){return a.priority>b.priority;});
    freezeContent();
    buildNetworkDescription();m_phase=MC_INITIALIZE;dispatcher()=[this](MC_Event& e){dispatch(e);};commandDispatcher()=[this](const std::string& s){return command(s);};
    auto ready=event(MC_INITIALIZE);dispatch(ready);
    // Initialization may persist default options. Match the configuration that
    // the loaded plugins actually start with, without changing save fingerprints.
    for(const auto& info:m_info)if(info.active&&!info.manifest.visual) {
        const auto path=m_paths.dataRoot/"config"/"plugins"/(info.manifest.id+".json");
        if(std::filesystem::exists(path)) {
            readJson(path);std::ifstream file(path);
            content().networkDescription["config/"+info.manifest.id]=nlohmann::json::parse(file).dump();
        }
    }
}
void PluginManager::dispatch(MC_Event& e) {
    pollFaults();
    const ActorScope actorScope({e.player_id,e.dimension,e.role});
    check(!m_dispatching,"Recursive plugin event dispatch");
    if(e.kind==MC_WORLD_CLOSE&&!m_world)return;
    if(e.kind==MC_WORLD_READY)m_world=true;
    if(e.kind==MC_HUD)m_hudOperations=0;
    if(e.kind==MC_UPDATE_PRE||e.kind==MC_WORLD_READY)m_particleEmitted=0;
    const auto previous=m_phase;m_phase=e.kind;m_dispatching=true;
    const bool cancellable=e.kind==MC_BREAK_PRE||e.kind==MC_PLACE_PRE||e.kind==MC_USE_PRE||e.kind==MC_DAMAGE_PRE;
    for(const auto& listener:m_listeners) {
        if(listener.kind!=e.kind||!m_info[listener.plugin].active)continue;
        if(e.role==MC_CLIENT && !m_info[listener.plugin].manifest.visual &&
           e.kind!=MC_INITIALIZE && e.kind!=MC_SHUTDOWN && e.kind!=MC_REGISTER && e.kind!=MC_HUD && e.kind!=MC_ENVIRONMENT)continue;
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
    auto it=m_commands.find(name);if(it==m_commands.end()||!m_info[it->second.plugin].active||!content().runtimeFault.empty())return false;
    if(context().role==MC_CLIENT)return false;
    if(context().player && !(it->second.flags&MC_COMMAND_ALLOW_GUEST))return false;
    check(m_world&&!m_dispatching,"Plugin command requires a world");
    const std::string arguments=split==std::string::npos?"":line.substr(split+1);auto e=event(MC_COMMAND);e.command=name.c_str();e.arguments=arguments.c_str();
    if(operations.player){MC_PlayerSnapshot snapshot{};snapshot.size=sizeof(snapshot);if(operations.player(snapshot))for(int i=0;i<3;++i)e.player[i]=snapshot.position[i];}
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
void PluginManager::discardOperations(){m_queue.clear();for(auto& pair:m_actionResults)if(pair.second.status==MC_ACTION_PENDING)pair.second.status=MC_ACTION_FAILED;}
void PluginManager::pollFaults() {
    check(hostThread(),"Fault polling is main-thread only");
    std::lock_guard<std::mutex> lock(generationFaultMutex());
    if(!generationFault().empty()){content().runtimeFault=generationFault();generationFault().clear();discardOperations();}
}
void PluginManager::setEnabled(size_t i,bool enabled) {
    const auto& p=m_info.at(i);m_enabled[p.manifest.id]=enabled;
    writeText(m_paths.dataRoot/"plugins.json",nlohmann::json(m_enabled).dump(2)+"\n");
    m_info[i].enabled=enabled;m_info[i].status="Restart required";
}
void PluginManager::shutdown() {
    actionDispatcher()={};targetDispatcher()={};useDispatcher()={};
    if(m_shutdown)return;
    m_shutdown=true;
    if(m_initialized){auto e=event(MC_SHUTDOWN);dispatch(e);}
    dispatcher()={};commandDispatcher()={};contextProvider()={};discardOperations();m_listeners.clear();m_commands.clear();operations={};
    for(auto it=m_loaded.rbegin();it!=m_loaded.rend();++it){if(it->api&&it->api->unload)it->api->unload(it->context.get());it->api=nullptr;it->library.reset();}
}
} // namespace Plugins
