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
#include "debug/Log.h"
#include "Config.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// UIRenderer is a graphics-backed facade; the flow tests never render, so these
// inert definitions keep the target free of the Vulkan backend (same pattern
// as InputRoutingTests).
UIRenderer::~UIRenderer() = default;
void UIRenderer::beginUIFrame(int, int) {}
void UIRenderer::setCanvas(float, float, float, float) {}
void UIRenderer::endUIFrame() {}
void UIRenderer::drawRect(float, float, float, float, const glm::vec4&) {}
void UIRenderer::drawBlockIcon(float, float, float, float, BlockId) {}
void UIRenderer::drawItemIcon(float, float, float, float, const ItemStack&) {}
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

// Inert renderer for the loading gate: mesh uploads/releases are no-ops.
class StubRenderer final : public IGameRenderer {
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
    void finishScene(const PostProcessState&) override {}
    void setEnvironment(const RenderEnvironment&, const glm::vec3&) override {}
    void renderSky(const RenderEnvironment&, const glm::mat4&,
                   const glm::vec3&, bool) override {}
    void renderChunk(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                     bool) override {}
    void renderLod(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                   const glm::vec2&,
                   float, float, bool) override {}
    void renderChunkShadows(ShadowQuality, const glm::mat4&, const glm::mat4&,
                            const glm::dvec3&,
                            const std::vector<ShadowChunkSubmission>&) override {
    }
    void uploadChunkMesh(ChunkMesh&) override {}
    void releaseChunkMesh(ChunkMesh&) override {}
    void beginTranslucent() override {}
    void endTranslucent() override {}
    void bindBlockShader() const override {}
    void unbindBlockShader() const override {}
    void renderWireframe(const glm::vec3&, const glm::vec3&,
                         const glm::mat4&) override {}
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

// Drives the loading gate the same way the application does until the world
// render target is fully loaded or the budget is exhausted.
bool loadWorld(GameSession& session, StubRenderer& stub, RuntimeClock& clock) {
    for (int i = 0; i < 2000; ++i) {
        if (session.advanceLoading(&stub, clock.now())) return true;
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
          ui(session.player, clipboard),
          flow(session, ui, scene, audio, window, clock, settings, clipboard),
          router(window, ui, session, inputs, scene, settings, flow, clock) {
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

int main() {
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
        const std::string id = harness.session.worldCatalog.create(
            "Flow Test", 42, GameMode::Survival, Difficulty::Normal, true);
        require(!id.empty(), "world creation returns an id");

        // New-world start enters the loading state through the flow.
        harness.flow.startGame(id, true);
        require(harness.flow.state() == GameState::LoadingWorld,
                "startGame enters the loading state");
        require(harness.ui.hotbar.inventory() == &harness.session.player.inventory(),
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
        require(harness.session.player.getPosition() ==
                    harness.session.world.findSafeSpawn(),
                "heaven switch centers its first stream on the island spawn");
        require(loadWorld(harness.session, harness.stub, harness.clock),
                "overworld-to-heaven loading gate completes");
        require(harness.session.activeDimension() == DimensionId::Heaven,
                "heaven is active after its loading gate");
        require(harness.session.switchDimension(
                    DimensionId::Overworld, harness.clock.now()),
                "heaven switches back to overworld");
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
        require(harness.session.player.gameMode() == GameMode::Creative,
                "gamemode command switches the player to creative");
        require(harness.ui.survivalInventory.creativeAccess(),
                "creative access follows the gamemode command");
        require(harness.ui.hotbar.inventory() ==
                    &harness.session.player.inventory(),
                "hotbar remains bound after the gamemode command");

        // Command errors surface messages without changing state.
        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/bogus");
        harness.flow.executeCommand();
        require(!harness.ui.chatHistory.empty(),
                "an unknown command reports a message");

        // Respawn from the death screen.
        harness.session.playerDead = true;
        harness.flow.respawnPlayer();
        require(!harness.session.playerDead,
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
        require(harness.session.saveStore == nullptr,
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

        const std::string id = harness.session.worldCatalog.create(
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
        const TouchContactId tabContact{2, 1};
        harness.router.handleTouch(
            {tabContact, TouchPhase::Begin, touchX, touchY});
        require(harness.ui.commandInput.text() == "/locate structure",
                "the virtual mobile Tab uses command completion");
        harness.router.handleTouch(
            {tabContact, TouchPhase::End, touchX, touchY});
        harness.settings.controlMode = ControlMode::Auto;
        harness.flow.closeCommandInput();

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
        harness.session.playerDead = true;
        harness.router.handleKeyEvent(Key::Enter, 0, ButtonAction::Press, 0);
        require(!harness.session.playerDead,
                "Enter on the death screen respawns the player");

        // The drop-item key on a filled hotbar spawns a dropped item entity.
        harness.flow.openCommandInput();
        harness.ui.commandInput.setText("/gamemode 1");
        harness.flow.executeCommand();
        harness.flow.giveCreativeItem(ItemId::STONE);
        harness.router.handleKeyEvent(Key::Q, 0, ButtonAction::Press, 0);
        size_t itemEntities = 0;
        for (const Entity& entity : harness.session.entities.entities())
            if (entity.type == EntityType::Item && !entity.item.empty())
                ++itemEntities;
        require(itemEntities == 1,
                "the drop-item key spawns one dropped item");
        require(harness.session.player.inventory().slot(0).count == 63,
                "plain drop removes one item from the selected stack");

        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        require(harness.session.player.inventory().slot(0).empty() &&
                    harness.session.player.inventory().offhand().count == 63,
                "the swap-offhand binding exchanges the selected and offhand stacks");
        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        harness.router.handleKeyEvent(
            Key::Q, 0, ButtonAction::Press, KeyModifier::Control);
        itemEntities = 0;
        for (const Entity& entity : harness.session.entities.entities())
            if (entity.type == EntityType::Item && !entity.item.empty())
                ++itemEntities;
        require(itemEntities == 2 &&
                    harness.session.player.inventory().slot(0).empty(),
                "Ctrl plus drop removes and spawns the entire selected stack");

        harness.session.player.setPosition({0.5, 200.0, 0.5});
        harness.session.world.setBlock(0, 201, 2, BlockId::STONE);
        harness.flow.pickBlock();
        require(harness.session.player.inventory().slot(0).id == ItemId::STONE &&
                    harness.session.player.inventory().slot(0).count == 64,
                "creative pick block supplies the targeted block as a full stack");
        harness.session.world.setBlock(0, 201, 2, BlockId::AIR);

        // Java inventory shortcuts act on the hovered slot: number keys swap
        // with that hotbar slot, F swaps with offhand, and Q/Ctrl+Q drops.
        harness.flow.giveCreativeItem(ItemId::STONE, 0);
        harness.session.player.inventory().slot(9) = {ItemId::DIRT, 8, 0};
        harness.flow.openInventory();
        harness.flow.openPlayerInventoryView();
        UIRenderer inertUi;
        Localization inertLocalization;
        inertUi.setLocalization(inertLocalization);
        harness.ui.survivalInventory.render(inertUi, 640, 480, 128, 257);
        harness.ui.survivalInventory.onMouseMove(128, 257);
        harness.router.handleKeyEvent(Key::Num1, 0, ButtonAction::Press, 0);
        require(harness.session.player.inventory().slot(0).id == ItemId::DIRT &&
                    harness.session.player.inventory().slot(9).id == ItemId::STONE,
                "an inventory number shortcut swaps the hovered stack with its hotbar slot");
        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        require(harness.session.player.inventory().slot(9).empty() &&
                    harness.session.player.inventory().offhand().id == ItemId::STONE,
                "inventory F swaps the hovered stack with the offhand slot");
        harness.router.handleKeyEvent(Key::F, 0, ButtonAction::Press, 0);
        harness.router.handleKeyEvent(
            Key::Q, 0, ButtonAction::Press, KeyModifier::Control);
        require(harness.session.player.inventory().slot(9).empty(),
                "inventory Ctrl+Q drops the complete hovered stack");
        harness.flow.closeInventory();

        // Leave the store attached here.  Harness destruction covers the
        // real application shutdown order while streaming cache writes may
        // still be draining.
    }

    std::cout << "PASS: application flow state transitions\n";
    return 0;
}
