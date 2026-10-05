// Application orchestration tests: drives GameFlowController (and, in later
// stages, ApplicationInputRouter) with real offscreen-SDL collaborators — a
// temp-dir GameSession, a concrete GameUiController/GameScenePresenter, and
// an inert IGameRenderer stub for the loading gate. Locks in the application
// state machine, inventory/command transitions, respawn, and persistence
// behavior that Application.cpp previously owned untested.
//
// The same stub pattern as InputRoutingTests/SessionFlowTests keeps this
// target free of graphics backend and asset loading: UIRenderer and Localization are
// provided as inert link-level definitions.

#include "app/ApplicationInputController.h"
#include "app/ApplicationInputRouter.h"
#include "app/GameFlowController.h"
#include "app/GameScenePresenter.h"
#include "app/GameSession.h"
#include "app/GameUiController.h"
#include "audio/AudioSystem.h"
#include "core/InputCodes.h"
#include "core/RuntimeClock.h"
#include "core/Window.h"
#include "entity/EntityManager.h"
#include "game/ClientSettings.h"
#include "game/Item.h"
#include "game/WorldCatalog.h"
#include "platform/sdl/SdlClipboard.h"
#include "renderer/GameRenderer.h"
#include "ui/Menu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "debug/Log.h"
#include "Config.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

struct GameSessionTestAccess {
    static void waitWorkers(GameSession& session) {
        session.threadPool.waitIdle();
    }
    static void markPlayerDead(GameSession& session) {
        session.playerDead = true;
    }
    static void setPlayerPosition(GameSession& session,
                                  const glm::dvec3& position) {
        session.player.setPosition(position);
    }
    static void setBlock(GameSession& session, int x, int y, int z, BlockId block) {
        session.world.setBlock(x, y, z, block);
    }
};

// UIRenderer is a graphics-backed facade; the flow tests never render, so these
// inert definitions keep the target free of the Vulkan backend (same pattern
// as InputRoutingTests).
namespace {
glm::vec2 drawnShortcutSlot{-1,-1};
struct DrawnRounded { float x,y,w,h; glm::vec4 color; };
bool recordUi=false;
std::vector<DrawnRounded> drawnRounded;
std::unordered_map<std::string,glm::vec2> drawnLabels;
}

UIRenderer::~UIRenderer() = default;
void UIRenderer::beginUIFrame(int, int) {}
void UIRenderer::setCanvas(float, float, float, float) {}
void UIRenderer::endUIFrame() {}
void UIRenderer::drawRoundedRect(float x,float y,float w,float h,float,const glm::vec4& color) {
    if (recordUi) drawnRounded.push_back({x,y,w,h,color});
}
void UIRenderer::renderTextAlpha(const std::string& label,float x,float y,float,const glm::vec3&,float) {
    if (recordUi) drawnLabels[label]={x,y};
}
void UIRenderer::setOpacity(float) {}
void UIRenderer::drawRect(float, float, float, float, const glm::vec4&) {}
void UIRenderer::drawBlockIcon(float, float, float, float, BlockId) {}
void UIRenderer::drawItemIcon(float x,float y,float w,float h,const ItemStack& stack) {
    if (stack.id==ItemId::DIRT && stack.count==8) drawnShortcutSlot={x+w*.5f,y+h*.5f};
}
void UIRenderer::drawDurability(float, float, float, const ItemStack&) {}
void UIRenderer::drawPanel(float, float, float, float, const glm::vec4&) {}
void UIRenderer::drawTooltip(float, float, const ItemStack&) {}
void UIRenderer::setLocalization(const Localization& localization) {
    m_localization = &localization;
}
void UIRenderer::renderText(const std::string&, float, float, float,
                            const glm::vec3&) {}
glm::vec2 UIRenderer::measureText(const std::string&, float) {
    return {0, 0};
}

// Command/console and HUD strings return keys verbatim (SessionFlowTests
// pattern) so no localization assets are needed.
std::string Localization::text(std::string_view key) const {
    return std::string(key);
}
std::string Localization::format(
    std::string_view key,
    std::initializer_list<std::string> /*arguments*/) const {
    return std::string(key);
}
std::string Localization::itemName(ItemId) const {
    return "item";
}
std::string Localization::actionName(InputAction) const {
    return "action";
}
std::string Localization::bindingName(const InputBinding&) const {
    return "binding";
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

glm::vec4 drawnBorder(glm::vec2 center) {
    glm::vec4 color{0};
    float width=0;
    for (const auto& r:drawnRounded)
        if (r.h<=Config::UI_BUTTON_HEIGHT && r.color.a>.9f && r.w>width &&
            center.x>=r.x && center.x<=r.x+r.w && center.y>=r.y && center.y<=r.y+r.h) {
            width=r.w;color=r.color;
        }
    return color;
}

// Inert renderer for the loading gate: mesh uploads/releases are no-ops.
class StubRenderer : public IGameRenderer {
public:
    // ── IRenderDevice ──────────────────────────────────────────────
    RenderDeviceCapabilities capabilities() const override { return {}; }
    RenderMeshHandle createMesh(const MeshData&) override { return {}; }
    void destroyMesh(RenderMeshHandle) override {}
    RenderTextureHandle createTexture(const TextureData&,
                                      const TextureSamplerDesc&) override {
        return {};
    }
    void destroyTexture(RenderTextureHandle) override {}
    RenderMaterialHandle createMaterial(const MaterialDesc&) override {
        return {};
    }
    void destroyMaterial(RenderMaterialHandle) override {}
    void beginFrame(const FrameData&) override {}
    void draw(const DrawCommand&) override {}
    void endFrame() override {}
    void resize(int, int) override {}
    void waitIdle() override {}
    RendererPerformanceStats performanceStats() const override { return {}; }

    // ── IGameRenderer ──────────────────────────────────────────────
    void initialize(Window&, const std::filesystem::path&) override {}
    void reinitialize(const std::filesystem::path&) override {}
    void suspendPresentation() override {}
    void resumePresentation() override {}
    void beginFrame() override {}
    void setVisualQuality(VisualQuality) override {}
    void setEnhancedVisuals(bool) override {}
    void setLeafTransparency(bool) override {}
    void finishScene(const PostProcessState&) override {}
    void setEnvironment(const RenderEnvironment&, const glm::vec3&) override {}
    void renderSky(const RenderEnvironment&, const glm::mat4&,
                   const glm::vec3&, bool) override {}
    void renderChunk(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                     bool) override {}
    void renderLod(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                   const glm::vec4&,
                   float, float, bool) override {}
    void renderChunkShadows(ShadowQuality, const glm::mat4&, const glm::mat4&,
                            const glm::dvec3&,
                            const std::vector<ShadowChunkSubmission>&) override {
    }
    void uploadChunkMesh(ChunkMesh& mesh) override { mesh.gpuReady = true; }
    void releaseChunkMesh(ChunkMesh& mesh) override { mesh.abandonGpuResources(); }
    void beginTranslucent() override {}
    void endTranslucent() override {}
    void bindBlockShader() const override {}
    void unbindBlockShader() const override {}
    void renderWireframe(const glm::vec3&, const glm::vec3&,
                         const glm::mat4&, const glm::vec3&) override {}
    void renderEntity(const glm::vec3&, const glm::vec3&, const glm::vec3&,
                      int, const glm::mat4&) override {}
    void renderCompatibilityEntityCube(const glm::vec3&, const glm::vec3&,
                                       const glm::vec3&, int, float,
                                       const glm::mat4&,
                                       SmoothLightSample) override {}
    model::ModelRenderer& modelRenderer() override {
        throw std::logic_error(
            "modelRenderer is not used by application flow tests");
    }
    void flushModels(const glm::mat4&) override {}
    void beginViewModel(const glm::mat4&) override {}
    void renderEntityPart(const glm::vec3&, const glm::vec3&,
                          const glm::vec3&, float, const glm::vec3&, int,
                          const glm::mat4&, SmoothLightSample) override {}
    void renderParticles(const std::vector<ParticleRenderData>&,
                         const glm::mat4&, const glm::vec3&, const glm::vec3&,
                         float) override {}
    void renderClouds(const glm::dvec3&, const glm::mat4&, uint64_t, float,
                      int) override {}
    void setViewProjection(const glm::mat4&) override {}
    void setFrustum(const Frustum&) override {}
    const Frustum& getFrustum() const override { return m_frustum; }
    RenderTextureHandle getBlockAtlasTexture() const override { return {}; }
    uint32_t blockAtlasTilesPerSide() const override { return 1; }

private:
    Frustum m_frustum;
};

class ItemRecordingRenderer final : public StubRenderer {
public:
    std::unordered_map<uint32_t,MeshData> meshes;
    std::unordered_map<uint32_t,MaterialDesc> materials;
    std::unordered_map<uint32_t,TextureData> textures;
    std::vector<DrawCommand> draws;
    uint32_t next=1;
    size_t cubes=0;
    RenderMeshHandle createMesh(const MeshData& data) override {
        validateMeshData(data);const uint32_t id=next++;meshes.emplace(id,data);return {id};
    }
    void destroyMesh(RenderMeshHandle id) override {meshes.erase(id.value);}
    RenderTextureHandle createTexture(const TextureData& data,const TextureSamplerDesc&) override {
        validateTextureData(data);const uint32_t id=next++;textures.emplace(id,data);return {id};
    }
    void destroyTexture(RenderTextureHandle id) override {textures.erase(id.value);}
    RenderMaterialHandle createMaterial(const MaterialDesc& data) override {
        const uint32_t id=next++;materials.emplace(id,data);return {id};
    }
    void destroyMaterial(RenderMaterialHandle id) override {materials.erase(id.value);}
    RenderTextureHandle getBlockAtlasTexture() const override {return {999};}
    uint32_t blockAtlasTilesPerSide() const override {return 32;}
    void draw(const DrawCommand& command) override {draws.push_back(command);}
    void renderCompatibilityEntityCube(const glm::vec3&,const glm::vec3&,
        const glm::vec3&,int,float,const glm::mat4&,SmoothLightSample) override {++cubes;}
};

void checkDroppedItems(const std::filesystem::path& assets) {
    ItemRecordingRenderer renderer;
    HeldItemRenderer items;items.initialize(renderer,assets);
    const glm::vec3 position(-16.5f,70,-.5f);
    for (uint16_t i=1;i<static_cast<uint16_t>(ItemId::COUNT);++i) {
        const ItemStack item{static_cast<ItemId>(i),1,0};
        renderer.draws.clear();items.updateUseState(false,0,false,1);
        items.renderDropped(item,glm::mat4(1),position,0,0,{1,0});
        require(!renderer.draws.empty(),"every nonempty item must have dropped geometry");
        const auto original=renderer.draws;
        const size_t meshCount=renderer.meshes.size();
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        for (const auto& command : original) {
            const auto& mesh=renderer.meshes.at(command.mesh.value);
            require(command.useCustomViewProjection && command.tint==glm::vec4(1),
                    "world item lost its projection or daylight tint");
            const auto& props=getItemProps(item.id);
            const bool cube=props.placedBlock && getBlockProps(*props.placedBlock).shape==RenderShape::Cube;
            require((renderer.materials.at(command.material.value).baseColorTexture.value==999)==cube,
                    "dropped block must use the shared block atlas");
            const uint32_t count=command.indexCount ? command.indexCount : mesh.indices.size();
            require(command.firstIndex+count<=mesh.indices.size(),"dropped tool index range invalid");
            for (uint32_t n=command.firstIndex;n<command.firstIndex+count;++n) {
                const glm::vec3 p(command.model*glm::vec4(mesh.vertices[mesh.indices[n]].position,1));
                minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);
            }
        }
        const glm::vec3 extent=maximum-minimum;
        require(std::max({extent.x,extent.y,extent.z})<=.401f && minimum.y>=position.y,
                "dropped geometry must fit above the ground");
        require(glm::length((minimum+maximum)*.5f-position-glm::vec3(0,.25f,0))<.001f,
                "dropped model must rotate around its geometry center");
        items.updateUseState(true,1,true,1);renderer.draws.clear();
        items.renderDropped(item,glm::mat4(1),position,0,0,{1,0});
        require(renderer.meshes.size()==meshCount && renderer.draws.size()==original.size(),
                "player use state changed dropped geometry or allocated another mesh");
        for (size_t n=0;n<original.size();++n)
            require(renderer.draws[n].model==original[n].model &&
                    renderer.draws[n].indexCount==original[n].indexCount,
                    "dropped bow inherited player charge or arrow");
    }
    renderer.draws.clear();items.renderDropped({},glm::mat4(1),position,0,0,{1,0});
    require(renderer.draws.empty(),"empty stacks must not draw");
    items.renderDropped({ItemId::DIAMOND,1,0},glm::mat4(1),position,1,1,{0,0});
    require(renderer.draws.front().tint.r<.03f,"dark dropped items must retain world lighting");
    items.reset();
    require(renderer.meshes.empty() && renderer.materials.empty() && renderer.textures.empty(),
            "shared item renderer leaked cached resources");
    World world;EntityManager entities(world);
    entities.spawnItem(glm::dvec3(position),{ItemId::DIAMOND,1,0});
    entities.render(renderer,glm::mat4(1),{0,0,0});
    require(renderer.cubes==0,"item entities must not draw a duplicate placeholder cube");
    std::cout<<"PASS dropped item models, bounds, lighting, use isolation and cache lifecycle\n";
}

// Drives the loading gate the same way the application does until the world
// render target is fully loaded or the budget is exhausted.
bool loadWorld(GameSession& session, StubRenderer& stub, RuntimeClock& clock) {
    for (int i = 0; i < 2000; ++i) {
        if (session.advanceLoading(&stub, clock.now())) {
            require(session.loadingSnapshot().fraction == 1.0f,
                    "completed loading fills the overall progress bar");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

struct Harness {
    // Must outlive the session: the world's mesh pipeline holds a raw
    // IGameRenderer* that World::~World dereferences via releaseAllMeshes().
    StubRenderer stub;
    std::filesystem::path root;
    RuntimeClock clock;
    platform::sdl::SdlClipboard clipboard;
    AudioSystem audio;
    ClientSettings settings;
    GameSession session;
    GameScenePresenter scene;
    GameUiController ui;
    ApplicationInputController inputs;
    GameFlowController flow;
    ApplicationInputRouter router;

    explicit Harness(const std::filesystem::path& testRoot, Window& window)
        : root(testRoot),
          session(root / "saves"),
          ui(session.inventory(), clipboard),
          flow(session, ui, scene, audio, window, clock, settings, clipboard),
          router(window, ui, session, inputs, scene, settings, flow, clock) {
        session.configureLod(
            {true, 16, LodAggressiveness::Fast, LodPrecision::Low});
    }

    // Wires the minimal callbacks the application's initialize() sets up.
    void wireCallbacks() {
        ui.menuCallbacks.onResume = [this]() { flow.resume(); };
    }

    // Binds explicit keyboard actions so routing tests do not depend on
    // default settings.
    void bindKeys() {
        auto bind = [this](InputAction action, int key) {
            settings.bindings[static_cast<size_t>(action)] = {
                InputDevice::Keyboard, key};
        };
        bind(InputAction::Inventory, Key::E);
        bind(InputAction::Command, Key::C);
        bind(InputAction::Perspective, Key::X);
        bind(InputAction::DropItem, Key::Q);
        bind(InputAction::DirectCommand, Key::Slash);
        bind(InputAction::SwapOffhand, Key::F);
        bind(InputAction::Hotbar1, Key::Num1);
    }

    // Loads a fresh world and reaches the Playing state.
    void play(const std::string& worldId) {
        const int oldRenderDistance = Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE = 2;
        flow.startGame(worldId, true);
        require(loadWorld(session, stub, clock),
                "loading gate completes in the router harness");
        flow.completeLoading();
        Config::RENDER_DISTANCE = oldRenderDistance;
    }
};
}

int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--dropped-item-tests") {
        checkDroppedItems(argv[2]);return 0;
    }
    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-application-flow-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::unique_ptr<Window> window;
    try {
        window = std::make_unique<Window>(
            640, 480, "application flow test", Window::SurfaceMode::InputOnly,
            true, false);
    } catch (const std::exception&) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        try {
            window = std::make_unique<Window>(
                640, 480, "application flow test", Window::SurfaceMode::InputOnly,
                true, false);
        } catch (const std::exception&) {
            std::cerr << "FAILED: no SDL video driver can create a window\n";
            return 1;
        }
    }

    {
        Harness harness(root / "loading-phases", *window);
        const int oldRenderDistance = Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE = 2;
        harness.session.configureLod(
            {false, 16, LodAggressiveness::PowerSaver, LodPrecision::Low});
        const std::string id = harness.session.createWorld(
            "Loading phases", 42, GameMode::Creative, Difficulty::Peaceful,
            true, WorldType::Superflat);
        harness.flow.startGame(id, true);
        require(harness.session.loadingSnapshot().phase ==
                    GameSession::LoadingPhase::Chunks,
                "initial loading shows the chunk generation phase");
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "near chunks load with distant terrain disabled");
        require(harness.session.loadingSnapshot().phase ==
                    GameSession::LoadingPhase::PreparingChunks,
                "disabled LOD does not introduce a distant-terrain phase");

        // Introduce a cold LOD selection after near meshes are complete so
        // the final loading stage is exercised independently of worker timing.
        harness.session.configureLod(
            {true, 16, LodAggressiveness::PowerSaver, LodPrecision::Low});
        require(!harness.session.advanceLoading(&harness.stub, harness.clock.now()),
                "the loading gate still waits for distant terrain coverage");
        const auto loading = harness.session.loadingSnapshot();
        require(loading.progress.total > 0 &&
                    loading.progress.completed == loading.progress.total &&
                    loading.phase == GameSession::LoadingPhase::DistantTerrain,
                "completed near chunks switch the status to distant terrain");
        require(loading.phaseFraction < 1.0f &&
                    loading.phaseFraction == harness.session.worldState().lodCoverageFraction() &&
                    std::abs(loading.fraction -
                        (0.9f + loading.phaseFraction * 0.1f)) < 0.00001f,
                "the distant-terrain percentage matches the final progress segment");
        recordUi = true;
        drawnLabels.clear();
        harness.ui.render(harness.session, harness.settings, harness.inputs,
                          *window, GameState::LoadingWorld, false);
        require(drawnLabels.count("loading.lod") == 1 &&
                    drawnLabels.count("loading.preparing") == 0,
                "the loading UI identifies distant terrain instead of completed chunks");
        recordUi = false;
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "distant terrain completes the final loading phase");
        const auto exactCache = harness.root / "saves" / id /
            "lod" / "r5" / "d_0" / "exact";
        require(!std::filesystem::exists(exactCache) ||
                    std::filesystem::is_empty(exactCache),
                "loading does not wait for background exact LOD cache extraction");
        harness.flow.completeLoading();
        harness.session.updatePlaying(0.0f, &harness.stub, {});
        GameSessionTestAccess::waitWorkers(harness.session);
        require(std::filesystem::exists(exactCache) &&
                    !std::filesystem::is_empty(exactCache),
                "playing resumes exact LOD cache extraction in the background");
        harness.flow.backToMainMenu();
        harness.flow.startGame(id, false);
        require(!harness.session.loadingSnapshot().newWorld &&
                    harness.session.loadingSnapshot().phase == GameSession::LoadingPhase::Chunks,
                "reopening a save starts with the cached chunk loading phase");
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "existing-world loading retains complete LOD coverage");
        Config::RENDER_DISTANCE = oldRenderDistance;
    }

    {
        recordUi=true;
        UIRenderer ui; Localization localization;ui.setLocalization(localization);
        int confirmed=0;
        Button button("feedback",[&]{++confirmed;});
        button.setPosition(10,20);button.setSize(200,44);button.setHovered(true);
        ui.advanceTime(.5f);drawnRounded.clear();button.render(ui);
        require(drawnBorder({110,42}).g>UiTheme::BORDER.g+.2f,
                "mouse hover visibly highlights the border");
        button.setPressed(true);ui.advanceTime(.1f);drawnRounded.clear();button.render(ui);
        require(button.containsPoint(10,20)&&button.containsPoint(210,64),
                "pressed motion keeps the original click corners active");
        button.setPressed(false);button.setHovered(false);button.activate();
        ui.advanceTime(1.0f/60);drawnRounded.clear();button.render(ui);
        require(confirmed==1&&drawnBorder({110,42}).g>UiTheme::BORDER.g+.2f,
                "immediate activation still leaves a visible pulse");
        button.setEnabled(false);button.activate();
        require(confirmed==1,"disabled buttons cannot activate");

        MenuCallbacks callbacks;PauseMenu menu(callbacks,localization);
        const auto renderMenu=[&]{drawnRounded.clear();ui.advanceTime(.5f);menu.render(ui,640,480);};
        renderMenu();
        const auto resume=drawnLabels.at("menu.pause.resume");
        const auto options=drawnLabels.at("menu.home.settings");
        const auto quit=drawnLabels.at("menu.home.quit");
        menu.onMouseMove(quit.x,quit.y);renderMenu();
        require(drawnBorder(quit).g>UiTheme::BORDER.g+.2f&&drawnBorder(resume)==UiTheme::BORDER,
                "pointer hover clears the previous navigation highlight");
        menu.onKeyPress(Key::Down);renderMenu();
        require(drawnBorder(options)==UiTheme::ACCENT&&drawnBorder(quit)==UiTheme::BORDER,
                "keyboard/controller navigation clears the stationary pointer highlight");
        recordUi=false;
    }

    {
        Harness harness(root,*window);
        int confirmed=0;
        MenuCallbacks callbacks;callbacks.onResume=[&]{++confirmed;};
        harness.ui.activeMenu=std::make_unique<PauseMenu>(callbacks,harness.ui.localization);
        harness.ui.guiScale=1;
        harness.settings.controlMode=ControlMode::Touch;
        recordUi=true;
        const auto safe=window->safeArea();
        harness.ui.activeMenu->render(harness.ui.renderer,safe.width,safe.height);
        const auto center=drawnLabels.at("menu.pause.resume");
        const auto event=[&](TouchPhase phase,glm::vec2 position) {
            const double sx=static_cast<double>(window->windowWidth())/window->width();
            const double sy=static_cast<double>(window->windowHeight())/window->height();
            harness.router.handleTouch({{9,1},phase,(safe.x+position.x)*sx,
                (window->height()-safe.y-position.y)*sy});
        };
        event(TouchPhase::Begin,center);
        require(harness.inputs.uiTouch.buttonDown&&confirmed==0,
                "touch menus capture on finger down and activate on release");
        event(TouchPhase::Move,center+glm::vec2(0,60));
        event(TouchPhase::End,center);
        require(confirmed==0&&!harness.inputs.uiTouch.active,
                "scrolling from a menu button cancels activation");
        event(TouchPhase::Begin,center);event(TouchPhase::Cancel,center);
        require(confirmed==0,"system touch cancellation never activates a menu button");
        event(TouchPhase::Begin,center);event(TouchPhase::End,center);
        require(confirmed==1&&!harness.inputs.uiPointerVisible,
                "a normal tap activates once and clears hover after finger lift");
        event(TouchPhase::Begin,center);
        event(TouchPhase::Move,center+glm::vec2(500,0));event(TouchPhase::End,center);
        require(confirmed==1,"releasing outside the captured button cancels a tap");
        harness.ui.activeMenu->onMouseButton(MouseButton::Left,ButtonAction::Press,center.x,center.y);
        harness.router.bind();
        SDL_Event lost{};lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;window->handleEvent(&lost);
        harness.ui.activeMenu->onMouseButton(MouseButton::Left,ButtonAction::Release,center.x,center.y);
        require(confirmed==1,"focus loss cancels captured mouse presses");
        window->setKeyCallback({});window->setCharCallback({});
        window->setMouseButtonCallback({});window->setScrollCallback({});
        window->setTouchCallback({});window->setFocusCallback({});
        window->setScreenKeyboardCallback({});
        recordUi=false;
    }

    {
        // Native CI windows may never gain keyboard focus. Keep synthetic
        // controller sampling independent of desktop activation for this block.
        const char* previousHint = SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
        const bool hadPreviousHint = previousHint != nullptr;
        const std::string previousBackgroundInput = previousHint ? previousHint : "";
        require(SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                                        "1", SDL_HINT_OVERRIDE),
                "UI virtual controller permits background input");
        SDL_VirtualJoystickDesc descriptor{};SDL_INIT_INTERFACE(&descriptor);
        descriptor.type=SDL_JOYSTICK_TYPE_GAMEPAD;descriptor.naxes=6;descriptor.nbuttons=15;
        descriptor.axis_mask=(1u<<SDL_GAMEPAD_AXIS_LEFTX)|(1u<<SDL_GAMEPAD_AXIS_LEFTY);
        descriptor.button_mask=1u<<SDL_GAMEPAD_BUTTON_SOUTH;
        descriptor.name="MinecraftC UI Gamepad";
        const auto id=SDL_AttachVirtualJoystick(&descriptor);
        require(id!=0,"UI test virtual controller attaches");
        SDL_Event added{};added.type=SDL_EVENT_GAMEPAD_ADDED;added.gdevice.which=id;window->handleEvent(&added);
        auto* joystick=SDL_OpenJoystick(id);require(joystick!=nullptr,"UI virtual joystick opens");
        Harness harness(root,*window);
        int confirmed=0;
        MenuCallbacks callbacks;callbacks.onOpenSettings=[&]{++confirmed;};
        harness.ui.activeMenu=std::make_unique<PauseMenu>(callbacks,harness.ui.localization);
        const auto update=[&]{SDL_UpdateJoysticks();harness.router.beginFrame(harness.clock.now(),false);};
        require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,32767),"UI controller axis updates");
        update();
        require(harness.inputs.gamepadAxes[1] > .99f,"UI controller down axis is sampled");
        require(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTY,0),"UI controller axis centers");
        update();
        require(harness.inputs.gamepadAxes[1] == 0,"UI controller centered axis is sampled");
        require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true),"UI controller confirms");
        update();update();
        require(harness.inputs.gamepadButtons[0],"UI controller A press is sampled");
        require(confirmed==1,"controller navigation and held A confirm exactly once");
        require(!harness.inputs.uiPointerVisible,"controller keeps the stationary cursor hidden");
        require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false),"UI controller releases");
        update();
        harness.inputs.uiTouch.active=true;
        require(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true),"UI mixed-input confirms");
        update();harness.inputs.uiTouch.active=false;update();
        require(confirmed==1,"a held controller button cannot steal an active touch or activate after lift");
        SDL_CloseJoystick(joystick);require(SDL_DetachVirtualJoystick(id),"UI controller detaches");
        SDL_Event removed{};removed.type=SDL_EVENT_GAMEPAD_REMOVED;removed.gdevice.which=id;window->handleEvent(&removed);
        require(hadPreviousHint
                    ? SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                                              previousBackgroundInput.c_str(), SDL_HINT_OVERRIDE)
                    : SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS),
                "UI virtual controller restores background input hint");
    }

    for (const auto size:{glm::ivec2(960,600),glm::ivec2(320,640),glm::ivec2(640,240)}) {
        InventoryModel inventory;inventory.slot(1)={ItemId::DIRT,8,0};
        SurvivalInventoryScreen screen(inventory);
        UIRenderer ui;Localization localization;ui.setLocalization(localization);
        screen.render(ui,960,600,-10000,-10000);
        screen.onGamepadNavigate(1,0);
        ui.advanceTime(.5f);screen.render(ui,size.x,size.y,-10000,-10000);
        screen.onGamepadAction(0);
        require(inventory.slot(1).empty(),"controller focus follows its slot across canvas resizing");
        screen.onGamepadNavigate(1,0);screen.onGamepadAction(0);
        require(inventory.slot(2).id==ItemId::DIRT&&inventory.slot(2).count==8,
                "controller navigation places into the adjacent fitted slot");
        recordUi=true;
        screen.onMouseMove(-10000,-10000);ui.advanceTime(.5f);drawnRounded.clear();
        screen.render(ui,size.x,size.y,-10000,-10000);
        require(drawnBorder(drawnShortcutSlot)==UiTheme::BORDER,
                "mouse movement releases stale controller inventory focus");
        recordUi=false;
    }

    {
        ClientSettings settings;Localization localization;
        int opened=0;
        MenuCallbacks callbacks;
        callbacks.onOpenWorld=[&](const std::string&){++opened;};
        WorldSummary supported; supported.id="supported";supported.compatible=true;
        WorldSummary incompatible; incompatible.id="old";incompatible.compatible=false;
        MainMenu menu(callbacks,{supported,incompatible},settings,localization,nullptr);
        menu.onKeyPress(Key::Enter); // World list, focus first card.
        menu.onKeyPress(Key::Down);menu.onKeyPress(Key::Enter); // Select incompatible.
        menu.onKeyPress(Key::Enter);
        require(opened==0,"incompatible world selection cannot activate loading");
        menu.onKeyPress(Key::Up);menu.onKeyPress(Key::Enter); // Select supported.
        menu.onKeyPress(Key::Enter);
        require(opened==1,"selected world remains focused after rebuilding its card");
    }

    for (const auto language : languagesByEnglishName()) {
        ClientSettings settings; settings.language=Language::English;
        Localization localization; localization.setLanguage(settings.language);
        int changes=0;
        MenuCallbacks callbacks; callbacks.onSettingsChanged=[&]{++changes;};
        MainMenu menu(callbacks,{},settings,localization,nullptr);
        menu.onKeyPress(Key::Down);menu.onKeyPress(Key::Down);menu.onKeyPress(Key::Enter);
        require(settings.language==Language::English && changes==0,
                "opening language submenu does not cycle or save language");
        const auto& languages=languagesByEnglishName();
        const int target=static_cast<int>(std::find(languages.begin(),languages.end(),language)-languages.begin());
        for (int i=2;i<target;++i) menu.onKeyPress(Key::Down);
        for (int i=2;i>target;--i) menu.onKeyPress(Key::Up);
        menu.onKeyPress(Key::Enter);
        require(settings.language==language && localization.language()==language &&
                    changes==(language==Language::English?0:1),
                "language submenu directly selects and persists every supported language");
        menu.onKeyPress(Key::Enter);
        require(changes==(language==Language::English?0:1),
                "reselecting current language keeps focus and avoids redundant saving");
        menu.onKeyPress(Key::Escape);
        UIRenderer ui;recordUi=true;drawnLabels.clear();menu.render(ui,960,600);
        require(drawnLabels.count("MINECRAFTC")==1,"Escape returns from language page to home");
        recordUi=false;
    }

    for (const auto size : {glm::ivec2(960,600),glm::ivec2(320,640),glm::ivec2(640,240)}) {
        ClientSettings settings;settings.language=Language::English;
        Localization localization;localization.setLanguage(settings.language);
        int changes=0;
        MenuCallbacks callbacks;callbacks.onSettingsChanged=[&]{++changes;};
        MainMenu menu(callbacks,{},settings,localization,nullptr);
        menu.onKeyPress(Key::Down);menu.onKeyPress(Key::Down);menu.onKeyPress(Key::Enter);
        UIRenderer ui;recordUi=true;
        for (int i=0;i<8;++i) menu.onScroll(-1);
        drawnLabels.clear();menu.render(ui,size.x,size.y);
        // Keyboard focus takes priority on the first render; wheel scroll follows it.
        for (int i=0;i<11;++i) menu.onScroll(-1);
        drawnLabels.clear();menu.render(ui,size.x,size.y);
        require(drawnLabels.count("Español")==1,"wheel scrolling reveals last language on compact canvases");
        auto point=drawnLabels.at("Español")+glm::vec2(1,1);
        menu.onMouseButton(MouseButton::Left,ButtonAction::Press,point.x,point.y);
        menu.onMouseButton(MouseButton::Left,ButtonAction::Release,point.x,point.y);
        require(settings.language==Language::Spanish && changes==1,"pointer directly selects visible language");
        menu.onKeyPress(Key::Down);drawnLabels.clear();menu.render(ui,size.x,size.y);
        require(drawnLabels.count("common.back")==1,"keyboard focus scrolls Back into view");
        point=drawnLabels.at("common.back")+glm::vec2(1,1);
        menu.onMouseButton(MouseButton::Left,ButtonAction::Press,point.x,point.y);
        menu.onMouseButton(MouseButton::Left,ButtonAction::Release,point.x,point.y);
        drawnLabels.clear();menu.render(ui,size.x,size.y);
        require(drawnLabels.count("MINECRAFTC")==1,"language Back button returns home");
        recordUi=false;
    }

    {
        ClientSettings menuSettings;
        Localization menuLocalization;
        std::string openedUrl;
        int openedUrlCount = 0;
        MenuCallbacks callbacks;
        callbacks.onOpenUrl = [&](const std::string& url) {
            openedUrl = url;
            ++openedUrlCount;
        };
        MainMenu menu(callbacks, {}, menuSettings, menuLocalization, nullptr);
        for (int i = 0; i < 4; ++i) menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Enter); // Home -> About.
        menu.onKeyPress(Key::Enter); // Open the project link.
        require(openedUrl == "https://github.com/SafeFain/MinecraftC" &&
                    openedUrlCount == 1,
                "About menu opens the canonical project URL");
        // Follow the public keyboard/scroll paths across the complete credits.
        const std::vector<std::string> repositories = {
            "https://github.com/libsdl-org/SDL",
            "https://github.com/g-truc/glm",
            "https://github.com/KhronosGroup/Vulkan-Headers",
            "https://github.com/KhronosGroup/Vulkan-Loader",
            "https://github.com/KhronosGroup/MoltenVK",
            "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator",
            "https://github.com/Auburn/FastNoiseLite",
            "https://github.com/jkuhlmann/cgltf",
            "https://github.com/nlohmann/json",
            "https://github.com/nothings/stb",
            "https://github.com/notofonts/noto-cjk",
            "https://github.com/notofonts/arabic",
        };
        menu.onKeyPress(Key::Left); // Clamp at the first page.
        menu.onScroll(0.0); // No movement must preserve the page.
        for (size_t page = 0; page < 3; ++page) {
            for (size_t row = 0; row < 4; ++row) {
                menu.onKeyPress(Key::Down);
                menu.onKeyPress(Key::Enter);
                require(openedUrl == repositories[page * 4 + row],
                        "Every About credit opens its upstream repository");
            }
            if (page == 0) {
                menu.onKeyPress(Key::Down); // Previous.
                menu.onKeyPress(Key::Down); // Next.
                menu.onKeyPress(Key::Enter);
            } else if (page == 1) {
                menu.onScroll(-1.0);
            }
        }
        menu.onKeyPress(Key::Right); // Clamp at the last page.
        menu.onKeyPress(Key::Enter);
        require(openedUrl == repositories.back(),
                "Last-page navigation preserves the selected credit");
        menu.onKeyPress(Key::Left);
        menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Enter);
        require(openedUrl == repositories[4], "Left returns to the preceding credits page");
        menu.onScroll(1.0);
        menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Enter);
        require(openedUrl == repositories[0], "Scroll up returns to the first credits page");
        const int countBeforeEscape = openedUrlCount;
        menu.onKeyPress(Key::Escape); // About -> Home.
        menu.onKeyPress(Key::Enter); // Home -> world list.
        require(openedUrlCount == countBeforeEscape,
                "Escape returns from About without reopening the URL");
    }

    {
        // Exercise the create-screen selector through the public keyboard
        // path so the menu callback carries the selected terrain preset.
        ClientSettings menuSettings;
        Localization menuLocalization;
        WorldType selectedType = WorldType::Normal;
        bool created = false;
        MenuCallbacks callbacks;
        callbacks.onCreateWorld =
            [&](const std::string&, const std::string&, GameMode,
                WorldType type, bool) {
                selectedType = type;
                created = true;
            };
        MainMenu menu(callbacks, {}, menuSettings, menuLocalization, nullptr);
        menu.onKeyPress(Key::Enter); // Home -> world list.
        menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Enter); // World list -> create screen.
        menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Enter); // Toggle Normal -> Superflat.
        for (int i = 0; i < 5; ++i) menu.onKeyPress(Key::Down);
        menu.onKeyPress(Key::Enter); // Confirm.
        require(created && selectedType == WorldType::Superflat,
                "create menu forwards the selected superflat type");
    }

    {
        Harness harness(root, *window);
        require(harness.flow.state() == GameState::MainMenu,
                "fresh application is in the main menu");

        const int oldRenderDistance = Config::RENDER_DISTANCE;
        Config::RENDER_DISTANCE = 2;
        const std::string id = harness.session.createWorld(
            "Flow Test", 42, GameMode::Survival, Difficulty::Normal, true);
        require(!id.empty(), "world creation returns an id");

        // New-world start enters the loading state through the flow.
        harness.flow.startGame(id, true);
        require(harness.flow.state() == GameState::LoadingWorld,
                "startGame enters the loading state");
        require(harness.audio.musicMode() == AudioMusicMode::Overworld,
                "new Overworld loading selects only Overworld music");
        require(harness.ui.hotbar.inventory() == &harness.session.playerState().inventory(),
                "hotbar is bound to the player inventory");

        // The loading gate completes and hands control to Playing.
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "loading gate completes with a small render distance");
        harness.flow.completeLoading();
        require(harness.flow.state() == GameState::Playing,
                "completeLoading enters the playing state");

        // Exercise the real render-target loading gate across both dimension
        // resets. In particular, a budgeted final lighting handoff must keep
        // every unprocessed completion queued until all Heaven chunks can be
        // meshed instead of stalling partway through preparation.
        Config::RENDER_DISTANCE = 4;
        require(harness.session.switchDimension(
                    DimensionId::Heaven, harness.clock.now()),
                "playing world switches into heaven");
        harness.flow.beginDimensionLoading();
        require(harness.audio.musicMode() == AudioMusicMode::Heaven,
                "Heaven loading switches to its exclusive music");
        require(harness.session.playerState().getPosition() ==
                    harness.session.worldState().findSafeSpawn(),
                "heaven switch centers its first stream on the island spawn");
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "overworld-to-heaven loading gate completes");
        require(harness.session.activeDimension() == DimensionId::Heaven,
                "heaven is active after its loading gate");
        require(harness.session.switchDimension(
                    DimensionId::Overworld, harness.clock.now()),
                "heaven switches back to overworld");
        harness.flow.beginDimensionLoading();
        require(harness.audio.musicMode() == AudioMusicMode::Overworld,
                "returning from Heaven restores Overworld-only music");
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "heaven-to-overworld loading gate completes");
        Config::RENDER_DISTANCE = 2;

        // Pause/resume round trip through the flow.
        harness.flow.pause();
        require(harness.flow.state() == GameState::Paused,
                "pause enters the paused state");
        require(harness.ui.activeMenu != nullptr,
                "pause opens the pause menu");
        harness.flow.resume();
        require(harness.flow.state() == GameState::Playing &&
                    !harness.ui.activeMenu,
                "resume returns to playing without a menu");

        // Inventory and command console transitions.
        harness.flow.openInventory();
        require(harness.ui.inventoryOpen,
                "openInventory shows the inventory");
        harness.flow.closeInventory();
        require(!harness.ui.inventoryOpen,
                "closeInventory hides the inventory");
        harness.flow.openCommandInput();
        require(harness.ui.commandOpen,
                "openCommandInput opens the console");
        harness.flow.closeCommandInput();
        require(!harness.ui.commandOpen,
                "closeCommandInput closes the console");

        // Command execution routes into the session and updates UI access.
        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/gamemode 1");
        harness.flow.executeCommand();
        require(!harness.ui.commandOpen,
                "executing a command closes the console");
        require(harness.session.playerState().gameMode() == GameMode::Creative,
                "gamemode command switches the player to creative");
        require(harness.ui.survivalInventory.creativeAccess(),
                "creative access follows the gamemode command");
        require(harness.ui.hotbar.inventory() ==
                    &harness.session.playerState().inventory(),
                "hotbar remains bound after the gamemode command");

        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/tp 144 200 0");
        harness.flow.executeCommand();
        require(harness.flow.state() == GameState::LoadingWorld,
                "teleport did not wait for the new render target");
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "teleport loading did not complete with LOD coverage");
        harness.flow.completeLoading();

        // Command errors surface messages without changing state.
        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/bogus");
        harness.flow.executeCommand();
        require(!harness.ui.chatHistory.empty(),
                "an unknown command reports a message");

        // Respawn from the death screen.
        GameSessionTestAccess::markPlayerDead(harness.session);
        harness.flow.respawnPlayer();
        require(!harness.session.isPlayerDead(),
                "respawn clears the dead state");

        // Explicit save writes the world metadata to disk.
        harness.flow.saveCurrentWorld();
        require(std::filesystem::exists(
                    root / "saves" / id / "level.bin"),
                "saveCurrentWorld persists the world");

        // Back to menu saves, leaves the world, and returns to MainMenu.
        harness.flow.backToMainMenu();
        require(harness.flow.state() == GameState::MainMenu,
                "backToMainMenu returns to the main menu");
        require(harness.ui.activeMenu != nullptr,
                "backToMainMenu shows the main menu");
        require(!harness.session.hasWorldStore(),
                "backToMainMenu leaves the world");

        Config::RENDER_DISTANCE = oldRenderDistance;
    }

    {
        // Router scenarios on a fresh world.
        const auto routerRoot = std::filesystem::temp_directory_path() /
                                "minecraftc-application-router-test";
        std::filesystem::remove_all(routerRoot);
        std::filesystem::create_directories(routerRoot);
        Harness harness(routerRoot, *window);
        harness.wireCallbacks();
        harness.bindKeys();

        const std::string id = harness.session.createWorld(
            "Router Test", 7, GameMode::Survival, Difficulty::Normal, true);
        require(!id.empty(), "router world creation returns an id");
        harness.play(id);
        require(harness.flow.state() == GameState::Playing,
                "router harness reaches the playing state");

        // ESC pauses through the router; the pause menu's resume callback
        // returns to playing.
        harness.router.handleKeyEvent(Key::Escape, 0, ButtonAction::Press, 0);
        require(harness.flow.state() == GameState::Paused &&
                    harness.ui.activeMenu != nullptr,
                "ESC pauses the game through the router");
        harness.router.handleKeyEvent(Key::Escape, 0, ButtonAction::Press, 0);
        require(harness.flow.state() == GameState::Playing,
                "ESC in the pause menu resumes through the menu callback");

        // The inventory key toggles the inventory; ESC closes it first.
        harness.router.handleKeyEvent(Key::E, 0, ButtonAction::Press, 0);
        require(harness.ui.inventoryOpen,
                "the inventory key opens the inventory");
        harness.router.handleKeyEvent(Key::Escape, 0, ButtonAction::Press, 0);
        require(!harness.ui.inventoryOpen && harness.flow.state() == GameState::Playing,
                "ESC closes the inventory without pausing");

        // The command key opens the console; text events edit the buffer and
        // Enter executes the command.
        harness.router.handleKeyEvent(Key::C, 0, ButtonAction::Press, 0);
        require(harness.ui.commandOpen,
                "the command key opens the console");
        harness.router.handleTextEvent("help");
        require(harness.ui.commandInput.text() == "help",
                "text events insert into the command buffer");
        harness.router.handleKeyEvent(Key::Enter, 0, ButtonAction::Press, 0);
        require(!harness.ui.commandOpen,
                "Enter executes and closes the console");
        require(!harness.ui.chatHistory.empty(),
                "command execution reports a message");

        harness.router.handleKeyEvent(Key::Slash, 0, ButtonAction::Press, 0);
        require(harness.ui.commandOpen && harness.ui.commandInput.text() == "/",
                "the direct-command binding opens a slash-prefilled console");
        harness.flow.closeCommandInput();

        // Tab follows the command tree and cycles candidates in both
        // directions, matching the same completion path used by touch UI.
        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/locate ");
        harness.router.handleKeyEvent(Key::Tab, 0, ButtonAction::Press, 0);
        require(harness.ui.commandInput.text() == "/locate biome",
                "Tab selects the first locate subcommand");
        harness.router.handleKeyEvent(Key::Tab, 0, ButtonAction::Press, 0);
        require(harness.ui.commandInput.text() == "/locate structure",
                "repeated Tab cycles to the next locate subcommand");
        harness.router.handleKeyEvent(
            Key::Tab, 0, ButtonAction::Press, KeyModifier::Shift);
        require(harness.ui.commandInput.text() == "/locate biome",
                "Shift+Tab cycles command completion backwards");

        harness.settings.controlMode = ControlMode::Touch;
        harness.ui.commandInput.setText("/locate st");
        harness.ui.resetCommandCompletion();
        int sdlWindowCount = 0;
        SDL_Window** sdlWindows = SDL_GetWindows(&sdlWindowCount);
        require(sdlWindows && sdlWindowCount == 1,
                "chat IME test has one native window");
        SDL_Window* sdlWindow = sdlWindows[0];
        SDL_free(sdlWindows);
        require(!SDL_TextInputActive(sdlWindow),
                "chat input has not started before its first frame");
        harness.router.beginFrame(harness.clock.now(), true);
        SDL_Rect imeArea{};
        require(SDL_GetTextInputArea(sdlWindow, &imeArea, nullptr) &&
                SDL_TextInputActive(sdlWindow),
                "the first chat frame supplies a native input area and starts text input");
        const WindowSafeArea safe = window->safeArea();
        const int uiWidth = std::max(1, safe.width / harness.ui.guiScale);
        const int uiHeight = std::max(1, safe.height / harness.ui.guiScale);
        const TouchRect tab = touchCommandTabRect(uiWidth, uiHeight);
        const double scaleX = static_cast<double>(window->width()) /
                              std::max(1, window->windowWidth());
        const double scaleY = static_cast<double>(window->height()) /
                              std::max(1, window->windowHeight());
        const double touchX =
            (safe.x + (tab.x + tab.w * 0.5) * harness.ui.guiScale) / scaleX;
        const double touchY =
            (window->height() - safe.y -
             (tab.y + tab.h * 0.5) * harness.ui.guiScale) / scaleY;
        require(touchX >= imeArea.x && touchX <= imeArea.x + imeArea.w &&
                touchY >= imeArea.y && touchY <= imeArea.y + imeArea.h,
                "native keyboard avoidance includes the virtual Tab button");
        require(imeArea.y + imeArea.h > window->windowHeight() / 2,
                "native keyboard avoidance targets the bottom chat field");
        harness.settings.guiScale = 2;
        harness.router.beginFrame(harness.clock.now(), true);
        SDL_Rect scaledImeArea{};
        require(SDL_GetTextInputArea(sdlWindow, &scaledImeArea, nullptr) &&
                harness.ui.guiScale == 2 && scaledImeArea.h > imeArea.h &&
                scaledImeArea.y < imeArea.y,
                "GUI scale changes refresh the native chat input bounds");
        harness.settings.guiScale = 0;
        harness.router.beginFrame(harness.clock.now(), true);
        const TouchContactId tabContact{2, 1};
        harness.router.handleTouch(
            {tabContact, TouchPhase::Begin, touchX, touchY});
        require(harness.ui.commandInput.text() == "/locate structure",
                "the virtual mobile Tab uses command completion");
        harness.router.handleTouch(
            {tabContact, TouchPhase::End, touchX, touchY});
        harness.settings.controlMode = ControlMode::Auto;
        harness.flow.closeCommandInput();
        harness.router.beginFrame(harness.clock.now(), false);
        require(SDL_GetTextInputArea(sdlWindow, &imeArea, nullptr) &&
                imeArea.w == 0 && imeArea.h == 0 &&
                !SDL_TextInputActive(sdlWindow),
                "closing chat clears the native input bounds and stops text input");

        // Touch input in the gameplay region activates the touch HUD and
        // routes the contact as gameplay.
        harness.settings.controlMode = ControlMode::Touch;
        const TouchContactId contact{1, 1};
        harness.router.handleTouch({contact, TouchPhase::Begin, 200.0, 200.0});
        require(harness.inputs.touchHudVisible,
                "a gameplay touch shows the touch HUD");
        require(harness.inputs.touchGameplay.count(contact) == 1,
                "the contact is tracked as gameplay");
        harness.router.handleTouch({contact, TouchPhase::End, 200.0, 200.0});
        require(harness.inputs.touchGameplay.count(contact) == 0,
                "the contact is released on touch end");
        harness.settings.controlMode = ControlMode::Auto;

        // A dead player respawns through Enter/Space routing.
        GameSessionTestAccess::markPlayerDead(harness.session);
        harness.router.handleKeyEvent(Key::Enter, 0, ButtonAction::Press, 0);
        require(!harness.session.isPlayerDead(),
                "Enter on the death screen respawns the player");

        // The drop-item key on a filled hotbar spawns a dropped item entity.
        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/gamemode 1");
        harness.flow.executeCommand();
        harness.flow.giveCreativeItem(ItemId::STONE);
        harness.router.handleKeyEvent(Key::Q, 0, ButtonAction::Press, 0);
        size_t itemEntities = 0;
        for (const Entity& entity : harness.session.entityState().entities())
            if (entity.type == EntityType::Item && !entity.item.empty())
                ++itemEntities;
        require(itemEntities == 1,
                "the drop-item key spawns one dropped item");
        require(harness.session.playerState().inventory().slot(0).count == 63,
                "plain drop removes one item from the selected stack");

        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        require(harness.session.playerState().inventory().slot(0).empty() &&
                    harness.session.playerState().inventory().offhand().count == 63,
                "the swap-offhand binding exchanges the selected and offhand stacks");
        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        harness.router.handleKeyEvent(
            Key::Q, 0, ButtonAction::Press, KeyModifier::Control);
        itemEntities = 0;
        for (const Entity& entity : harness.session.entityState().entities())
            if (entity.type == EntityType::Item && !entity.item.empty())
                ++itemEntities;
        require(itemEntities == 2 &&
                    harness.session.playerState().inventory().slot(0).empty(),
                "Ctrl plus drop removes and spawns the entire selected stack");

        GameSessionTestAccess::setPlayerPosition(harness.session, {0.5, 200.0, 0.5});
        GameSessionTestAccess::setBlock(harness.session, 0, 201, 2, BlockId::STONE);
        harness.flow.pickBlock();
        require(harness.session.playerState().inventory().slot(0).id == ItemId::STONE &&
                    harness.session.playerState().inventory().slot(0).count == 64,
                "creative pick block supplies the targeted block as a full stack");
        GameSessionTestAccess::setBlock(harness.session, 0, 201, 2, BlockId::AIR);

        // Java inventory shortcuts act on the hovered slot: number keys swap
        // with that hotbar slot, F swaps with offhand, and Q/Ctrl+Q drops.
        harness.flow.giveCreativeItem(ItemId::STONE, 0);
        harness.session.inventory().slot(9) = {ItemId::DIRT, 8, 0};
        harness.flow.openInventory();
        harness.flow.openPlayerInventoryView();
        UIRenderer inertUi;
        Localization inertLocalization;
        inertUi.setLocalization(inertLocalization);
        for (auto dimensions:{glm::ivec2(640,480),glm::ivec2(320,640),glm::ivec2(640,240)}) {
            harness.session.inventory().slot(0)={ItemId::STONE,64,0};
            harness.session.inventory().slot(9)={ItemId::DIRT,8,0};
        // Find the actual rendered item center, independent of theme/layout.
        drawnShortcutSlot={-1,-1};
        harness.ui.survivalInventory.render(inertUi,dimensions.x,dimensions.y,0,0);
        require(drawnShortcutSlot.x>=0&&drawnShortcutSlot.y>=0,
                "inventory shortcut target is visibly rendered");
        harness.ui.survivalInventory.onMouseMove(static_cast<int>(drawnShortcutSlot.x),
                                               static_cast<int>(drawnShortcutSlot.y));
        harness.router.handleKeyEvent(Key::Num1, 0, ButtonAction::Press, 0);
        require(harness.session.playerState().inventory().slot(0).id == ItemId::DIRT &&
                    harness.session.playerState().inventory().slot(9).id == ItemId::STONE,
                "an inventory number shortcut swaps the hovered stack with its hotbar slot");
        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        require(harness.session.playerState().inventory().slot(9).empty() &&
                    harness.session.playerState().inventory().offhand().id == ItemId::STONE,
                "inventory F swaps the hovered stack with the offhand slot");
        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        harness.router.handleKeyEvent(
            Key::Q, 0, ButtonAction::Press, KeyModifier::Control);
        require(harness.session.playerState().inventory().slot(9).empty(),
                "inventory Ctrl+Q drops the complete hovered stack");
        }
        harness.flow.closeInventory();

        // Leave the store attached here.  Harness destruction covers the
        // real application shutdown order while streaming cache writes may
        // still be draining.
    }

    std::cout << "PASS: application flow state transitions\n";
    return 0;
}
