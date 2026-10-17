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
    std::string id,name,version,gameMinimum,gameMaximum,entry,data,networkCompatibility;
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
    std::function<std::vector<uint64_t>()> players;
    std::function<bool(uint64_t,MC_PlayerSnapshot&,uint32_t&)> playerById;
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
    void pollFaults();
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
    int32_t addCommand(MC_PluginContext&,const char*,MC_EventCallback,void*,uint32_t flags=0);
    int32_t enqueue(MC_PluginContext&,std::function<bool()>);
    int32_t storage(MC_PluginContext&,bool,bool,const char*,char*,uint32_t);
    int32_t blockFamily(MC_PluginContext&,const MC_BlockFamily&,uint16_t&);
    int32_t blockGeometry(MC_PluginContext&,const MC_BlockGeometry&);
    std::function<bool(uint32_t,int32_t,int32_t,int32_t,bool,bool)> blockInteraction;
    int32_t entity(MC_PluginContext&,const MC_EntityDefinition&,uint16_t&);
    int32_t dimensionDefinition(MC_PluginContext&,const MC_DimensionDefinition&,uint32_t&);
    int32_t dimensionDefinitionV2(MC_PluginContext&,const MC_DimensionDefinitionV2&,uint32_t&);
    std::function<bool(uint64_t,uint32_t)> travelPlayer;
    int32_t generator(MC_PluginContext&,const MC_GeneratorDefinition&);
    int32_t generatorV2(MC_PluginContext&,const MC_GeneratorDefinitionV2&);
    int32_t modifierV2(MC_PluginContext&,const MC_ModifierDefinitionV2&);
    int32_t modifier(MC_PluginContext&,const MC_ModifierDefinition&);
    int32_t renderResource(MC_PluginContext&,const MC_RenderResource&,MC_RenderHandle&);
    int32_t renderPass(MC_PluginContext&,const MC_RenderPass&);
    const void* extension(MC_PluginContext*,const char*,uint32_t);
    void callbackFault(size_t,const std::string&);
    const void* capability(const char*,uint32_t);
    void blockBehavior(MC_PluginContext&,const MC_BlockBehavior&);
    void itemBehavior(MC_PluginContext&,const MC_ItemBehavior&);
    void lootTable(MC_PluginContext&,const MC_LootTable&);
    void structure(MC_PluginContext&,const MC_StructureDefinition&);
    void structureV2(MC_PluginContext&,const MC_StructureDefinitionV2&);
    void playerAction(MC_PluginContext&,const MC_PlayerAction&);
    void environmentProfile(MC_PluginContext&,const MC_EnvironmentProfile&);
    void particleType(MC_PluginContext&,const MC_ParticleDefinition&);
    void heldModel(MC_PluginContext&,const MC_HeldModel&);
    void playlist(MC_PluginContext&,const MC_MusicPlaylist&);
    void validateCapabilities();
    void describeCapabilities();
    void loadCapabilities(MC_PluginContext&,const Json&);
    void reference(MC_PluginContext&,const std::string&) const;
    std::function<bool(uint32_t,const std::string&,int32_t,int32_t,uint32_t,MC_LocatedStructure&)> locateStructure;
    std::function<bool(uint64_t,const std::string&,bool,MC_ActionTarget&)> actionTarget;
    std::function<bool(uint64_t,const std::string&)> executeAction;
    std::function<uint64_t(uint64_t)> actionState;
    std::function<bool(uint64_t,const std::string&,float&)> actionCooldown;
    std::function<bool(uint64_t,uint32_t,const double*)> teleportPlayer;
    std::function<bool(uint32_t,MC_DimensionTime&)> dimensionTime;
    std::function<bool(const MC_DimensionTime&)> setDimensionTime;
    std::function<bool(const MC_ParticleEmission&)> emitParticle;
    uint64_t submitAction(MC_PluginContext&,const MC_ActionRequest&);
    MC_ActionResult actionResult(MC_PluginContext&,uint64_t) const;
    std::function<bool(uint32_t,uint16_t,const double*)> spawnEntity;
    std::function<bool(uint32_t,uint64_t,MC_EntitySnapshot&,std::string&)> entitySnapshot;
    std::function<bool(uint32_t,uint64_t)> removeEntity;
    std::function<bool(uint32_t,uint64_t,const MC_EntityIntent&)> entityIntent;
    std::function<bool(size_t,const MC_RenderDraw&)> renderDraw;
    std::function<bool(size_t,uint64_t,MC_RenderHandle)> renderScene;
    std::function<bool(size_t,MC_RenderHandle,const void*,uint32_t)> renderBuffer;
    uint32_t phase() const {return m_phase;}
    bool readOnly() const {return m_readOnly;}
    bool hostThread() const {return std::this_thread::get_id()==m_thread;}
private:
    struct Loaded {
        std::unique_ptr<Platform::DynamicLibrary> library;
        const MC_Plugin* api=nullptr;
        bool loadEntered=false;
        std::unique_ptr<MC_PluginContext> context;
    };
    struct Listener {size_t plugin;uint32_t kind;int32_t priority;MC_EventCallback callback;void* userdata;};
    struct Command {size_t plugin;MC_EventCallback callback;void* userdata;uint32_t flags=0;};
    RuntimePaths m_paths;
    MC_Host m_host{};
    std::vector<PluginInfo> m_info;
    std::vector<Loaded> m_loaded;
    std::vector<Listener> m_listeners;
    std::map<std::string,Command> m_commands;
    std::vector<std::function<bool()>> m_queue;
    std::map<std::string,bool> m_enabled;
    uint32_t m_phase=MC_REGISTER;
    bool m_readOnly=false;
    bool m_initialized=false,m_shutdown=false,m_dispatching=false,m_world=false;
    std::thread::id m_thread=std::this_thread::get_id();
    size_t m_hudOperations=0;
    uint32_t m_particleEmitted=0;
    uint64_t m_nextAction=1;
    struct ActionReceipt {size_t owner=0;uint32_t status=MC_ACTION_PENDING;};
    std::map<uint64_t,ActionReceipt> m_actionResults;
    void loadData(MC_PluginContext&,const Json&);
    void loadRenderData(MC_PluginContext&,const Json&);
    void validateContent();
    void buildNetworkDescription();
    void owned(MC_PluginContext&,const std::string&) const;
};
const MC_Plugin* builtinContent(uint32_t);
const MC_Plugin* builtinAtmosphere(uint32_t);
}
