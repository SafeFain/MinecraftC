#pragma once
#include "core/DynamicLibrary.h"
#include "core/Platform.h"
#include "plugins/ContentRegistry.h"
#include "plugins/Json.h"
#include "plugins/Runtime.h"
#include <functional>
#include <memory>
#include <optional>
#include <thread>

struct MC_PluginContext {
    void* manager=nullptr;
    size_t index=0;
    std::string error;
    bool registrationFailed=false;
};
namespace Plugins {
struct Version {
    uint32_t major=0,minor=0,patch=0;
    std::vector<std::string> prerelease;
    static Version parse(const std::string& value);
    bool operator<(const Version& other) const;
};
struct Dependency {
    std::string id, minimum, maximum;
    bool optional=false;
};
struct Manifest {
    std::string id,name,version,gameMinimum,gameMaximum,entry,data;
    std::vector<Dependency> dependencies;
    std::vector<std::string> before,after,conflicts;
    bool visual=false, builtin=false, defaultEnabled=true;
    static Manifest parse(const Json& value);
};
struct PluginInfo {
    Manifest manifest;
    std::filesystem::path root;
    bool enabled=true, active=false;
    std::string status;
};
struct HostOperations {
    std::function<bool(MC_PlayerSnapshot&)> player;
    std::function<bool(int32_t,int32_t,int32_t,uint16_t&)> getBlock;
    std::function<bool(int32_t,int32_t,int32_t,uint16_t)> setBlock;
    std::function<bool(uint16_t,uint32_t)> giveItem;
    std::function<void(float,float,float,float,const glm::vec4&)> hudRect;
    std::function<void(float,float,const std::string&,const glm::vec4&)> hudText;
    std::function<void(float,float,float,float,uint16_t,const glm::vec4&)> hudImage;
    std::function<std::filesystem::path()> worldDirectory;
};
class PluginManager {
public:
    explicit PluginManager(RuntimePaths paths);
    ~PluginManager();
    PluginManager(const PluginManager&)=delete;
    PluginManager& operator=(const PluginManager&)=delete;
    void initialize(bool safeMode=false);
    void shutdown();
    void dispatch(MC_Event& event);
    bool command(const std::string& command);
    void flushOperations();
    void discardOperations();
    void setEnabled(size_t index,bool enabled);
    const std::vector<PluginInfo>& plugins() const {return m_info;}
    HostOperations operations;
    static std::filesystem::path containedPath(const std::filesystem::path& root,const std::string& relative);
    static std::vector<size_t> resolve(std::vector<PluginInfo>& info);
    // Host ABI adapters use the owning context, never a process-global plugin.
    const MC_Host& host() const {return m_host;}
    int32_t block(MC_PluginContext&,const MC_Block&,uint16_t&);
    int32_t item(MC_PluginContext&,const MC_Item&,uint16_t&);
    int32_t material(MC_PluginContext&,const MC_Material&);
    int32_t recipe(MC_PluginContext&,const MC_Recipe&);
    int32_t subscribe(MC_PluginContext&,uint32_t,int32_t,MC_EventCallback,void*);
    int32_t addCommand(MC_PluginContext&,const char*,MC_EventCallback,void*);
    int32_t enqueue(MC_PluginContext&,std::function<bool()>);
    int32_t storage(MC_PluginContext&,bool,bool,const char*,char*,uint32_t);
    uint32_t phase() const {return m_phase;}
    bool hostThread() const {return std::this_thread::get_id()==m_thread;}
private:
    struct Loaded {
        std::unique_ptr<Platform::DynamicLibrary> library;
        const MC_Plugin* api=nullptr;
        bool loadEntered=false;
        std::unique_ptr<MC_PluginContext> context;
    };
    struct Listener {size_t plugin;uint32_t kind;int32_t priority;MC_EventCallback callback;void* userdata;};
    struct Command {size_t plugin;MC_EventCallback callback;void* userdata;};
    RuntimePaths m_paths;
    MC_Host m_host{};
    std::vector<PluginInfo> m_info;
    std::vector<Loaded> m_loaded;
    std::vector<Listener> m_listeners;
    std::map<std::string,Command> m_commands;
    std::vector<std::function<bool()>> m_queue;
    std::map<std::string,bool> m_enabled;
    uint32_t m_phase=MC_REGISTER;
    bool m_initialized=false,m_shutdown=false,m_dispatching=false,m_world=false;
    std::thread::id m_thread=std::this_thread::get_id();
    size_t m_hudOperations=0;
    void loadData(MC_PluginContext&,const Json&);
    void validateContent();
    void owned(MC_PluginContext&,const std::string&) const;
};
const MC_Plugin* builtinContent(uint32_t);
const MC_Plugin* builtinAtmosphere(uint32_t);
}
