#include "app/Application.h"
#include "app/ApplicationInputController.h"
#include "app/ApplicationInputRouter.h"
#include "app/GameFlowController.h"
#include "app/GameScenePresenter.h"
#include "app/GameSession.h"
#include "app/GameUiController.h"
#include "core/Window.h"
#include "core/ApplicationHost.h"
#include "core/Input.h"
#include "core/Platform.h"
#include "core/RuntimeClock.h"
#include "core/TextEditBuffer.h"
#include "platform/sdl/SdlClipboard.h"
#include "core/AssetStore.h"
#if defined(MINECRAFTC_ENABLE_OPENGL)
#include "renderer/Renderer.h"
#endif
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "renderer/RenderEnvironment.h"
#include "renderer/ParticleSystem.h"
#include "renderer/CameraEffects.h"
#include "renderer/HeldItemRenderer.h"
#include "Config.h"
#include "world/World.h"
#include "player/Player.h"
#include "player/PlayerVisual.h"
#include "player/PlayerRenderer.h"
#include "threading/ThreadPool.h"
#include "ui/UIRenderer.h"
#include "ui/Menu.h"
#include "ui/SettingsMenu.h"
#include "ui/Hotbar.h"
#include "ui/Inventory.h"
#include "ui/SurvivalInventory.h"
#include "ui/ContainerScreen.h"
#include "ui/TouchControls.h"
#include "debug/Log.h"
#include "debug/CrashHandler.h"
#include "debug/Profiler.h"
#include "game/SaveStore.h"
#include "game/WorldCatalog.h"
#include "game/Command.h"
#include "game/Weather.h"
#include "game/SurvivalRules.h"
#include "game/ClientSettings.h"
#include "game/Localization.h"
#include "game/SurvivalSession.h"
#include "game/InventoryInteraction.h"
#include "game/Utf8.h"
#include "game/TextWrap.h"
#include "world/WorldGenContext.h"
#include "entity/EntityManager.h"
#include "entity/ProjectileLogic.h"
#include "audio/AudioSystem.h"
#include "renderer/ChunkRenderScene.h"
#include "renderer/TexturedCubeScene.h"
#if defined(MINECRAFTC_ENABLE_VULKAN)
#include "renderer/backend/vulkan/VulkanRenderer.h"
#endif
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <deque>
#include <random>
#include <iostream>
#include <limits>
#include <optional>
#include <iomanip>
#include <unordered_map>

namespace {
std::unique_ptr<IGameRenderer> createGameRenderer(GraphicsApi api) {
    if (api == GraphicsApi::Vulkan) {
#if defined(MINECRAFTC_ENABLE_VULKAN)
        return std::make_unique<VulkanRenderer>();
#else
        throw std::runtime_error("Vulkan support is not enabled in this build");
#endif
    }
#if defined(MINECRAFTC_ENABLE_OPENGL)
    return std::make_unique<Renderer>();
#else
    throw std::runtime_error("OpenGL support is not enabled in this build");
#endif
}
}

class Application final : public ApplicationHost {
public:
    explicit Application(RuntimePaths paths,
                         GraphicsApi graphicsApi = GraphicsApi::OpenGL33)
        : m_paths(std::move(paths)),
          m_assets(m_paths.assetRoot),
          m_window(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, "MinecraftC",
                   graphicsApi == GraphicsApi::Vulkan ? 0 : Config::MSAA_SAMPLES,
                   graphicsApi),
          m_renderer(createGameRenderer(graphicsApi)),
          m_graphicsApi(graphicsApi),
          m_session(m_paths.savesDirectory()),
          m_ui(m_session.player, m_clipboard),
          m_flow(m_session, m_ui, m_scene, m_audio, m_window, m_runtimeClock,
                 m_clientSettings, m_clipboard),
          m_router(m_window, m_ui, m_session, m_inputs, m_scene,
                   m_clientSettings, m_flow, m_runtimeClock)
    {}

    void start() { initialize(); }
    bool iterate() override {
        if (m_graphicsResetPending) restoreGraphics();
        if (m_backgrounded) { RuntimeClock::sleepMilliseconds(100); return m_running; }
        return runFrame();
    }
    void event(ApplicationEvent event, const void* nativeEvent) override {
        if (nativeEvent) m_window.handleEvent(nativeEvent);
        switch (event) {
            case ApplicationEvent::EnterBackground:
                m_renderer->suspendPresentation();
                m_backgrounded = true;
                m_inputs.touchControls.cancelAll();
                m_flow.saveCurrentWorld();
                break;
            case ApplicationEvent::EnterForeground:
                m_renderer->resumePresentation();
                m_backgrounded = false;
                m_lastFrameTick = m_runtimeClock.now();
                break;
            case ApplicationEvent::GraphicsReset:
                m_graphicsResetPending = true;
                break;
            case ApplicationEvent::LowMemory:
                LOG_WARN("The platform reported low memory");
                break;
            case ApplicationEvent::Terminating:
                m_flow.saveCurrentWorld();
                m_savedForTermination = true;
                m_running = false;
                break;
            case ApplicationEvent::Input:
                break;
        }
    }
    void shutdown() override { if(!m_cleaned){m_cleaned=true;cleanup();} }

private:
    RuntimePaths m_paths;
    AssetStore m_assets;
    platform::sdl::SdlClipboard m_clipboard;
    Window      m_window;
    std::unique_ptr<IGameRenderer> m_renderer;
    GraphicsApi m_graphicsApi = GraphicsApi::OpenGL33;
    GameSession m_session;
    GameScenePresenter m_scene;
    GameUiController m_ui;
    ApplicationInputController m_inputs;
    bool        m_running = true;
    bool        m_cleaned = false;
    bool        m_backgrounded = false;
    bool        m_graphicsResetPending = false;
    bool        m_savedForTermination = false;

    // ── UI / State ────────────────────────────────────────────────────
    Debug::FrameTimer     m_frameTimer{600};

    RuntimeClock m_runtimeClock;
    RuntimeClock::Tick m_lastFrameTick = 0;
    AudioSystem m_audio;
    ClientSettings m_clientSettings;
    GameSession::Feedback m_sessionFeedback;
    GameFlowController m_flow;
    ApplicationInputRouter m_router;

    void initialize() {
        // ── Debug infrastructure ────────────────────────────────────────
        std::error_code directoryError;
        std::filesystem::create_directories(m_paths.dataRoot, directoryError);
        if (directoryError)
            throw std::runtime_error("Cannot create user data directory: " +
                                     directoryError.message());
        Debug::Log::init(Debug::LogLevel::Trace, Config::LogConfig::FILE_OUTPUT,
                         m_paths.logFile());
        Debug::installCrashHandlers();

        m_clientSettings = ClientSettings::load(m_paths.settingsFile());
        m_ui.localization.load(m_paths.assetRoot);
        m_ui.localization.setLanguage(m_clientSettings.language);
        applyClientSettings(false);

        const GraphicsCapabilities graphics = m_window.graphicsCapabilities();
        m_renderer->initialize(m_window, graphics, m_paths.assetRoot);
        m_renderer->setVisualQuality(m_clientSettings.visualQuality);
        m_renderer->resize(m_window.width(), m_window.height());
        m_session.entities.initializeModels(m_paths.assetRoot, *m_renderer);
        m_scene.initialize(*m_renderer, m_paths.assetRoot);
        m_audio.initialize(&m_assets);
        m_ui.renderer.initialize(*m_renderer,
            m_renderer->getBlockAtlasTexture(), m_renderer->usesFramebufferSrgb(),
            m_paths.assetRoot, graphics.api);
        m_ui.renderer.setLocalization(m_ui.localization);
        m_window.setResizeCallback([this](int width, int height) {
            m_renderer->resize(width, height);
        });

        // Start with cursor visible (main menu)
        m_window.setCursorLocked(false);

        // Set up thread pool for async mesh building
        m_session.player.setBlockBreakCallback(
            [this](const glm::ivec3& position, BlockId block) {
                m_session.particles.emitBlockBreak(position, block);
                m_window.gamepads().rumble(.18f, 70, m_clientSettings.gamepadRumble);
            });
        m_session.player.setDamageCallback(
            [this](float amount) {
                m_scene.onPlayerDamaged(amount);
                m_window.gamepads().rumble(std::min(1.0f, .2f + amount * .08f),
                                           140, m_clientSettings.gamepadRumble);
            });
        m_sessionFeedback.setRainVolume =
            [this](float volume) { m_audio.setRainVolume(volume); };
        m_sessionFeedback.playExplosion = [this](float pan, float gain) {
            m_audio.playExplosion(pan, gain);
        };
        m_sessionFeedback.playThunder = [this](float pan, float gain) {
            m_audio.playThunder(pan, gain);
        };
        m_sessionFeedback.rumble = [this](float strength, uint32_t duration) {
            m_window.gamepads().rumble(
                strength, duration, m_clientSettings.gamepadRumble);
        };
        m_sessionFeedback.playerDied = [this] {
            m_window.setCursorLocked(false);
        };
        m_sessionFeedback.autosaveMetadataError = [this] {
            m_flow.showCommandMessage(m_ui.localization.text("message.autosave_log"));
        };
        m_sessionFeedback.autosaveFlushError = [this] {
            m_flow.showCommandMessage(m_ui.localization.text("message.autosave_retry"));
        };
        m_session.player.setBedCallback([this](const glm::ivec3& bed) {
            m_session.worldMetadata.bedSpawn = bed;
            if (!m_session.dayNightCycle.isNight()) {
                m_flow.showCommandMessage(m_ui.localization.text("message.respawn_set"));
            } else if (m_session.entities.hasHostileNear(glm::vec3(bed), 8.0f)) {
                m_flow.showCommandMessage(m_ui.localization.text("message.monsters_nearby"));
            } else {
                m_session.dayNightCycle.resetMorning();
                m_session.weather.setWeather(WeatherType::Clear);
                m_flow.showCommandMessage(m_ui.localization.text("message.slept"));
            }
        });

        // ── Menu callbacks ────────────────────────────────────────────
        m_ui.menuCallbacks.onOpenWorld = [this](const std::string& id) {
            m_flow.startGame(id, false);
        };
        m_ui.menuCallbacks.onRefreshWorlds = [this]() {
            return m_session.worldCatalog.list();
        };
        m_ui.menuCallbacks.onDeleteWorld = [this](const std::string& id) {
            try {
                return m_session.worldCatalog.deleteWorld(id);
            } catch (const std::exception& error) {
                LOG_ERROR("Could not delete world '" << id << "': "
                          << error.what());
                return false;
            }
        };
        m_ui.menuCallbacks.onCreateWorld =
            [this](const std::string& name, const std::string& seedText,
                   GameMode mode, WorldType worldType, bool cheatsEnabled) {
                uint64_t seed = 0;
                if (seedText.empty() || seedText == "-") {
                    std::random_device device;
                    std::mt19937_64 generator(device());
                    seed = generator();
                } else {
                    std::size_t consumed = 0;
                    try {
                        if (seedText.front() == '-') {
                            const int64_t signedSeed = std::stoll(seedText, &consumed, 10);
                            seed = static_cast<uint64_t>(signedSeed);
                        } else {
                            seed = std::stoull(seedText, &consumed, 10);
                        }
                    } catch (const std::exception&) {
                        LOG_WARN("Invalid seed '" << seedText << "'; using a random seed");
                        std::random_device device;
                        std::mt19937_64 generator(device());
                        seed = generator();
                    }
                    if (consumed != seedText.size())
                        LOG_WARN("Seed contained unused characters: " << seedText);
                }
                const std::string id = m_session.worldCatalog.create(
                    name.empty() ? m_ui.localization.text("menu.create.default_name") : name,
                    seed, mode, Difficulty::Normal, cheatsEnabled, worldType);
                m_flow.startGame(id, true);
            };
        m_ui.menuCallbacks.onResume = [this]() { m_flow.resume(); };
        m_ui.menuCallbacks.onBackToMenu = [this]() { m_flow.backToMainMenu(); };
        m_ui.menuCallbacks.onQuit = [this]() { m_running = false; };
        m_ui.menuCallbacks.onSettingsChanged = [this]() { applyClientSettings(); };

        m_ui.menuCallbacks.onOpenSettings = [this]() {
            // Save current state to restore the correct menu on back
            GameState prevState = m_flow.state();
            MenuCallbacks prevCallbacks = m_ui.menuCallbacks;
            m_ui.activeMenu = std::make_unique<SettingsMenu>(m_clientSettings,
                [this]() { applyClientSettings(); },
                [this, prevState, prevCallbacks]() {
                m_flow.restoreState(prevState);
                if (prevState == GameState::Paused) {
                    m_ui.activeMenu = std::make_unique<PauseMenu>(
                        prevCallbacks, m_ui.localization);
                } else {
                    m_flow.showMainMenu();
                }
            }, m_ui.localization, RendererBackendAvailability{
#if defined(MINECRAFTC_ENABLE_OPENGL)
            true,
#else
            false,
#endif
#if defined(MINECRAFTC_ENABLE_VULKAN)
            true
#else
            false
#endif
            });
        };

        // ── Input callbacks ───────────────────────────────────────────
        m_router.bind();

        // ── Show main menu ────────────────────────────────────────────
        m_flow.showMainMenu();

        m_lastFrameTick = m_runtimeClock.now();

        LOG_INFO("MinecraftC initialized");
    }

    void restoreGraphics() {
        m_graphicsResetPending = false;
        m_session.world.invalidateGpuMeshes();
        const GraphicsCapabilities graphics = m_window.graphicsCapabilities();
        m_ui.renderer.resetGraphics();
        m_scene.resetGraphics();
        m_renderer->reinitialize(graphics, m_paths.assetRoot);
        m_renderer->setVisualQuality(m_clientSettings.visualQuality);
        m_session.entities.initializeModels(m_paths.assetRoot, *m_renderer);
        m_scene.restoreGraphics(*m_renderer, m_paths.assetRoot);
        m_ui.renderer.initialize(*m_renderer,
            m_renderer->getBlockAtlasTexture(), m_renderer->usesFramebufferSrgb(),
            m_paths.assetRoot, graphics.api);
        m_ui.renderer.setLocalization(m_ui.localization);
        m_session.world.restoreGpuMeshes();
        m_renderer->resize(m_window.width(), m_window.height());
        LOG_INFO("Graphics resources restored after device reset");
    }

    void applyClientSettings(bool persist = true) {
        m_clientSettings.validate();
        Config::RENDER_DISTANCE = m_clientSettings.renderDistance;
        Config::DAY_CYCLE_MINUTES = m_clientSettings.dayCycleMinutes;
        Config::SMOOTH_LIGHTING = m_clientSettings.smoothLighting;
        Config::AUTO_JUMP = m_clientSettings.autoJump;
        if (m_renderer) m_renderer->setVisualQuality(m_clientSettings.visualQuality);
        if (persist && !m_clientSettings.save(m_paths.settingsFile()))
            LOG_WARN("Could not save client settings");
        if (m_clientSettings.controlMode == ControlMode::KeyboardMouse)
            m_router.releaseGameplayActions();
    }

    struct FrameContext {
        RuntimeClock::Tick started = 0;
        RuntimeClock::Tick now = 0;
        float dt = 0.0f;
    };

    FrameContext beginFramePhases() {
        const RuntimeClock::Tick frameStarted = m_runtimeClock.now();
        m_frameTimer.beginFrame();
        const RuntimeClock::Tick now = m_runtimeClock.now();
        float dt = static_cast<float>(RuntimeClock::seconds(
            RuntimeClock::elapsed(m_lastFrameTick, now)));
        m_lastFrameTick = now;
        dt = std::min(dt, 0.1f);
        m_ui.tick(dt);
        m_router.beginFrame(now, m_ui.commandOpen ||
            (m_ui.activeMenu && m_ui.activeMenu->wantsTextInput()));
        return {frameStarted, now, dt};
    }

    void updateFrameState(float dt, RuntimeClock::Tick now) {
        // ── Update ────────────────────────────────────────────────
        m_session.dayNightCycle.update(
            dt, Config::DAY_CYCLE_MINUTES, m_flow.state() == GameState::Playing);
        if (m_flow.state() != GameState::Playing || m_ui.inventoryOpen ||
            m_ui.commandOpen || m_session.playerDead)
            m_session.player.cancelBowCharge();
        if (m_flow.state() == GameState::Playing) {
            if (m_ui.containerOpen && (!m_ui.containerScreen.valid())) m_flow.closeInventory();
            m_session.updatePlaying(
                dt, m_renderer.get(), m_sessionFeedback);
            m_scene.updateCamera(
                m_session.world, m_session.player, dt, m_session.playerDead);
        } else if (m_flow.state() == GameState::LoadingWorld) {
            if (m_session.advanceLoading(m_renderer.get(), now))
                m_flow.completeLoading();
        }
    }

    void renderFrameScene(float dt, RuntimeClock::Tick now) {
        const bool showFirstPersonItem =
            m_flow.state() == GameState::Playing && !m_ui.inventoryOpen &&
            !m_ui.activeMenu;
        m_scene.render(
            m_session, *m_renderer, m_clientSettings, m_window,
            m_ui.localization, m_flow.state(), showFirstPersonItem, dt, now);
    }
    void renderFrameUi() {
        m_ui.render(
            m_session, m_clientSettings, m_inputs, m_window, m_flow.state(),
            m_scene.perspective != CameraPerspective::ThirdPersonFront);
    }
    void finishFramePhases(RuntimeClock::Tick frameStarted) {
        m_renderer->endFrame();
        m_runtimeClock.sleepUntil(frameStarted + RuntimeClock::fromSeconds(
            1.0 / static_cast<double>(m_clientSettings.frameRateLimit)));
        m_frameTimer.endFrame();
        if (m_window.isKeyPressed(Key::F4) &&
            (m_inputs.keys[Key::LeftAlt] || m_inputs.keys[Key::RightAlt]))
            m_running = false;
    }

    bool runFrame() {
        if (m_window.shouldClose() || !m_running) return false;
        const FrameContext frame = beginFramePhases();
        if (m_window.isMinimized()) {
            RuntimeClock::sleepMilliseconds(100);
            m_window.finishEventFrame();
            return !m_window.shouldClose() && m_running;
        }
        m_router.handleFrameInput(frame.dt);
        updateFrameState(frame.dt, frame.now);
        renderFrameScene(frame.dt, frame.now);
        renderFrameUi();
        finishFramePhases(frame.started);
        m_window.finishEventFrame();
        return !m_window.shouldClose() && m_running;
    }
    void cleanup() {
        if (!m_savedForTermination) m_flow.saveCurrentWorld();
        Debug::Log::shutdown();
        // Resources cleaned up by destructors
    }
};

std::unique_ptr<ApplicationHost> createGameApplication(
    RuntimePaths paths, GraphicsApi api) {
    auto app = std::make_unique<Application>(std::move(paths), api);
    app->start();
    return app;
}
