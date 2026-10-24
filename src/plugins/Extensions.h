#pragma once
#include "minecraftc/extensions.h"
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace model {struct ModelAsset;}
namespace Plugins {
struct EntityContent {
    std::string key, name, drop;
    std::filesystem::path model;
    std::shared_ptr<const model::ModelAsset> asset;
    uint16_t id=0, base=1;
    uint32_t dropCount=0, stateVersion=0;
    bool hostile=false, natural=false;
    float health=10, speed=2, damage=2;
    std::array<float,3> size{{.6f,1,.6f}};
    std::function<void(uint32_t,const MC_EntitySnapshot&,MC_EntityIntent&)> callback;
};
struct GenerationConfig {
    std::string generator, revision, parameters="{}";
    std::vector<std::pair<std::string,std::string>> modifiers;
    bool operator==(const GenerationConfig& b) const {
        return generator==b.generator&&revision==b.revision&&parameters==b.parameters&&modifiers==b.modifiers;
    }
};
struct DimensionContent {
    uint64_t seedDomain=0;
    std::string nameKey;
    std::string key, name, revision="1", deathTarget, voidTarget;
    GenerationConfig generation;
    uint32_t weather=MC_WEATHER_NORMAL, spawn=MC_SPAWN_SAFE_SURFACE;
    bool natural=true;
    std::array<double,3> position{{0,65,0}};
    MC_DimensionSpawnCallback sampleSpawn=nullptr;
    void* spawnUserdata=nullptr;
    mutable std::vector<MC_DimensionModifier> modifierView;
};
struct DimensionTravelContent {
    uint32_t source=0,target=0;
    std::string key,nameKey;
};
inline std::map<uint32_t,DimensionContent> builtinDimensions() {
    DimensionContent overworld;overworld.key="minecraftc:overworld";overworld.name="Overworld";overworld.nameKey="lan.overworld";
    DimensionContent nether;
    nether.key="minecraftc:nether";nether.name="Nether";
    nether.seedDomain=0x4e455448455231ULL;
    nether.weather=MC_WEATHER_CLEAR;
    nether.deathTarget="minecraftc:overworld";
    nether.voidTarget="minecraftc:overworld";
    return {{0,overworld},{2,nether}};
}
struct GeneratorContent {
    std::string key, name, revision;
    uint32_t dimension=0;
    MC_ColumnCallback column=nullptr;
    MC_VoxelCallback voxel=nullptr;
    void* userdata=nullptr;
    std::string lodRevision;
    MC_BatchVoxelCallback batch=nullptr;
    MC_GenerationLayersCallback layers=nullptr;
    MC_LodCallback terrainLod=nullptr, decorationLod=nullptr;
    // Data generators use these immutable values instead of callbacks.
    int32_t height=64, water=62;
    uint16_t surface=0, fill=0;
};
struct ModifierContent {
    std::string key, revision, lodRevision;
    uint32_t dimension=0,radius=0;
    MC_DecorationCallback decorate=nullptr;
    MC_LodCallback decorationLod=nullptr;
    void* userdata=nullptr;
};
struct RenderResourceContent {
    std::string key;
    size_t owner=0;
    MC_RenderResource descriptor{};
    std::vector<uint8_t> data;
    std::vector<uint32_t> indices;
    uint32_t shaderStage=0;
};
struct RenderPassContent {
    std::string key;
    size_t owner=0;
    uint32_t stage=0;
    bool replace=false;
    int32_t priority=0;
    std::function<void(const MC_RenderFrame&)> callback;
};
struct DeclarativeRenderPass {
    std::vector<MC_RenderDraw> draws;
    MC_RenderHandle scenePipeline=0;
};
struct BlockBehaviorContent {
    struct Drop {std::string item;uint32_t minimum=0,maximum=0,numerator=0,denominator=1;};
    uint32_t fields=0,light=0,tags=0,substrate=0;
    std::vector<Drop> drops;
    bool climbable=false, replaceable=false;
    uint32_t support=0, encouragement=0, flammability=0, harvestCount=0, resetTicks=0;
    float damage=0;
    std::string harvest, replacement, action;
};
struct ItemBehaviorContent {std::string action, dimension;uint32_t fields=0,fuel=0,tags=0;};
struct LootContent {
    struct Entry {std::string item;uint32_t minimum=0,maximum=0,chance=0;};
    std::string key,revision;
    std::vector<Entry> entries;
};
struct StructureContent {
    bool omitCoarseLod=false;
    struct Block {int32_t x=0,y=0,z=0;uint16_t id=0;std::string loot;uint64_t lootSeed=0;};
    std::string key,name,revision;
    uint32_t dimension=0,cell=64,chance=10,radius=8,height=16,tolerance=2;
    uint64_t domain=0;
    std::vector<Block> blocks;
    std::vector<std::string> namespaces;
    MC_StructureCallback build=nullptr;
    int32_t minY=0,maxY=0,order=0;
    MC_StructurePlacementCallback placements=nullptr;
    MC_StructureCallbackV2 buildV2=nullptr;
    void* userdata=nullptr;
};
inline std::map<std::string,StructureContent> builtinStructures() {
    std::map<std::string,StructureContent> out;
    for(const char* key:{"minecraftc:nether_fortress","minecraftc:bastion_bridge","minecraftc:bastion_stable","minecraftc:bastion_housing","minecraftc:bastion_treasure","minecraftc:nether_fossil","minecraftc:ruined_portal"}) {
        StructureContent s;s.key=key;s.name=key;s.revision="1";s.dimension=s.key=="minecraftc:ruined_portal"?0u:2u;s.cell=256;s.chance=0;
        out.emplace(s.key,std::move(s));
    }
    return out;
}
struct ActionContent {
    std::function<bool(const MC_ActionViewV3&,MC_ActionTargetV2&)> targetV3;
    std::function<bool(const MC_ActionViewV2&,MC_ActionTargetV2&)> targetV2;
    bool rangeFromEye=false,toolPose=false,invalidMarker=false;
    float useCooldown=0;
    std::string key,dimension;
    uint32_t kind=0,durability=0;
    float range=32,cooldown=0;
    std::function<bool(const MC_ActionView&,MC_ActionTarget&)> target;
    bool contextual() const {return bool(targetV2)||bool(targetV3);}
};
struct EnvironmentContent {
    uint32_t skyFlags=0;
    std::array<float,3> gradeColor{{1,1,1}};
    float gradeStrength=0;
    float particleIntensity=-1;
    std::array<std::array<float,4>,MC_SKY_LAYER_VECTORS> skyLayers{};
    std::string key;
    MC_EnvironmentParameters parameters{};
    uint32_t overrides=0;
    std::function<void(MC_EnvironmentParameters&)> callback;
};
struct ParticleContent {
    std::string key,material;MC_ParticleDefinition definition{};size_t owner=0;
    bool advanced=false,collision=true;
    uint32_t shape=0;
    std::array<std::array<float,4>,3> drift{};
};
struct AmbientContent {
    size_t owner=0;
    std::string key;
    uint32_t dimension=0,stateBytes=0;
    bool replaceNative=false;
    std::function<bool(const MC_AmbientContext&,void*)> update;
};
struct HeldModelContent {
    std::shared_ptr<const model::ModelAsset> asset;
    std::filesystem::path path;
    std::array<float,16> first{},third{};
};
struct PlaylistContent {
    std::string key;
    uint32_t dimension=0;
    bool menu=false,random=true;
    float gap=0;
    std::vector<std::filesystem::path> tracks;
};
struct ExtensionContent {
    std::map<uint32_t,std::string> legacyStructureAliases;
    std::map<std::string,DimensionTravelContent> bedTravel;
    std::map<std::string,std::map<std::string,std::string>> translations;
    std::map<std::string,std::string> nameKeys;
    std::map<std::string,std::string> translationAliases;
    std::map<uint16_t,BlockBehaviorContent> blockBehaviors;
    std::map<uint16_t,ItemBehaviorContent> itemBehaviors;
    std::map<std::string,LootContent> loot;
    std::map<std::string,StructureContent> structures=builtinStructures();
    std::map<std::string,ActionContent> actions;
    std::map<uint32_t,EnvironmentContent> environments;
    std::map<std::string,ParticleContent> particles;
    std::map<std::string,AmbientContent> ambient;
    std::map<uint16_t,HeldModelContent> heldModels;
    std::map<std::string,PlaylistContent> playlists;
    std::map<uint32_t,DimensionContent> dimensions=builtinDimensions();
    std::map<uint16_t,EntityContent> entities;
    std::map<std::string,GeneratorContent> generators;
    std::map<std::string,ModifierContent> modifiers;
    std::map<MC_RenderHandle,RenderResourceContent> renderResources;
    std::vector<RenderPassContent> renderPasses;
    std::vector<std::shared_ptr<DeclarativeRenderPass>> dataPasses;
};
// Worker failures are handed to the main thread, never written to ContentState.
inline std::mutex& generationFaultMutex(){static std::mutex m;return m;}
inline std::string& generationFault(){static std::string s;return s;}
inline void failGeneration(const std::string& key) {
    std::lock_guard<std::mutex> lock(generationFaultMutex());
    generationFault()="Plugin generation failed: "+key;
    throw std::runtime_error(generationFault());
}
inline uint64_t generationFingerprint(const GenerationConfig& config) {
    uint64_t hash=1469598103934665603ULL;
    auto add=[&](const std::string& s){for(unsigned char c:s){hash^=c;hash*=1099511628211ULL;}hash^=255;hash*=1099511628211ULL;};
    add(config.generator);add(config.revision);add(config.parameters);
    for(const auto& m:config.modifiers){add(m.first);add(m.second);}
    return hash;
}
} // namespace Plugins
