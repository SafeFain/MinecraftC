#ifndef MINECRAFTC_CAPABILITIES_H
#define MINECRAFTC_CAPABILITIES_H
/* Included by extensions.h after its generation declarations. */
#define MC_BEHAVIORS_EXTENSION "minecraftc.behaviors"
#define MC_STRUCTURES_EXTENSION "minecraftc.structures"
#define MC_PLAYER_ACTIONS_EXTENSION "minecraftc.player_actions"
#define MC_ENVIRONMENT_EXTENSION "minecraftc.environment"
#define MC_PRESENTATION_EXTENSION "minecraftc.presentation"
#define MC_MATERIAL_SEQUENCES_EXTENSION "minecraftc.material_sequences"
typedef struct MC_MaterialSequenceDefinition {
    uint32_t size;
    MC_Material base;
    uint32_t variants,frames,fps_milli,seed;
    const char* const* images;
    const char* const* normals;
    const char* const* properties;
} MC_MaterialSequenceDefinition;
typedef struct MC_MaterialSequencesV1 {
    uint32_t size,version;
    int32_t (*register_sequence)(MC_PluginContext*,const MC_MaterialSequenceDefinition*);
} MC_MaterialSequencesV1;
#define MC_MAX_STRUCTURE_BLOCKS 16384u
/* Zeroed behavior is inert. Support: 0 none, 1 below, 2 above. */
typedef struct MC_BlockBehavior {
    uint32_t size;
    const char* block;
    uint32_t climbable, replaceable, support, fire_encouragement, fire_flammability;
    float contact_damage;
    const char* harvest_item;
    uint32_t harvest_count;
    const char* harvest_replacement;
    uint32_t harvest_reset_ticks;
    const char* action; /* Optional special action when using this block. */
} MC_BlockBehavior;
typedef struct MC_ItemBehavior {
    uint32_t size;
    const char* item;
    const char* action;
    const char* dimension; /* Null/empty permits all dimensions. */
} MC_ItemBehavior;
typedef struct MC_BehaviorsV1 {
    uint32_t size, version;
    int32_t (*register_block)(MC_PluginContext*,const MC_BlockBehavior*);
    int32_t (*register_item)(MC_PluginContext*,const MC_ItemBehavior*);
} MC_BehaviorsV1;

/* V1 layouts are frozen. Flags distinguish absence from explicit zero/empty.
   Tags describe semantics, never numeric ID ranges. Support accepts any tag. */
enum { MC_TAG_LEAVES=1u, MC_TAG_PLANKS=2u, MC_TAG_FERTILE=4u,
       MC_TAG_SAND=8u, MC_TAG_HEAVEN_SOIL=16u, MC_TAG_MOON_SOIL=32u,
       MC_TAG_NATURAL_ROCK=64u, MC_TAG_PLANT=128u, MC_TAG_FLOWER=256u };
enum { MC_BLOCK_LIGHT=1u, MC_BLOCK_TAGS=2u, MC_BLOCK_DROPS=4u, MC_BLOCK_SUBSTRATE=8u };
enum { MC_ITEM_FUEL=1u, MC_ITEM_TAGS=2u };
typedef struct MC_BreakDrop {
    const char* item;
    uint32_t minimum, maximum, numerator, denominator;
} MC_BreakDrop;
typedef struct MC_BlockBehaviorV2 {
    uint32_t size;
    MC_BlockBehavior base;
    uint32_t fields, light_dampening, tags, substrate_tags;
    const MC_BreakDrop* drops;
    uint32_t drop_count;
} MC_BlockBehaviorV2;
typedef struct MC_ItemBehaviorV2 {
    uint32_t size;
    MC_ItemBehavior base;
    uint32_t fields, fuel_ticks, tags;
} MC_ItemBehaviorV2;
typedef struct MC_BehaviorsV2 {
    uint32_t size, version;
    int32_t (*register_block)(MC_PluginContext*,const MC_BlockBehaviorV2*);
    int32_t (*register_item)(MC_PluginContext*,const MC_ItemBehaviorV2*);
    const MC_BehaviorsV1* v1;
} MC_BehaviorsV2;
typedef struct MC_LootEntry {
    const char* item;
    uint32_t minimum, maximum, chance; /* chance: 0..100 */
} MC_LootEntry;
typedef struct MC_LootTable {
    uint32_t size;
    const char* key;
    const char* revision;
    const MC_LootEntry* entries;
    uint32_t count;
} MC_LootTable;
/* Coordinates relative to the accepted anchor. Loot is only valid on chests. */
typedef struct MC_StructureBlock {
    int32_t x,y,z;
    uint16_t block;
    const char* loot;
} MC_StructureBlock;
typedef struct MC_StructureContext {
    uint32_t size;
    MC_GenerationContext generation;
    int32_t anchor[3];
    uint64_t variant;
    /* Read-only terrain authority; valid only during the structure callback. */
    int32_t (*surface)(void*,int32_t,int32_t,MC_SurfaceSample*);
    void* reader;
} MC_StructureContext;
typedef int32_t (*MC_StructureCallback)(const MC_StructureContext*,MC_StructureBlock*,uint32_t,uint32_t*,void*);
typedef struct MC_StructureDefinition {
    uint32_t size, dimension;
    const char* key;
    const char* name;
    const char* revision;
    uint32_t cell_size, chance, radius, height, terrain_tolerance;
    uint64_t seed_domain;
    const MC_StructureBlock* blocks;
    uint32_t count;
    MC_StructureCallback build; /* Pure concurrent callback; no plugin context. */
    void* userdata;
} MC_StructureDefinition;
typedef struct MC_LocatedStructure {
    uint32_t size, dimension;
    int32_t anchor[3];
    uint64_t variant;
} MC_LocatedStructure;
typedef struct MC_StructuresV1 {
    uint32_t size, version;
    int32_t (*register_structure)(MC_PluginContext*,const MC_StructureDefinition*);
    int32_t (*register_loot)(MC_PluginContext*,const MC_LootTable*);
    /* Bounded search, reads terrain samples without loading chunks. */
    int32_t (*locate)(MC_PluginContext*,uint32_t,const char*,int32_t,int32_t,uint32_t,MC_LocatedStructure*);
} MC_StructuresV1;
/* V2 placements are final accepted instances, not suggestions for V1's terrain
   or reservation rules. Each anchor belongs to the requested floor-divided
   cell. Return at most MC_MAX_STRUCTURE_INSTANCES in any order. Callbacks are
   pure/concurrent; context.surface reads undecorated terrain only. */
#define MC_MAX_STRUCTURE_INSTANCES 16u
typedef struct MC_StructureInstance {
    int32_t anchor[3];
    uint64_t variant;
    int32_t order;
} MC_StructureInstance;
typedef int32_t (*MC_StructurePlacementCallback)(const MC_StructureContext*,int32_t,int32_t,MC_StructureInstance*,uint32_t,uint32_t*,void*);
typedef struct MC_StructureBlockV2 {
    MC_StructureBlock base;
    uint64_t loot_seed; /* Exact value, including zero. */
} MC_StructureBlockV2;
typedef int32_t (*MC_StructureCallbackV2)(const MC_StructureContext*,MC_StructureBlockV2*,uint32_t,uint32_t*,void*);
typedef struct MC_StructureDefinitionV2 {
    uint32_t size, dimension;
    const char *key, *name, *revision;
    uint32_t cell_size, radius;
    int32_t min_y, max_y; /* Inclusive relative Y; span <= world height. */
    int32_t order; /* Ascending, then key; V1 has order zero. */
    MC_StructurePlacementCallback placements;
    MC_StructureCallbackV2 build;
    void* userdata;
} MC_StructureDefinitionV2;
typedef struct MC_StructuresV2 {
    uint32_t size, version;
    int32_t (*register_structure)(MC_PluginContext*,const MC_StructureDefinitionV2*);
    const MC_StructuresV1* v1;
} MC_StructuresV2;
/* V3 retains V2 geometry/metadata. Coarse policy: 0 exact-center fallback
   (the V2 default), 1 omit above size 1 when the generator owns silhouettes. */
typedef struct MC_StructuresV3 {
    uint32_t size,version;
    int32_t (*set_coarse_lod_policy)(MC_PluginContext*,const char* structure,uint32_t policy);
    const MC_StructuresV2* v2;
} MC_StructuresV3;
/* Read-only action callbacks run on the main thread both for preview and host
   execution. Clients never supply an authoritative destination. */
typedef struct MC_ActionView {
    uint32_t size, dimension;
    uint64_t player_id;
    double position[3], eye[3];
    float forward[3];
    uint16_t item;
    int32_t (*block)(void*,int32_t,int32_t,int32_t,uint16_t*);
    void* reader;
} MC_ActionView;
typedef struct MC_ActionTarget {
    uint32_t size, dimension, valid;
    double position[3];
} MC_ActionTarget;
typedef int32_t (*MC_ActionTargetCallback)(const MC_ActionView*,MC_ActionTarget*,void*);
typedef enum MC_ActionKind { MC_ACTION_TELEPORT=0, MC_ACTION_TRAVEL=1 } MC_ActionKind;
typedef struct MC_PlayerAction {
    uint32_t size, kind;
    const char* key;
    const char* dimension; /* Required target dimension for travel. */
    float range, cooldown;
    uint32_t durability;
    MC_ActionTargetCallback target; /* Null: ray surface for teleport / entry policy for travel. */
    void* userdata;
} MC_PlayerAction;
typedef struct MC_ActionRequest {
    uint32_t size;
    uint64_t player_id;
    const char* action;
} MC_ActionRequest;
typedef enum MC_ActionStatus { MC_ACTION_PENDING=0, MC_ACTION_SUCCEEDED=1, MC_ACTION_FAILED=2 } MC_ActionStatus;
typedef struct MC_ActionResult {
    uint32_t size, status;
    uint64_t request_id;
} MC_ActionResult;
typedef struct MC_PlayerActionsV1 {
    uint32_t size, version;
    int32_t (*register_action)(MC_PluginContext*,const MC_PlayerAction*);
    int32_t (*request)(MC_PluginContext*,const MC_ActionRequest*,uint64_t*);
    int32_t (*result)(MC_PluginContext*,uint64_t,MC_ActionResult*);
    int32_t (*preview)(MC_PluginContext*,uint64_t,const char*,MC_ActionTarget*);
    int32_t (*cooldown)(MC_PluginContext*,uint64_t,const char*,float*);
    /* Explicit host-authorized teleport; loaded safe landing required. */
    int32_t (*teleport)(MC_PluginContext*,uint64_t,uint32_t,const double*);
} MC_PlayerActionsV1;

/* Read services are borrowed for this callback only, never load chunks and
   cannot mutate the world. Layer slots are stable, including absent layers. */
typedef struct MC_ActionLayer {int32_t bottom,top;uint32_t present,biome;} MC_ActionLayer;
typedef struct MC_ActionViewV2 {
    uint32_t size;
    MC_ActionView base;
    uint32_t sneaking, source, phase, hit_valid; /* source: 0 item, 1 block; phase: 0 preview, 1 execution (use press) */
    int32_t hit[3], face[3];
    uint16_t hit_block;
    MC_GenerationContext generation;
    double spawn[3];
    uint32_t builtin_generation;
    int32_t (*layers)(void*,int32_t,int32_t,MC_ActionLayer*,uint32_t,uint32_t*);
    int32_t (*block_flags)(void*,uint16_t,uint32_t*); /* full collision 1, empty collision 2 */
} MC_ActionViewV2;
typedef struct MC_ActionTargetV2 {MC_ActionTarget base;uint32_t handled;} MC_ActionTargetV2;
typedef int32_t (*MC_ActionTargetCallbackV2)(const MC_ActionViewV2*,MC_ActionTargetV2*,void*);
typedef struct MC_PlayerActionV2 {
    uint32_t size;
    MC_PlayerAction base;
    MC_ActionTargetCallbackV2 target;
    uint32_t range_from_eye, tool_pose, invalid_marker;
    float use_cooldown;
} MC_PlayerActionV2;
typedef struct MC_PlayerActionsV2 {
    uint32_t size,version;
    int32_t (*register_action)(MC_PluginContext*,const MC_PlayerActionV2*);
    const MC_PlayerActionsV1* v1;
} MC_PlayerActionsV2;
typedef struct MC_ActionViewV3 {
    uint32_t size;
    MC_ActionViewV2 base;
    const char* dimension_key;
    const char* generation_provider; /* Empty for native generation. Borrowed. */
} MC_ActionViewV3;
typedef int32_t (*MC_ActionTargetCallbackV3)(const MC_ActionViewV3*,MC_ActionTargetV2*,void*);
typedef struct MC_PlayerActionV3 {
    uint32_t size;
    MC_PlayerActionV2 base; /* V2 target must be null. */
    MC_ActionTargetCallbackV3 target;
} MC_PlayerActionV3;
typedef struct MC_PlayerActionsV3 {
    uint32_t size,version;
    int32_t (*register_action)(MC_PluginContext*,const MC_PlayerActionV3*);
    const MC_PlayerActionsV2* v2;
} MC_PlayerActionsV3;
/* advancing: 0 inherit world rule, 1 paused, 2 advance. cycle_seconds 0 inherits. */
typedef struct MC_DimensionTime {
    uint32_t size, dimension, cycle_seconds, advancing;
    float phase;
} MC_DimensionTime;
typedef struct MC_EnvironmentParameters {
    uint32_t size, dimension, sky_style, clouds;
    float day_phase, daylight, star_intensity;
    float sun_direction[3], moon_direction[3], light_direction[3];
    MC_Environment colors;
    float rain, thunder, lightning;
    float sun_size, moon_size;
} MC_EnvironmentParameters;
typedef int32_t (*MC_EnvironmentCallback)(MC_PluginContext*,MC_EnvironmentParameters*,void*);
typedef struct MC_EnvironmentProfile {
    uint32_t size, dimension;
    const char* key;
    uint32_t override_parameters; /* bits: colors 1, celestial 2, weather 4 */
    MC_EnvironmentParameters parameters;
    MC_EnvironmentCallback callback;
    void* userdata;
} MC_EnvironmentProfile;
typedef struct MC_EnvironmentV1 {
    uint32_t size, version;
    int32_t (*register_profile)(MC_PluginContext*,const MC_EnvironmentProfile*);
    int32_t (*time)(MC_PluginContext*,uint32_t,MC_DimensionTime*);
    int32_t (*set_time)(MC_PluginContext*,const MC_DimensionTime*);
} MC_EnvironmentV1;
/* Generic cloud-sea / aurora layers. Twelve vec4 parameters, in order:
   sea motion(scale,dx,dz,opacity), mask(horizon low/high,noise low/high),
   day/night colors, noise(octave scale,offset,first weight,second weight),
   view(minimum ray denominator,0,0,0), followed by aurora motion, mask,
   two colors, noise(first scale,second scale,offset,first weight),
   view(ray bias,minimum denominator,hue frequency,minimum night).
   Zero opacity disables a layer. All inputs must be finite. */
#define MC_SKY_LAYER_VECTORS 12u
enum {MC_SKY_NO_ATMOSPHERE=1u,MC_SKY_NO_LOW_CLOUDS=2u,MC_SKY_NO_VOXEL_CLOUDS=4u,MC_SKY_NO_LIGHT_SHAFTS=8u};
typedef struct MC_SkyParameters {uint32_t size;float layers[MC_SKY_LAYER_VECTORS][4];} MC_SkyParameters;
typedef struct MC_EnvironmentProfileV2 {
    uint32_t size;
    MC_EnvironmentProfile base;
    MC_SkyParameters sky;
    uint32_t override_particle_intensity;
    float particle_intensity;
    uint32_t sky_flags;
    float grade_color[3],grade_strength;
} MC_EnvironmentProfileV2;
typedef struct MC_EnvironmentV2 {
    uint32_t size,version;
    int32_t (*register_profile)(MC_PluginContext*,const MC_EnvironmentProfileV2*);
    const MC_EnvironmentV1* v1;
} MC_EnvironmentV2;
typedef struct MC_ParticleDefinition {
    uint32_t size;
    const char* key;
    const char* material;
    float color[4], lifetime, scale, gravity, velocity[3];
} MC_ParticleDefinition;
typedef struct MC_ParticleEmission {
    uint32_t size, dimension, count, local_only;
    const char* particle;
    double position[3];
} MC_ParticleEmission;
typedef struct MC_HeldModel {
    uint32_t size;
    const char* item;
    const char* path; /* Package-contained glTF/GLB, static bind pose. */
    float first_person[16], third_person[16]; /* Column-major transforms. */
} MC_HeldModel;
typedef struct MC_MusicPlaylist {
    uint32_t size, dimension, menu, random;
    const char* key;
    const char* const* tracks; /* Package-contained WAV files. */
    uint32_t count;
    float gap_seconds;
} MC_MusicPlaylist;
typedef struct MC_PresentationV1 {
    uint32_t size, version;
    int32_t (*register_particle)(MC_PluginContext*,const MC_ParticleDefinition*);
    int32_t (*emit)(MC_PluginContext*,const MC_ParticleEmission*);
    int32_t (*register_held_model)(MC_PluginContext*,const MC_HeldModel*);
    int32_t (*register_playlist)(MC_PluginContext*,const MC_MusicPlaylist*);
} MC_PresentationV1;
/* V2 keeps V1 layouts frozen. Drift axes are {frequency,phase_multiplier,
   acceleration,wave}; wave 0 is sine, 1 cosine. Shape: 0 atlas, 1 glow,
   2 soft tuft, 3 four-point sparkle. Collision: 0 free motion, 1 world. */
typedef struct MC_ParticleDefinitionV2 {
    uint32_t size;
    MC_ParticleDefinition base;
    uint32_t shape,collision;
    float drift[3][4];
} MC_ParticleDefinitionV2;
typedef struct MC_ParticleInstanceV2 {
    uint32_t size,dimension;
    const char* particle;
    double position[3];
    float velocity[3],color[4],lifetime,scale,phase,rotation,angular_velocity;
} MC_ParticleInstanceV2;
typedef struct MC_AmbientContext {
    uint32_t size,dimension,capacity;
    MC_GenerationContext generation;
    double viewer[3];
    float dt,daylight;
    uint64_t seed;
    void* reader;
    uint64_t (*random_bits)(void*);
    void (*mix_random)(void*,uint64_t);
    int32_t (*emit)(void*,const MC_ParticleInstanceV2*);
    const char* generation_provider;
    int32_t (*layers)(void*,int32_t,int32_t,MC_GenerationLayer*,uint32_t,uint32_t*);
} MC_AmbientContext;
typedef int32_t (*MC_AmbientCallback)(const MC_AmbientContext*,void* state,void* userdata);
typedef struct MC_AmbientDefinition {
    uint32_t size,dimension,state_bytes,replace_native;
    const char* key;
    MC_AmbientCallback update;
    void* userdata;
} MC_AmbientDefinition;
typedef struct MC_ItemIcon {uint32_t size;const char* item;const char* path;} MC_ItemIcon;
/* Visual-only packages may target existing items; overlays never register items.
 * Gameplay packages retain ownership restrictions. Images are package-local 16x16 PNGs. */
typedef struct MC_PresentationV2 {
    uint32_t size,version;
    int32_t (*register_particle)(MC_PluginContext*,const MC_ParticleDefinitionV2*);
    int32_t (*emit)(MC_PluginContext*,const MC_ParticleInstanceV2*);
    int32_t (*register_ambient)(MC_PluginContext*,const MC_AmbientDefinition*);
    const MC_PresentationV1* v1;
    int32_t (*register_item_icon)(MC_PluginContext*,const MC_ItemIcon*);
} MC_PresentationV2;
#endif
