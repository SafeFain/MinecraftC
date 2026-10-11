#ifndef MINECRAFTC_EXTENSIONS_H
#define MINECRAFTC_EXTENSIONS_H
#include "plugin.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MC_ENTITIES_EXTENSION "minecraftc.entities"
#define MC_WORLDGEN_EXTENSION "minecraftc.worldgen"
#define MC_RENDER_EXTENSION "minecraftc.render"
#define MC_EXTENSION_VERSION 1u
#define MC_ENTITY_STATE_LIMIT 4096u
#define MC_BUILTIN_ENTITY_COUNT 14u
/* Runtime entity types are not stable across launches. Persist namespaced keys. */
typedef struct MC_EntitySnapshot {
    uint32_t size, dimension;
    uint64_t id;
    uint16_t type;
    double position[3];
    float velocity[3], health, age;
    const char* state; /* JSON, borrowed for this callback */
    uint32_t state_version;
} MC_EntitySnapshot;
typedef struct MC_EntityIntent {
    uint32_t size;
    float movement[3]; /* horizontal speed in blocks/second; vertical component ignored */
    uint32_t jump;
    uint64_t attack_player; /* UINT64_MAX means no attack; 0 identifies the host */
    const char* state; /* optional replacement JSON; plugin-owned, valid until next callback */
} MC_EntityIntent;
typedef enum MC_EntityEvent { MC_ENTITY_SPAWN=0, MC_ENTITY_DECIDE=1, MC_ENTITY_DAMAGE=2, MC_ENTITY_DEATH=3 } MC_EntityEvent;
typedef int32_t (*MC_EntityCallback)(MC_PluginContext*,uint32_t,const MC_EntitySnapshot*,MC_EntityIntent*,void*);
typedef struct MC_EntityDefinition {
    uint32_t size;
    const char* key;
    const char* target; /* optional builtin key minecraftc:entity_N, explicit override */
    const char* name;
    const char* model; /* optional package-relative GLB/glTF; sibling .anim.json */
    const char* drop_item;
    uint32_t drop_count, template_type, hostile, natural_spawn, state_version;
    float health, size_xyz[3], speed, attack_damage;
    MC_EntityCallback callback; /* decision replaces the template AI */
    void* userdata;
} MC_EntityDefinition;
typedef struct MC_SpawnEggDefinition {
    uint32_t size;
    MC_Item item;
    const char* entity;
} MC_SpawnEggDefinition;
typedef struct MC_EntitiesV1 {
    uint32_t size, version;
    int32_t (*register_type)(MC_PluginContext*,const MC_EntityDefinition*,uint16_t*);
    uint16_t (*find_type)(MC_PluginContext*,const char*);
    int32_t (*spawn)(MC_PluginContext*,uint32_t,uint16_t,const double*);
    int32_t (*snapshot)(MC_PluginContext*,uint32_t,uint64_t,MC_EntitySnapshot*,char*,uint32_t);
    int32_t (*remove)(MC_PluginContext*,uint32_t,uint64_t);
    int32_t (*set_intent)(MC_PluginContext*,uint32_t,uint64_t,const MC_EntityIntent*);
    int32_t (*register_spawn_egg)(MC_PluginContext*,const MC_SpawnEggDefinition*,uint16_t*);
} MC_EntitiesV1;
/* Generation functions may run concurrently on workers. No plugin context or
   main-thread host calls are available. Outputs depend only on coordinates,
   seed and immutable parameters; all strings are borrowed for the call. */
typedef struct MC_GenerationContext {
    uint32_t size, dimension;
    uint64_t seed;
    const char* parameters;
} MC_GenerationContext;
typedef struct MC_SurfaceSample {
    uint32_t size;
    int32_t height, water_level;
    uint32_t biome;
} MC_SurfaceSample;
typedef int32_t (*MC_ColumnCallback)(const MC_GenerationContext*,int32_t,int32_t,MC_SurfaceSample*,void*);
typedef int32_t (*MC_VoxelCallback)(const MC_GenerationContext*,int32_t,int32_t,int32_t,uint16_t*,void*);
typedef struct MC_GeneratorDefinition {
    uint32_t size;
    const char* key;
    const char* name;
    const char* revision;
    uint32_t dimension;
    MC_ColumnCallback column;
    MC_VoxelCallback voxel;
    void* userdata;
} MC_GeneratorDefinition;
typedef struct MC_DecorationBlock { int32_t x,y,z; uint16_t block; } MC_DecorationBlock;
typedef int32_t (*MC_DecorationCallback)(const MC_GenerationContext*,int32_t,int32_t,MC_DecorationBlock*,uint32_t,uint32_t*,void*);
typedef struct MC_ModifierDefinition {
    uint32_t size;
    const char* key;
    const char* revision;
    uint32_t dimension, radius;
    MC_DecorationCallback decorate; /* anchor grid is host-owned 16x16 world cells */
    void* userdata;
} MC_ModifierDefinition;
typedef struct MC_WorldgenV1 {
    uint32_t size, version;
    int32_t (*register_generator)(MC_PluginContext*,const MC_GeneratorDefinition*);
    int32_t (*register_modifier)(MC_PluginContext*,const MC_ModifierDefinition*);
} MC_WorldgenV1;
typedef uint64_t MC_RenderHandle;
#define MC_RENDER_SCENE_COLOR UINT64_MAX
typedef enum MC_RenderStage { MC_RENDER_SKY=0, MC_RENDER_SHADOWS, MC_RENDER_TERRAIN,
    MC_RENDER_ENTITIES, MC_RENDER_TRANSLUCENT, MC_RENDER_POST, MC_RENDER_STAGE_COUNT } MC_RenderStage;
typedef enum MC_RenderResourceKind { MC_RENDER_SHADER=0, MC_RENDER_TEXTURE,
    MC_RENDER_MESH, MC_RENDER_BUFFER, MC_RENDER_TARGET, MC_RENDER_PIPELINE } MC_RenderResourceKind;
/* Vertex contract: float position[3], uv[2]. Shader contract: set 0 binding 0
   sampler2D; optional uniform block at binding 1; 80-byte push constants
   mat4 MVP + vec4 tint. GLSL Vulkan SPIR-V. */
typedef struct MC_RenderVertex { float position[3], uv[2]; } MC_RenderVertex;
typedef struct MC_RenderResource {
    uint32_t size, kind;
    const char* key;
    const char* path; /* package-relative SPIR-V for shader */
    const void* data;
    uint32_t data_bytes, width, height;
    const uint32_t* indices;
    uint32_t index_count;
    MC_RenderHandle vertex_shader, fragment_shader, texture;
    uint32_t depth_test, blend;
} MC_RenderResource;
typedef struct MC_RenderFrame {
    uint32_t size, stage, width, height;
    double time, origin[3];
    float view_projection[16];
    uint64_t resource_revision;
    uint64_t scene_count; /* scene submissions exposed through scene_draw */
} MC_RenderFrame;
typedef struct MC_RenderDraw {
    uint32_t size;
    MC_RenderHandle mesh, pipeline, target, uniforms; /* target 0 is current scene */
    float transform[16], tint[4];
    uint32_t clip_space;
} MC_RenderDraw;
typedef int32_t (*MC_RenderCallback)(MC_PluginContext*,const MC_RenderFrame*,void*);
typedef struct MC_RenderPass {
    uint32_t size, stage, replace;
    int32_t priority;
    const char* key;
    MC_RenderCallback callback;
    void* userdata;
} MC_RenderPass;
typedef struct MC_RenderV1 {
    uint32_t size, version;
    int32_t (*register_resource)(MC_PluginContext*,const MC_RenderResource*,MC_RenderHandle*);
    int32_t (*register_pass)(MC_PluginContext*,const MC_RenderPass*);
    int32_t (*draw)(MC_PluginContext*,const MC_RenderDraw*);
    int32_t (*scene_draw)(MC_PluginContext*,uint64_t,MC_RenderHandle);
    int32_t (*update_buffer)(MC_PluginContext*,MC_RenderHandle,const void*,uint32_t);
} MC_RenderV1;
#ifdef __cplusplus
}
#endif
#endif
