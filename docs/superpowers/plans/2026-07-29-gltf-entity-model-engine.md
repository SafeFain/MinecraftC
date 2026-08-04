# glTF Entity Model Engine Implementation Plan

## Completion status

Tasks 1-7 were completed and merged into `main` at `37b9acc`. The implementation
commit is `f6d2e0b`; pre-existing cloud rendering was preserved separately in
`672af93`. Post-merge Release build, CTest 23/23, whitespace check, and OpenGL
startup smoke passed. See `docs/gltf-entity-model-engine.md` for final evidence
and explicitly unrun validation.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hard-coded animal cuboids with reusable GLB assets, hierarchical animation, and OpenGL 3.3 GPU skinning for all eight existing mobs.

**Architecture:** A CPU-only `src/model/` layer owns validated model data and animation evaluation; main-thread `ModelRenderer` owns GPU resources. `EntityModelRegistry` maps gameplay types to shared assets and per-entity animation instances without coupling gameplay to rendering.

**Tech Stack:** C++17, C11 `cgltf` 1.15, OpenGL 3.3 Core, GLM, GLAD, stb_image, GLSL 330, Python 3, CMake/CTest.

## Global Constraints

- Preserve the dirty cloud-rendering work in `Renderer.*`, `RenderEnvironment.h`, `RenderingLogicTests.cpp`, and `assets/shaders/cloud.*`.
- Keep all OpenGL calls and GPU ownership on the main/render thread.
- Resolve models with `RuntimePaths::asset()`; runtime assets are GLB, Y-up, meters, feet at local Y=0.
- Support node TRS, rigid meshes, four-weight linear blend skinning, and at most 64 joints per skin.
- Support `STEP`, `LINEAR`, `CUBICSPLINE`, base-color texture/factor, double-sided materials, and `OPAQUE`/`MASK`/`BLEND`.
- Reject malformed assets and unsupported required extensions without truncation. Fall back to one magenta model without aborting startup.
- Do not change save version 8, serialized entity IDs, physics, AI, spawning, loot, or world generation.
- Death visuals last exactly 1.0 seconds while loot/gameplay timing stays unchanged.
- Do not commit or alter branches without separate authorization. Tasks end at test/diff checkpoints.

## File Map

- `src/model/ModelAsset.h`: immutable CPU geometry, material, hierarchy, skin, animation, image, and bounds data.
- `src/model/GltfLoader.{h,cpp}`: cgltf parsing, validation, conversion, and diagnostics.
- `src/model/ModelAnimation.{h,cpp}`: sampling, blending, hierarchy composition, and joint matrices.
- `src/model/ModelRenderer.{h,cpp}`: GPU buffers, textures, queues, passes, and shaders.
- `src/entity/EntityModelRegistry.{h,cpp}`: type definitions, shared cache, instances, state selection, and fallback.
- `tools/generate_entity_models.py`: deterministic eight-mob GLB and test-fixture generation.
- `tests/{ModelAssetTests,ModelAnimationTests,GltfLoaderTests,EntityModelRegistryTests}.cpp`: CPU regression targets.
- `tests/test_entity_models.py`: reproducibility and asset contract.

---

### Task 1: CPU Model Types and Animation Math

**Files:**
- Create: `src/model/ModelAsset.h`
- Create: `src/model/ModelAnimation.h`
- Create: `src/model/ModelAnimation.cpp`
- Create: `tests/ModelAssetTests.cpp`
- Create: `tests/ModelAnimationTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `model::ModelAsset`, `model::Pose`, `model::ModelInstance`, `sampleChannel`, `evaluatePose`, `blendPoses`, `composeGlobals`, and `jointMatrices`.
- Consumes: GLM only; no OpenGL, renderer, entity, or filesystem dependency.

- [ ] **Step 1: Write failing layout and transform tests**

```cpp
model::ModelAsset asset;
model::Node root;
root.name = "root";
root.translation = {1,0,0};
model::Node child;
child.name = "child";
child.parent = 0;
child.translation = {0,2,0};
root.children = {1};
asset.nodes = {root, child};
auto pose = model::bindPose(asset);
model::composeGlobals(asset, pose);
require(nearVec(glm::vec3(pose.global[1][3]), {1,2,0}),
        "child global transform lost its parent");
model::Vertex vertex{};
vertex.weights = glm::vec4(0);
model::normalizeWeights(vertex);
require(vertex.joints.x == 0u && near(vertex.weights.x, 1.0f),
        "zero weights did not select joint zero");
```

- [ ] **Step 2: Register tests and prove they fail**

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --target model_asset_tests model_animation_tests -j2
```

Expected: compile failure because model headers and symbols do not exist.

- [ ] **Step 3: Define exact CPU data contracts**

```cpp
namespace model {
constexpr std::size_t MAX_JOINTS = 64;
struct Vertex { glm::vec3 position{}, normal{}; glm::vec2 uv{};
  glm::uvec4 joints{0}; glm::vec4 weights{1,0,0,0}; };
enum class AlphaMode { Opaque, Mask, Blend };
struct Material { glm::vec4 baseColor{1}; int image=-1;
  AlphaMode alphaMode=AlphaMode::Opaque; float alphaCutoff=.5f; bool doubleSided=false; };
struct Primitive { std::vector<Vertex> vertices; std::vector<uint32_t> indices;
  int material=-1, skin=-1; glm::vec3 boundsMin{}, boundsMax{}; };
struct Node { std::string name; int parent=-1; std::vector<int> children;
  glm::vec3 translation{0}, scale{1}; glm::quat rotation{1,0,0,0};
  glm::mat4 matrix{1}; bool usesMatrix=false; std::vector<int> primitives; int skin=-1; };
struct Skin { std::vector<int> joints; std::vector<glm::mat4> inverseBindMatrices; };
enum class ChannelPath { Translation, Rotation, Scale };
enum class Interpolation { Step, Linear, CubicSpline };
struct Channel { int node=-1; ChannelPath path{}; Interpolation interpolation{};
  std::vector<float> times; std::vector<glm::vec4> values; };
struct AnimationClip { std::string name; float duration=0; std::vector<Channel> channels; };
struct ImageData { int width=0,height=0,channels=4; std::vector<uint8_t> pixels; };
struct ModelAsset { std::vector<Primitive> primitives; std::vector<Material> materials;
  std::vector<Node> nodes; std::vector<int> sceneRoots; std::vector<Skin> skins;
  std::vector<AnimationClip> animations; std::vector<ImageData> images;
  glm::vec3 boundsMin{}, boundsMax{}; const AnimationClip* findClip(std::string_view) const; };
}
```

- [ ] **Step 4: Implement animation APIs and interpolation**

```cpp
struct Pose { std::vector<glm::vec3> translation,scale;
  std::vector<glm::quat> rotation; std::vector<glm::mat4> global; };
enum class PlaybackState { Idle, Walk, Hurt, Death, Attack };
struct ModelInstance { PlaybackState state=PlaybackState::Idle, previousState=PlaybackState::Idle;
  float stateTime=0, previousTime=0, transition=1; Pose pose;
  std::vector<std::vector<glm::mat4>> jointPalettes; };
Pose bindPose(const ModelAsset&);
glm::vec4 sampleChannel(const Channel&, float time, bool loop);
void evaluatePose(const ModelAsset&, std::string_view, float, bool, Pose&);
void blendPoses(const Pose&, const Pose&, float, Pose&);
void composeGlobals(const ModelAsset&, Pose&);
std::vector<glm::mat4> jointMatrices(const ModelAsset&, const Pose&, std::size_t skin);
```

Implement glTF Hermite cubic splines using in/value/out triplets and delta-scaled tangents. Normalize quaternion samples and use shortest-path `glm::slerp` for pose transitions.

- [ ] **Step 5: Verify focused tests**

```bash
cmake --build build-local --target model_asset_tests model_animation_tests -j2
ctest --test-dir build-local -R 'model_(asset|animation)' --output-on-failure
git diff --check
```

---

### Task 2: Pinned cgltf and Validated Loader

**Files:**
- Create: `external/cgltf/{cgltf.h,LICENSE,UPSTREAM.md}`
- Create: `src/model/GltfLoader.h`
- Create: `src/model/GltfLoader.cpp`
- Create: `tests/GltfLoaderTests.cpp`
- Create: `tests/fixtures/models/README.md`
- Modify: `CMakeLists.txt`
- Modify: `README.md` third-party license table

**Interfaces:**
- Consumes: Task 1 types and project stb_image.
- Produces: `model::LoadResult loadGltf(const std::filesystem::path&)`.

- [ ] **Step 1: Vendor cgltf 1.15 with upstream URL, tag, retrieval date, SHA-256, and unmodified license**

Use `https://github.com/jkuhlmann/cgltf/tree/v1.15`; add `external/cgltf` include paths. Record provenance in both `UPSTREAM.md` and the root README third-party table.

- [ ] **Step 2: Write failing loader tests**

```cpp
auto valid = model::loadGltf(fixture("skinned.glb"));
require(valid && valid.asset->skins[0].joints.size()==2, "valid skin failed");
auto over = model::loadGltf(fixture("skin_65.glb"));
require(!over && contains(over.error,"65") && contains(over.error,"64"),
        "joint limit diagnostic lost counts");
auto broken = model::loadGltf(fixture("accessor_oob.glb"));
require(!broken && contains(broken.error,"accessor"), "bad accessor was accepted");
```

Fixtures cover GLB and external-buffer glTF, embedded PNG, indexed/non-indexed triangles, 16/32-bit indices, TRS/matrix nodes, skinning, three interpolation modes, invalid joints, required extension rejection, and buffer overflow.

- [ ] **Step 3: Prove the loader tests fail**

```bash
cmake --build build-local --target gltf_loader_tests -j2
```

- [ ] **Step 4: Implement the loader contract**

```cpp
struct LoadResult { std::shared_ptr<const ModelAsset> asset; std::string error;
  std::vector<std::string> warnings; explicit operator bool() const { return bool(asset); } };
LoadResult loadGltf(const std::filesystem::path& path);
```

Define `CGLTF_IMPLEMENTATION` in only `GltfLoader.cpp`; call parse, buffer load, and validate. Verify primitive mode, accessor type/count/stride/bounds, joint range, weights, skin size, required extensions, and image data before conversion. Decode PNG with the existing stb implementation, never a second implementation macro.

- [ ] **Step 5: Verify loader and CPU model tests**

```bash
cmake --build build-local --target gltf_loader_tests model_asset_tests model_animation_tests -j2
ctest --test-dir build-local -R '(gltf_loader|model_)' --output-on-failure
git diff --check
```

---

### Task 3: Entity Playback Policy and Death Records

**Files:**
- Modify: `src/entity/EntityLogic.h`
- Modify: `src/entity/EntityManager.h`
- Modify: `src/entity/EntityManager.cpp`
- Modify: `tests/EntityLogicTests.cpp`

**Interfaces:**
- Produces: pure playback helpers and non-serialized `DeadEntityRender` records for Task 5.

- [ ] **Step 1: Add failing priority and timing tests**

```cpp
require(selectEntityPlayback(0,false,false)==EntityPlayback::Idle, "idle failed");
require(selectEntityPlayback(.2f,false,false)==EntityPlayback::Walk, "walk failed");
require(selectEntityPlayback(.2f,true,false)==EntityPlayback::Hurt, "hurt priority failed");
require(selectEntityPlayback(.2f,true,true)==EntityPlayback::Death, "death priority failed");
require(deathPresentationVisible(.999f) && !deathPresentationVisible(1.0f),
        "death display was not exactly 1.0 seconds");
```

- [ ] **Step 2: Prove failure, then implement exact helpers**

```cpp
enum class EntityPlayback { Idle, Walk, Hurt, Death, Attack };
constexpr float ENTITY_DEATH_PRESENTATION_SECONDS = 1.0f;
EntityPlayback selectEntityPlayback(float horizontalSpeed,bool hurt,bool dead);
float walkPlaybackRate(float horizontalSpeed);
bool deathPresentationVisible(float elapsed);
```

Copy only render data to `m_deadEntityRenders` before erasing dead mobs; drop loot immediately; expire records at `elapsed >= 1.0f`; never serialize them or leave dead mobs in `m_entities`.

- [ ] **Step 3: Verify existing and new entity logic**

```bash
cmake --build build-local --target entity_logic_tests -j2
ctest --test-dir build-local -R survival_entity --output-on-failure
git diff --check
```

---

### Task 4: GPU Model Renderer and Shaders

**Files:**
- Create: `src/model/{ModelRenderLogic.h,ModelRenderer.h,ModelRenderer.cpp}`
- Create: `assets/shaders/{model.vert,model.frag}`
- Modify: `src/renderer/{Shader.h,Shader.cpp,Renderer.h,Renderer.cpp}`
- Modify: `tests/RenderingLogicTests.cpp`

**Interfaces:**
- Consumes: ModelAsset, ModelInstance, RenderEnvironment, transforms, camera, VP, tint, and fog.
- Produces: `upload`, `queue`, `flushOpaque`, `flushBlend`, and `clear`.

- [ ] **Step 1: Write failing pure pass/sort/bounds tests**

```cpp
require(modelPass(AlphaMode::Mask)==ModelPass::Opaque, "mask pass wrong");
require(modelPass(AlphaMode::Blend)==ModelPass::Blend, "blend pass wrong");
sortBlended(draws);
require(draws[0].distanceSquared>draws[1].distanceSquared, "blend order wrong");
```

- [ ] **Step 2: Add matrix-array upload and renderer API**

```cpp
void Shader::setMat4Array(const std::string&,const glm::mat4*,std::size_t) const;
using ModelHandle=uint32_t;
struct ModelDraw { ModelHandle model; glm::mat4 transform; const ModelInstance* instance;
  glm::vec4 tint{1}; float distanceSquared=0; };
class ModelRenderer { public: void initialize(const fs::path&,bool);
  ModelHandle upload(std::shared_ptr<const ModelAsset>); void queue(const ModelDraw&);
  void flushOpaque(const glm::mat4&,const RenderEnvironment&,const glm::vec3&,float,float);
  void flushBlend(const glm::mat4&,const RenderEnvironment&,const glm::vec3&,float,float);
  void clear(); };
```

- [ ] **Step 3: Implement GPU resources and shader behavior**

Create interleaved position/normal/UV/integer-joints/weights buffers. Upload RGBA base colors as sRGB textures with `GL_NEAREST`. Restore blend, depth-write, and culling state after passes. GLSL skinning uses:

```glsl
mat4 skin=aWeights.x*uJoints[aJoints.x]+aWeights.y*uJoints[aJoints.y]
         +aWeights.z*uJoints[aJoints.z]+aWeights.w*uJoints[aJoints.w];
vec4 world=uModel*skin*vec4(aPosition,1.0);
```

Apply environment ambient/direct light, fog, alpha cutoff, tint, and conditional manual gamma. Use identity palettes for rigid meshes.

- [ ] **Step 4: Integrate carefully around dirty cloud code and verify**

`Renderer` owns the model renderer and retains `renderCompatibilityEntityCube()` for item/arrow/TNT.

```bash
cmake --build build-local --target rendering_logic_tests minecraftc -j2
ctest --test-dir build-local -R rendering_day_cycle_and_environment --output-on-failure
git diff --check
```

---

### Task 5: Registry and Animal Render Migration

**Files:**
- Create: `src/entity/{EntityModelRegistry.h,EntityModelRegistry.cpp}`
- Create: `tests/EntityModelRegistryTests.cpp`
- Modify: `src/entity/{EntityManager.h,EntityManager.cpp}`
- Modify: `src/renderer/{Renderer.h,Renderer.cpp}`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2–4, RuntimePaths, live entities, and death records.
- Produces: one shared asset/handle per type and one ModelInstance per entity ID.

- [ ] **Step 1: Write failing sharing, clip fallback, and placeholder tests**

```cpp
EntityModelRegistry registry(fakeLoader);
registry.loadAll(assetRoot);
require(loadCount[EntityType::Cow]==1, "cow model was not shared");
require(registry.clipFor(EntityType::Cow,EntityPlayback::Walk)=="idle",
        "missing walk did not fall back");
require(registry.definition(EntityType::Pig).usesPlaceholder,
        "missing model did not use placeholder");
```

- [ ] **Step 2: Implement fixed definitions and shared cache**

```cpp
const std::array<EntityModelDefinition,8> ENTITY_MODELS{{
 {EntityType::Cow,"cow.glb"},{EntityType::Pig,"pig.glb"},
 {EntityType::Sheep,"sheep.glb"},{EntityType::Chicken,"chicken.glb"},
 {EntityType::Zombie,"zombie.glb"},{EntityType::Skeleton,"skeleton.glb"},
 {EntityType::Spider,"spider.glb"},{EntityType::Blastling,"blastling.glb"}}};
```

Load from `assetRoot/models/entities`, cache by resolved path, deduplicate warnings, cross-fade locomotion for 0.15s, clamp walk rate to `[0.5,2.0]`, evaluate poses, and purge stale ID instances.

- [ ] **Step 3: Remove all hard-coded animal part branches**

Queue eight mob types and dead records through the registry. Route only item, arrow, and TNT through the named compatibility cube. Keep their atlas slots and do not delete the legacy atlas.

- [ ] **Step 4: Verify registry and migration**

```bash
cmake --build build-local --target entity_model_registry_tests entity_logic_tests minecraftc -j2
ctest --test-dir build-local -R '(entity_model|survival_entity)' --output-on-failure
rg -n "part\(" src/entity/EntityManager.cpp
git diff --check
```

Expected: tests pass and `rg` finds no animal part calls.

---

### Task 6: Deterministic Eight-Mob GLBs

**Files:**
- Create: `tools/generate_entity_models.py`
- Create: `tests/test_entity_models.py`
- Create: `assets/models/entities/README.md`
- Generate: `assets/models/entities/{cow,pig,sheep,chicken,zombie,skeleton,spider,blastling}.glb`
- Generate: `tests/fixtures/models/*.glb`
- Modify: `ASSET_PIPELINE.md`, `ASSET_SOURCES.md`, `assets/textures/LICENSE.md`, `CMakeLists.txt`

**Interfaces:**
- Produces: self-contained runtime GLBs matching Task 5 filenames and Task 2 fixtures.

- [ ] **Step 1: Write failing deterministic contract test**

```python
assert sha256(first_glb) == sha256(second_glb)
assert set(animation_names(doc)) >= {"idle","walk","hurt","death"}
assert max(map(lambda s: len(s["joints"]), doc["skins"])) <= 64
assert primitive_has(doc,{"POSITION","NORMAL","TEXCOORD_0","JOINTS_0","WEIGHTS_0"})
assert embedded_png_images(doc)
```

- [ ] **Step 2: Implement deterministic standard-library GLB generation**

Write stable JSON/BIN chunks with four-byte padding. Implement `add_box`, `add_bone`, `add_skin`, `add_channel`, and `write_glb`. Use per-face vertices, pixel UVs, and deterministic four-weight data. Generate original quadruped, chicken, humanoid, spider, and blastling silhouettes without copying Minecraft assets.

- [ ] **Step 3: Generate animation and embedded texture data**

Every mob receives a real skin and `idle`, `walk`, `hurt`, `death` clips. Walk alternates limbs; idle adds subtle motion; hurt recoils; death lowers/rotates and holds. Generate original nearest-filtered 16x16/32x32 RGBA PNGs from named palettes and record seed/version/provenance.

- [ ] **Step 4: Generate and validate twice**

```bash
python3 tools/generate_entity_models.py --output assets/models/entities --fixtures tests/fixtures/models
python3 tests/test_entity_models.py
cmake --build build-local --target entity_model_assets -j2
git diff --check
```

Run Khronos glTF Validator on all eight when its CLI is available and record its exact version/results; never claim an unavailable check passed.

---

### Task 7: Package and End-to-End Verification

**Files:**
- Modify: `CMakeLists.txt`, `README.md`, `PLAN.md`, `PROGRESS.md`, `TASKS.md`
- Modify: `.github/workflows/cross-platform.yml` only if current packaging does not cover assets
- Create on completion: `docs/tasks/archive/2026-07-29-gltf-entity-model-engine.md`

**Interfaces:**
- Consumes: complete Tasks 1–6.
- Produces: install/package-ready assets and recorded evidence.

- [ ] **Step 1: Add automated enumeration of exactly eight GLBs, required clips, embedded images, supported extensions, and bone limits**

Keep `install(DIRECTORY assets/ ...)` as the installation source and ensure staged-package tests inspect the installed model/shader files.

- [ ] **Step 2: Configure, build, test, and check whitespace**

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local -j2
ctest --test-dir build-local --output-on-failure
git diff --check
```

Expected: build and all tests pass; report warnings separately.

- [ ] **Step 3: Verify installed files**

```bash
cmake --install build-local --prefix ./install-local
find install-local -path '*assets/models/entities/*.glb' -type f | sort
```

Expected: exactly eight installed GLBs plus model documentation and shaders.

- [ ] **Step 4: Run real OpenGL smoke tests**

```bash
timeout 6s xvfb-run -a ./build-local/minecraftc
```

In a test/debug world, inspect all eight mobs for scale, orientation, nearest textures, idle/walk transition, hurt, 1.0-second death, lighting, fog, alpha, and GL errors. In a temporary staged asset copy, test missing, corrupt, and over-64-joint fallback without modifying checked-in assets.

- [ ] **Step 5: Record final scope evidence**

```bash
git status --short
git diff --stat
rg -n "renderEntityPart|part\(" src/entity src/renderer
```

Confirm cloud work remains, legacy rendering has only item/arrow/TNT consumers, and save/world formats are untouched. Update progress and archive only after every claimed validation has run.

## Final Review Gate

- [ ] All eight mobs use GLBs; no hard-coded animal geometry remains.
- [ ] Loader, hierarchy, all interpolation modes, blending, skinning, fallback, and exact death timing have CPU tests.
- [ ] Rendering covers rigid/skinned, opaque/masked/blended, light, fog, gamma, nearest textures, main-thread ownership, and render origin.
- [ ] Assets are deterministic and licensed; installed packages contain them.
- [ ] Full build, CTest, whitespace, install, validator availability, and OpenGL smoke evidence are reported accurately.
