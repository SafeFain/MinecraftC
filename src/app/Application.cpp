#include "app/Application.h"
#include "app/ApplicationInputController.h"
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
          m_ui(m_session.player, m_clipboard)
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
                saveCurrentWorld();
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
                saveCurrentWorld();
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

    void cyclePerspective() {
        m_scene.cyclePerspective();
    }

    // ── UI / State ────────────────────────────────────────────────────
    GameState             m_gameState = GameState::MainMenu;
    Debug::FrameTimer     m_frameTimer{600};

    RuntimeClock m_runtimeClock;
    RuntimeClock::Tick m_lastFrameTick = 0;
    AudioSystem m_audio;
    ClientSettings m_clientSettings;
    GameSession::Feedback m_sessionFeedback;

    void handleKeyEvent(
        int key, int, ButtonAction action, int mods) {
        if (action == ButtonAction::Press && m_clientSettings.controlMode == ControlMode::Auto)
            m_inputs.touchHudVisible = false;
        auto keyBound = [this, key](InputAction inputAction) {
            const auto& binding = m_clientSettings.bindings[static_cast<size_t>(inputAction)];
            return binding.device == InputDevice::Keyboard && binding.code == key;
        };
        if (m_ui.commandOpen) {
            if (action == ButtonAction::Press) {
                if (key == Key::Escape) {
                    closeCommandInput();
                } else if (key == Key::Enter) {
                    executeCommand();
                } else if (key == Key::Backspace) m_ui.commandInput.backspace();
                else if (key == Key::Delete) m_ui.commandInput.eraseForward();
                else if (key == Key::Left) m_ui.commandInput.moveLeft((mods & KeyModifier::Shift) != 0);
                else if (key == Key::Right) m_ui.commandInput.moveRight((mods & KeyModifier::Shift) != 0);
                else if (key == Key::Home) m_ui.commandInput.moveHome((mods & KeyModifier::Shift) != 0);
                else if (key == Key::End) m_ui.commandInput.moveEnd((mods & KeyModifier::Shift) != 0);
                else if ((mods & KeyModifier::Control) != 0 && key == Key::A) m_ui.commandInput.selectAll();
                else if ((mods & KeyModifier::Control) != 0 && key == Key::C) m_ui.commandInput.copySelection();
                else if ((mods & KeyModifier::Control) != 0 && key == Key::X) m_ui.commandInput.cutSelection();
                else if ((mods & KeyModifier::Control) != 0 && key == Key::V) m_ui.commandInput.pasteClipboard();
            }
            return;
        }

        if (action == ButtonAction::Press && keyBound(InputAction::Command) &&
            m_gameState == GameState::Playing && !m_ui.activeMenu &&
            !m_ui.inventoryOpen && !m_session.playerDead) {
            openCommandInput();
            return;
        }

        // E key — toggle creative inventory (Playing only, no menu active)
        if (action == ButtonAction::Press && keyBound(InputAction::Inventory)) {
            if (m_gameState == GameState::Playing && !m_ui.activeMenu &&
                !m_session.player.isSpectator()) {
                if (m_ui.inventoryOpen) {
                    closeInventory();
                } else {
                    openInventory();
                }
                return;
            }
        }

        // Discrete keyboard actions are handled in the event callback.
        // The callback updates InputState immediately, so waiting until
        // runFrame() would lose this press when beginFrame() clears edges.
        if (action == ButtonAction::Press && keyBound(InputAction::Perspective) &&
            m_gameState == GameState::Playing && !m_ui.activeMenu &&
            !m_ui.inventoryOpen && !m_ui.commandOpen) {
            cyclePerspective();
            return;
        }

        if(action==ButtonAction::Press&&keyBound(InputAction::DropItem)&&
           m_gameState==GameState::Playing&&!m_ui.activeMenu&&!m_ui.inventoryOpen&&
           !m_ui.commandOpen&&!m_session.playerDead){dropSelectedItem();return;}

        // Number keys 1-9 — hotbar selection (Playing only)
        for (int slot = 0; slot < 9 && action == ButtonAction::Press; ++slot) {
            if (!keyBound(static_cast<InputAction>(
                    static_cast<int>(InputAction::Hotbar1) + slot))) continue;
            if (m_gameState == GameState::Playing) {
                m_ui.hotbar.selectSlot(slot);
                m_session.player.setSelectedSlot(m_ui.hotbar.getSelectedSlot());
            }
            break;
        }

        if ((keyBound(InputAction::Attack) || keyBound(InputAction::Use)) &&
            m_gameState == GameState::Playing && !m_ui.inventoryOpen && !m_ui.commandOpen) {
            if (keyBound(InputAction::Attack)) handleGameplayAction(false, action);
            if (keyBound(InputAction::Use) && !m_ui.inventoryOpen) handleGameplayAction(true, action);
            return;
        }

        // ESC handling
        if (key == Key::Escape && action == ButtonAction::Press) {
            // Close inventory first if open
            if (m_ui.inventoryOpen) {
                closeInventory();
                return;
            }

            if (m_gameState == GameState::Playing) {
                // Pause the game
                m_audio.setPaused(true);
                m_gameState = GameState::Paused;
                m_window.setCursorLocked(false);
                m_ui.activeMenu = std::make_unique<PauseMenu>(
                    m_ui.menuCallbacks, m_ui.localization);
            } else if (m_gameState == GameState::Paused) {
                // Resume (ESC in pause menu handled by menu itself)
                // But just in case the menu hasn't handled it:
                m_ui.menuCallbacks.onResume();
            }
            // In MainMenu, ESC does nothing
            return;
        }

        if (m_session.playerDead && action == ButtonAction::Press &&
            (key == Key::Enter || key == Key::Space)) {
            respawnPlayer();
            return;
        }

        // Route to active menu for key presses
        if (action == ButtonAction::Press && m_ui.activeMenu) {
            m_ui.activeMenu->onKeyPress(key, mods);
        }
    }

    void handleTextEvent(std::string_view text) {
        for (const uint32_t codepoint : decodeUtf8(text)) {
            if (m_ui.commandOpen) {
                std::string encoded; appendUtf8(encoded, codepoint);
                m_ui.commandInput.insert(encoded);
            } else if (m_ui.activeMenu) {
                m_ui.activeMenu->onChar(codepoint);
            }
        }
    }

    void handleMouseButtonEvent(
        int button, ButtonAction action, int mods) {
        if (m_inputs.uiTouch.active) return;
        if (action == ButtonAction::Press && m_clientSettings.controlMode == ControlMode::Auto)
            m_inputs.touchHudVisible = false;
        auto mouseBound = [this, button](InputAction inputAction) {
            const auto& binding = m_clientSettings.bindings[static_cast<size_t>(inputAction)];
            return binding.device == InputDevice::Mouse && binding.code == button;
        };
        updateMouseScreenPosition();
        if (action == ButtonAction::Press && m_gameState == GameState::Playing && !m_ui.activeMenu) {
            if (mouseBound(InputAction::Command) && !m_ui.inventoryOpen && !m_session.playerDead) {
                openCommandInput(); return;
            }
            if (mouseBound(InputAction::Inventory) && !m_ui.commandOpen && !m_session.player.isSpectator()) {
                if(m_ui.inventoryOpen)closeInventory();else openInventory();return;
            }
            if(mouseBound(InputAction::Perspective)&&!m_ui.inventoryOpen&&!m_ui.commandOpen){
                cyclePerspective();return;
            }
            if(mouseBound(InputAction::DropItem)&&!m_ui.inventoryOpen&&!m_ui.commandOpen&&
               !m_session.playerDead){dropSelectedItem();return;}
            for(int slot=0;slot<9;++slot)if(mouseBound(static_cast<InputAction>(
                static_cast<int>(InputAction::Hotbar1)+slot))){m_ui.hotbar.selectSlot(slot);
                m_session.player.setSelectedSlot(slot);}
        }
        if (m_ui.inventoryOpen && (playerInventoryViewOpen() || m_ui.containerOpen) &&
            (action == ButtonAction::Press || action == ButtonAction::Release)) {
            if (!m_ui.containerOpen && m_session.player.gameMode() == GameMode::Creative &&
                action == ButtonAction::Press &&
                m_ui.survivalInventory.creativeCatalogButtonContains(
                    static_cast<int>(m_ui.mouseScreenX),static_cast<int>(m_ui.mouseScreenY))) {
                m_ui.openCreativeCatalog();
                return;
            }
            if (m_ui.containerOpen) m_ui.containerScreen.onMouseButton(
                button, action, static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY), mods);
            else m_ui.survivalInventory.onMouseButton(button, action,
                static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY), mods);
            return;
        }
        if (!m_ui.inventoryOpen && !m_ui.commandOpen && m_gameState == GameState::Playing &&
            (mouseBound(InputAction::Attack) || mouseBound(InputAction::Use))) {
            if (mouseBound(InputAction::Attack)) handleGameplayAction(false, action);
            if (mouseBound(InputAction::Use) && !m_ui.inventoryOpen) handleGameplayAction(true, action);
            return;
        }
        if (action == ButtonAction::Press || action == ButtonAction::Release) {
            if (m_ui.inventoryOpen) {
                if (m_session.player.gameMode()==GameMode::Creative &&
                    m_ui.creativeCatalogOpen && action == ButtonAction::Press) {
                    m_ui.inventory.onMouseClick(button,
                        static_cast<int>(m_ui.mouseScreenX),
                        static_cast<int>(m_ui.mouseScreenY),
                        [this](ItemId id) {
                            giveCreativeItem(id);
                        },[this](){openPlayerInventoryView();});
                }
            } else if (m_ui.activeMenu) {
                m_ui.activeMenu->onMouseButton(button, action,
                                            m_ui.mouseScreenX, m_ui.mouseScreenY);
            }
        }
    }

    void handleScrollEvent(double, double yoffset) {
        if (m_ui.activeMenu) { m_ui.activeMenu->onScroll(yoffset); return; }
        if (m_ui.inventoryOpen && m_session.player.gameMode()==GameMode::Creative &&
            m_ui.creativeCatalogOpen && !m_ui.containerOpen) {
            m_ui.inventory.onScroll(yoffset);
            return;
        }
        auto wheelBound=[this,yoffset](InputAction action){const auto& binding=m_clientSettings.bindings[static_cast<size_t>(action)];
            return binding.device==InputDevice::Wheel&&binding.code==(yoffset>0?1:-1);};
        if(m_gameState==GameState::Playing&&!m_ui.commandOpen){
            if(wheelBound(InputAction::Inventory)&&!m_session.player.isSpectator()){if(m_ui.inventoryOpen)closeInventory();else openInventory();return;}
            if(wheelBound(InputAction::Command)&&!m_ui.inventoryOpen&&!m_session.playerDead){openCommandInput();return;}
            if(wheelBound(InputAction::Perspective)&&!m_ui.inventoryOpen){cyclePerspective();return;}
            if(wheelBound(InputAction::DropItem)&&!m_ui.inventoryOpen&&!m_session.playerDead){dropSelectedItem();return;}
            for(int slot=0;slot<9;++slot)if(wheelBound(static_cast<InputAction>(static_cast<int>(InputAction::Hotbar1)+slot)))m_ui.hotbar.selectSlot(slot);
            if(!m_ui.inventoryOpen){if(wheelBound(InputAction::Attack)){handleGameplayAction(false,ButtonAction::Press);handleGameplayAction(false,ButtonAction::Release);}
                if(wheelBound(InputAction::Use)){handleGameplayAction(true,ButtonAction::Press);if(!m_ui.inventoryOpen)handleGameplayAction(true,ButtonAction::Release);}}
        }
        if (m_gameState == GameState::Playing && !m_ui.activeMenu &&
            !m_ui.inventoryOpen && !m_ui.commandOpen) {
            if (m_inputs.state.pressed(InputAction::PreviousSlot)) m_ui.hotbar.onScroll(1.0);
            if (m_inputs.state.pressed(InputAction::NextSlot)) m_ui.hotbar.onScroll(-1.0);
            m_session.player.setSelectedSlot(m_ui.hotbar.getSelectedSlot());
        }
    }

    void bindInputCallbacks() {
        m_inputs.bind(m_window, m_clientSettings, {
            [this](int key, int scancode, ButtonAction action, int mods) {
                handleKeyEvent(key, scancode, action, mods);
            },
            [this](std::string_view text) { handleTextEvent(text); },
            [this](int button, ButtonAction action, int mods) {
                handleMouseButtonEvent(button, action, mods);
            },
            [this](double xoffset, double yoffset) {
                handleScrollEvent(xoffset, yoffset);
            },
            [this](const TouchEvent& event) { handleTouch(event); },
            [this](bool visible) {
                if (!visible && m_ui.commandOpen) closeCommandInput();
            }
        });
    }

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
            showCommandMessage(m_ui.localization.text("message.autosave_log"));
        };
        m_sessionFeedback.autosaveFlushError = [this] {
            showCommandMessage(m_ui.localization.text("message.autosave_retry"));
        };
        m_session.player.setBedCallback([this](const glm::ivec3& bed) {
            m_session.worldMetadata.bedSpawn = bed;
            if (!m_session.dayNightCycle.isNight()) {
                showCommandMessage(m_ui.localization.text("message.respawn_set"));
            } else if (m_session.entities.hasHostileNear(glm::vec3(bed), 8.0f)) {
                showCommandMessage(m_ui.localization.text("message.monsters_nearby"));
            } else {
                m_session.dayNightCycle.resetMorning();
                m_session.weather.setWeather(WeatherType::Clear);
                showCommandMessage(m_ui.localization.text("message.slept"));
            }
        });

        // ── Menu callbacks ────────────────────────────────────────────
        m_ui.menuCallbacks.onOpenWorld = [this](const std::string& id) {
            startGame(id, false);
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
                   GameMode mode, bool cheatsEnabled) {
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
                    seed, mode, Difficulty::Normal, cheatsEnabled);
                startGame(id, true);
            };
        m_ui.menuCallbacks.onResume = [this]() {
            m_audio.setPaused(false);
            m_gameState = GameState::Playing;
            m_window.setCursorLocked(true);
            m_ui.activeMenu.reset();
        };
        m_ui.menuCallbacks.onBackToMenu = [this]() {
            saveCurrentWorld();
            m_session.leaveWorld();
            m_audio.stopRain();
            m_audio.setPaused(false);
            m_gameState = GameState::MainMenu;
            m_window.setCursorLocked(false);
            showMainMenu();
        };
        m_ui.menuCallbacks.onQuit = [this]() { m_running = false; };
        m_ui.menuCallbacks.onSettingsChanged = [this]() { applyClientSettings(); };

        m_ui.menuCallbacks.onOpenSettings = [this]() {
            // Save current state to restore the correct menu on back
            GameState prevState = m_gameState;
            MenuCallbacks prevCallbacks = m_ui.menuCallbacks;
            m_ui.activeMenu = std::make_unique<SettingsMenu>(m_clientSettings,
                [this]() { applyClientSettings(); },
                [this, prevState, prevCallbacks]() {
                m_gameState = prevState;
                if (prevState == GameState::Paused) {
                    m_ui.activeMenu = std::make_unique<PauseMenu>(
                        prevCallbacks, m_ui.localization);
                } else {
                    showMainMenu();
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
        bindInputCallbacks();

        // ── Show main menu ────────────────────────────────────────────
        showMainMenu();

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

    void showMainMenu() {
        m_audio.setMusicMode(AudioMusicMode::Menu);
        m_ui.activeMenu = std::make_unique<MainMenu>(
            m_ui.menuCallbacks, m_session.worldCatalog.list(), m_clientSettings, m_ui.localization,
            &m_clipboard);
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
        if (m_clientSettings.controlMode == ControlMode::KeyboardMouse) {
            handleGameplayAction(false,ButtonAction::Release);
            handleGameplayAction(true,ButtonAction::Release);
            m_inputs.touchControls.cancelAll();
            m_inputs.touchHudVisible = false;
        }
    }

    TouchControlConfig touchConfig() const {
        return {m_clientSettings.touchSensitivity,m_clientSettings.touchControlSize,
                m_clientSettings.touchControlOpacity,m_clientSettings.touchLeftHanded};
    }

    bool touchUiVisible() const {
        return m_clientSettings.controlMode == ControlMode::Touch ||
            (m_clientSettings.controlMode == ControlMode::Auto && m_inputs.touchHudVisible);
    }

    glm::vec2 touchToUi(double x,double y) const {
        int windowWidth=0,windowHeight=0,framebufferWidth=0,framebufferHeight=0;
        windowWidth=m_window.windowWidth();windowHeight=m_window.windowHeight();
        framebufferWidth=m_window.width();framebufferHeight=m_window.height();
        const double scaleX=windowWidth>0?static_cast<double>(framebufferWidth)/windowWidth:1.0;
        const double scaleY=windowHeight>0?static_cast<double>(framebufferHeight)/windowHeight:1.0;
        const double uiScale=std::max(1,m_ui.guiScale);
        const WindowSafeArea safe = m_window.safeArea();
        return {static_cast<float>((x*scaleX-safe.x)/uiScale),
                static_cast<float>((framebufferHeight-y*scaleY-safe.y)/uiScale)};
    }

    void dispatchTouchCommands(const std::vector<TouchCommandEvent>& commands) {
        for(const auto& command:commands){
            switch(command.command){
                case TouchCommand::AttackPress:handleGameplayAction(false,ButtonAction::Press);break;
                case TouchCommand::AttackRelease:handleGameplayAction(false,ButtonAction::Release);break;
                case TouchCommand::UsePress:handleGameplayAction(true,ButtonAction::Press);break;
                case TouchCommand::UseRelease:handleGameplayAction(true,ButtonAction::Release);break;
                case TouchCommand::OpenInventory:
                    if(!m_session.player.isSpectator()) {
                        handleGameplayAction(false,ButtonAction::Release);
                        handleGameplayAction(true,ButtonAction::Release);
                        m_inputs.touchControls.cancelAll();
                        openInventory();
                    }
                    break;
                case TouchCommand::OpenCommand:
                    if (!m_session.playerDead) {
                        handleGameplayAction(false,ButtonAction::Release);
                        handleGameplayAction(true,ButtonAction::Release);
                        m_inputs.touchControls.cancelAll();
                        openCommandInput();
                    }
                    break;
                case TouchCommand::Pause:
                    handleGameplayAction(false,ButtonAction::Release);
                    handleGameplayAction(true,ButtonAction::Release);
                    m_inputs.touchControls.cancelAll();m_audio.setPaused(true);
                    m_gameState=GameState::Paused;m_window.setCursorLocked(false);
                    m_ui.activeMenu=std::make_unique<PauseMenu>(m_ui.menuCallbacks,m_ui.localization);break;
                case TouchCommand::ChangePerspective:cyclePerspective();break;
                case TouchCommand::SelectHotbar:
                    m_ui.hotbar.selectSlot(command.value);m_session.player.setSelectedSlot(command.value);
                    break;
            }
        }
    }

    void dispatchUiTouchButton(int button,ButtonAction action,const glm::vec2& position) {
        const int x=static_cast<int>(position.x),y=static_cast<int>(position.y);
        if(m_ui.inventoryOpen&&(playerInventoryViewOpen()||m_ui.containerOpen)){
            if(!m_ui.containerOpen&&m_session.player.gameMode()==GameMode::Creative&&
               action==ButtonAction::Press&&
               m_ui.survivalInventory.creativeCatalogButtonContains(x,y)){
                m_ui.openCreativeCatalog();return;
            }
            if(m_ui.containerOpen)m_ui.containerScreen.onMouseButton(button,action,x,y);
            else m_ui.survivalInventory.onMouseButton(button,action,x,y);
        }else if(m_ui.inventoryOpen){
            if(action==ButtonAction::Press)m_ui.inventory.onMouseClick(button,x,y,[this](ItemId id){
                giveCreativeItem(id);},[this](){openPlayerInventoryView();});
        }else if(m_ui.activeMenu)m_ui.activeMenu->onMouseButton(button,action,position.x,position.y);
        else if(m_session.playerDead&&action==ButtonAction::Release)respawnPlayer();
    }

    void dispatchUiTouchMove(const glm::vec2& position) {
        const int x=static_cast<int>(position.x),y=static_cast<int>(position.y);
        if(m_ui.inventoryOpen){m_ui.inventory.onMouseMove(x,y);
            if(m_ui.containerOpen)m_ui.containerScreen.onMouseMove(x,y);
            else if(playerInventoryViewOpen())m_ui.survivalInventory.onMouseMove(x,y);
        }
        if(m_ui.activeMenu)m_ui.activeMenu->onMouseMove(position.x,position.y);
    }

    void handleUiTouch(const TouchEvent& event,const glm::vec2& position) {
        if(event.phase==TouchPhase::Begin){
            if(m_inputs.uiTouch.active)return;
            m_inputs.uiTouch={event.id,position,position,m_runtimeClock.now(),true,false,false,false};
            dispatchUiTouchMove(position);
            if (m_ui.activeMenu && m_ui.activeMenu->capturesPointerDrag(
                    position.x, position.y)) {
                dispatchUiTouchButton(
                    MouseButton::Left, ButtonAction::Press, position);
                m_inputs.uiTouch.buttonDown = true;
            }
            return;
        }
        if(!m_inputs.uiTouch.active||event.id!=m_inputs.uiTouch.id)return;
        if(event.phase==TouchPhase::Move){
            m_inputs.uiTouch.position=position;
            const glm::vec2 delta=position-m_inputs.uiTouch.origin;
            const bool scrollSurface=m_ui.activeMenu||(m_ui.inventoryOpen&&
                m_session.player.gameMode()==GameMode::Creative&&m_ui.creativeCatalogOpen&&!m_ui.containerOpen);
            if(scrollSurface&&!m_inputs.uiTouch.buttonDown&&std::abs(delta.y)>24.0f){
                const double scroll=delta.y>0.0f?-1.0:1.0;
                if(m_ui.activeMenu)m_ui.activeMenu->onScroll(scroll);else m_ui.inventory.onScroll(scroll);
                m_inputs.uiTouch.origin=position;m_inputs.uiTouch.scrolling=true;
            }else if(!scrollSurface&&!m_inputs.uiTouch.buttonDown&&glm::length(delta)>8.0f){
                dispatchUiTouchButton(MouseButton::Left,ButtonAction::Press,m_inputs.uiTouch.origin);
                m_inputs.uiTouch.buttonDown=true;
            }
            dispatchUiTouchMove(position);return;
        }
        if(event.phase==TouchPhase::End){
            if(m_inputs.uiTouch.buttonDown)dispatchUiTouchButton(
                m_inputs.uiTouch.rightButton?MouseButton::Right:MouseButton::Left,
                ButtonAction::Release,m_inputs.uiTouch.position);
            else if(!m_inputs.uiTouch.scrolling){
                dispatchUiTouchButton(MouseButton::Left,ButtonAction::Press,m_inputs.uiTouch.position);
                dispatchUiTouchButton(MouseButton::Left,ButtonAction::Release,m_inputs.uiTouch.position);
            }
            m_inputs.uiTouch={};
        }
    }

    void updateLongPress() {
        if(!m_inputs.uiTouch.active||m_inputs.uiTouch.buttonDown||m_inputs.uiTouch.scrolling||
           !m_ui.inventoryOpen||(!playerInventoryViewOpen()&&!m_ui.containerOpen))return;
        if(RuntimeClock::seconds(RuntimeClock::elapsed(m_inputs.uiTouch.started,m_runtimeClock.now()))<.45)return;
        dispatchUiTouchButton(MouseButton::Right,ButtonAction::Press,m_inputs.uiTouch.position);
        m_inputs.uiTouch.buttonDown=true;m_inputs.uiTouch.rightButton=true;
    }

    void handleTouch(const TouchEvent& event) {
        if(event.phase==TouchPhase::Cancel){
            handleGameplayAction(false,ButtonAction::Release);
            handleGameplayAction(true,ButtonAction::Release);
            m_inputs.touchControls.cancelAll();m_inputs.touchGameplay.clear();
            if(m_inputs.uiTouch.active&&m_inputs.uiTouch.buttonDown)dispatchUiTouchButton(
                m_inputs.uiTouch.rightButton?MouseButton::Right:MouseButton::Left,
                ButtonAction::Release,m_inputs.uiTouch.position);
            m_inputs.uiTouch={};return;
        }
        const WindowSafeArea safe = m_window.safeArea();
        m_inputs.touchControls.configure(
            std::max(1,safe.width/std::max(1,m_ui.guiScale)),
            std::max(1,safe.height/std::max(1,m_ui.guiScale)),touchConfig());
        glm::vec2 position=event.phase==TouchPhase::End&&m_inputs.uiTouch.active&&event.id==m_inputs.uiTouch.id
            ?m_inputs.uiTouch.position:touchToUi(event.x,event.y);
        if(event.phase==TouchPhase::Begin&&m_ui.inventoryOpen&&
           touchUiVisible()&&
           touchInventoryCloseRect(
               std::max(1,safe.width/std::max(1,m_ui.guiScale)),
               std::max(1,safe.height/std::max(1,m_ui.guiScale))).contains(position.x,position.y)){
            closeInventory();return;
        }
        bool gameplay=false;
        if(event.phase==TouchPhase::Begin){
            gameplay=m_gameState==GameState::Playing&&!m_ui.inventoryOpen&&!m_ui.activeMenu&&
                !m_ui.commandOpen&&!m_session.playerDead&&m_clientSettings.controlMode!=ControlMode::KeyboardMouse;
            m_inputs.touchGameplay[event.id]=gameplay;
        }else{const auto it=m_inputs.touchGameplay.find(event.id);gameplay=it!=m_inputs.touchGameplay.end()&&it->second;}
        if(gameplay){
            m_inputs.touchHudVisible=true;
            TouchEvent converted=event;converted.x=position.x;converted.y=position.y;
            dispatchTouchCommands(m_inputs.touchControls.onTouch(converted));
            // Preserve press/release edges even when a quick tap begins and
            // ends within one event-poll call.
            m_inputs.state.clearVirtual();
            m_inputs.touchControls.applyTo(m_inputs.state);
            m_inputs.state.update(m_clientSettings.bindings);
        }else handleUiTouch(event,position);
        if(event.phase==TouchPhase::End)m_inputs.touchGameplay.erase(event.id);
    }

    void handleGameplayAction(bool use, ButtonAction action) {
        const int logicalButton = use ? MouseButton::Right : MouseButton::Left;
        if (action == ButtonAction::Press && use && !m_session.player.isSpectator()) {
            auto hit = m_session.world.raycast(m_session.player.getEyePosition(), m_session.player.getForward(),
                                       Config::REACH_DISTANCE);
            if (hit) {
                const BlockId target = m_session.world.getBlock(
                    hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
                if (target == BlockId::CRAFTING_TABLE && m_session.player.isSurvival()) {
                    openInventory();
                    m_ui.survivalInventory.setCraftingTable(true);
                    return;
                }
                if (target == BlockId::CHEST || target == BlockId::FURNACE) {
                    if (m_ui.containerScreen.open(m_session.world, hit->blockPos)) {
                        m_ui.containerOpen = true;
                        m_ui.inventoryOpen = true;
                        m_window.setCursorLocked(false);
                        return;
                    }
                }
            }
        }
        m_session.player.handleMouseButton(logicalButton, action);
    }

    void startGame(const std::string& worldId, bool newWorld) {
        saveCurrentWorld();
        m_audio.setMusicMode(AudioMusicMode::Gameplay);
        const GameMode mode = m_session.startWorld(
            worldId, newWorld, m_runtimeClock.now());

        m_gameState = GameState::LoadingWorld;
        m_ui.hotbar.setInventory(mode == GameMode::Spectator
            ? nullptr : &m_session.player.inventory());
        m_ui.survivalInventory.setCreativeAccess(mode == GameMode::Creative);
        m_audio.stopRain();
        m_scene.resetForWorld(m_session.player.getPosition());
        m_window.setCursorLocked(false);
        m_ui.activeMenu.reset();
        m_ui.commandOpen = false;
        m_ui.commandInput.setText({});
        m_ui.chatHistory.clear();
        m_ui.chatVisibleSeconds = 0.0f;
        LOG_INFO((newWorld ? "Pregenerating new world around spawn"
                           : "Loading existing world around saved position")
                 << " at render distance " << Config::RENDER_DISTANCE);
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
        m_inputs.beginFrame(
            m_window, m_clientSettings, touchConfig(), m_ui.guiScale,
            m_ui.commandOpen ||
                (m_ui.activeMenu && m_ui.activeMenu->wantsTextInput()));
        updateLongPress();
        if (m_gameState == GameState::Playing && !m_ui.inventoryOpen &&
            !m_ui.commandOpen && m_inputs.state.pressed(InputAction::Perspective))
            cyclePerspective();
        if(m_gameState==GameState::Playing&&!m_ui.inventoryOpen&&!m_ui.commandOpen&&
           !m_session.playerDead&&m_inputs.state.pressed(InputAction::DropItem))
            dropSelectedItem();
        updateGamepadUi(now);
        return {frameStarted, now, dt};
    }

    void handleFrameInput(float dt) {
        // ── Handle input ──────────────────────────────────────────
        if (m_gameState == GameState::Playing && !m_ui.inventoryOpen &&
            !m_ui.commandOpen) {
            double dx, dy;
            m_window.getCursorDelta(dx, dy);
            m_session.player.handleMouseDelta(static_cast<float>(dx), static_cast<float>(dy),
                m_clientSettings.mouseSensitivity, m_clientSettings.invertMouseY);
            const float padLookX = normalizeGamepadAxis(m_inputs.gamepadAxes[2], m_clientSettings.gamepadDeadzone);
            float padLookY = normalizeGamepadAxis(m_inputs.gamepadAxes[3], m_clientSettings.gamepadDeadzone);
            if (m_clientSettings.invertGamepadY) padLookY = -padLookY;
            m_session.player.handleMouseDelta(padLookX, padLookY,
                4.0f * m_clientSettings.gamepadLookSensitivity * dt * 60.0f, false);
            const glm::vec2 touchLook=m_inputs.touchControls.consumeLookDelta();
            m_session.player.handleMouseDelta(touchLook.x,-touchLook.y,.15f,false);
            if (!m_session.playerDead) m_session.player.handleMovement(m_inputs.state, dt);
        }

        // Track mouse position (always, for inventory/menu hover)
        {
            double pointerDx=0,pointerDy=0;m_window.getCursorDelta(pointerDx,pointerDy);
            const bool pointerMoved=m_inputs.uiTouch.active||pointerDx!=0.0||pointerDy!=0.0;
            if (!m_inputs.uiTouch.active) updateMouseScreenPosition();
            else {
                m_ui.mouseScreenX=m_inputs.uiTouch.position.x;
                m_ui.mouseScreenY=m_inputs.uiTouch.position.y;
            }

            // Route to inventory hover if open
            if (m_ui.inventoryOpen && pointerMoved) {
                m_ui.inventory.onMouseMove(
                    static_cast<int>(m_ui.mouseScreenX),
                    static_cast<int>(m_ui.mouseScreenY));
                if (m_ui.containerOpen) m_ui.containerScreen.onMouseMove(
                    static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY));
                else if (playerInventoryViewOpen()) m_ui.survivalInventory.onMouseMove(
                    static_cast<int>(m_ui.mouseScreenX), static_cast<int>(m_ui.mouseScreenY));
            }

            // Route to menu hover
            if (m_ui.activeMenu && pointerMoved) {
                m_ui.activeMenu->onMouseMove(m_ui.mouseScreenX, m_ui.mouseScreenY);
            }
        }
    }

    void updateFrameState(float dt, RuntimeClock::Tick now) {
        // ── Update ────────────────────────────────────────────────
        m_session.dayNightCycle.update(
            dt, Config::DAY_CYCLE_MINUTES, m_gameState == GameState::Playing);
        if (m_gameState != GameState::Playing || m_ui.inventoryOpen ||
            m_ui.commandOpen || m_session.playerDead)
            m_session.player.cancelBowCharge();
        if (m_gameState == GameState::Playing) {
            if (m_ui.containerOpen && (!m_ui.containerScreen.valid())) closeInventory();
            m_session.updatePlaying(
                dt, m_renderer.get(), m_sessionFeedback);
            m_scene.updateCamera(
                m_session.world, m_session.player, dt, m_session.playerDead);
        } else if (m_gameState == GameState::LoadingWorld) {
            if (m_session.advanceLoading(m_renderer.get(), now)) {
                m_gameState = GameState::Playing;
                m_window.setCursorLocked(true);
            }
        }
    }

    void renderFrameScene(float dt, RuntimeClock::Tick now) {
        const bool showFirstPersonItem =
            m_gameState == GameState::Playing && !m_ui.inventoryOpen &&
            !m_ui.activeMenu;
        m_scene.render(
            m_session, *m_renderer, m_clientSettings, m_window,
            m_ui.localization, m_gameState, showFirstPersonItem, dt, now);
    }
    void renderFrameUi() {
        m_ui.render(
            m_session, m_clientSettings, m_inputs, m_window, m_gameState,
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
        handleFrameInput(frame.dt);
        updateFrameState(frame.dt, frame.now);
        renderFrameScene(frame.dt, frame.now);
        renderFrameUi();
        finishFramePhases(frame.started);
        m_window.finishEventFrame();
        return !m_window.shouldClose() && m_running;
    }
    void updateGamepadUi(RuntimeClock::Tick now) {
        auto* settings=dynamic_cast<SettingsMenu*>(m_ui.activeMenu.get());
        if(settings&&settings->capturingGamepad()){
            bool centered=true;for(float axis:m_inputs.gamepadAxes)if(std::abs(axis)>.25f)centered=false;
            if(centered)m_inputs.gamepadCaptureArmed=true;
            for(size_t i=0;i<m_inputs.gamepadButtons.size();++i)if(m_inputs.gamepadButtons[i]&&!m_inputs.previousGamepadButtons[i]){
                if(i==4)settings->onKeyPress(Key::Escape);
                else settings->onGamepadBinding({GamepadBindingType::Button,static_cast<int>(i)});
                m_inputs.gamepadCaptureArmed=false;break;
            }
            if(m_inputs.gamepadCaptureArmed&&settings->capturingGamepad())for(size_t i=0;i<m_inputs.gamepadAxes.size();++i){
                if(std::abs(m_inputs.gamepadAxes[i])>.65f){settings->onGamepadBinding({m_inputs.gamepadAxes[i]>0?GamepadBindingType::AxisPositive:GamepadBindingType::AxisNegative,static_cast<int>(i)});m_inputs.gamepadCaptureArmed=false;break;}}
            m_inputs.previousGamepadButtons=m_inputs.gamepadButtons;return;
        }
        m_inputs.gamepadCaptureArmed=false;
        const bool pressA=m_inputs.gamepadButtons[0]&&!m_inputs.previousGamepadButtons[0];
        const bool pressB=m_inputs.gamepadButtons[1]&&!m_inputs.previousGamepadButtons[1];
        const bool pressX=m_inputs.gamepadButtons[2]&&!m_inputs.previousGamepadButtons[2];
        const bool pressY=m_inputs.gamepadButtons[3]&&!m_inputs.previousGamepadButtons[3];
        int navX=(m_inputs.gamepadButtons[14]||m_inputs.gamepadAxes[0]>.65f)?1:
                 (m_inputs.gamepadButtons[13]||m_inputs.gamepadAxes[0]<-.65f)?-1:0;
        int navY=(m_inputs.gamepadButtons[12]||m_inputs.gamepadAxes[1]>.65f)?1:
                 (m_inputs.gamepadButtons[11]||m_inputs.gamepadAxes[1]<-.65f)?-1:0;
        bool navigate=navX!=m_inputs.gamepadNavX||navY!=m_inputs.gamepadNavY;
        if((navX||navY)&&now>=m_inputs.gamepadRepeatTick){navigate=true;m_inputs.gamepadRepeatTick=now+RuntimeClock::fromSeconds(.12);}
        if((navX!=m_inputs.gamepadNavX||navY!=m_inputs.gamepadNavY)&&(navX||navY))m_inputs.gamepadRepeatTick=now+RuntimeClock::fromSeconds(.35);
        m_inputs.gamepadNavX=navX;m_inputs.gamepadNavY=navY;
        if(m_ui.inventoryOpen){
            if(navigate){if(m_ui.containerOpen)m_ui.containerScreen.onGamepadNavigate(navX,-navY);else if(playerInventoryViewOpen())m_ui.survivalInventory.onGamepadNavigate(navX,-navY);else m_ui.inventory.onGamepadNavigate(navX,navY);}
            if(m_ui.containerOpen){if(pressA)m_ui.containerScreen.onGamepadAction(0);if(pressX)m_ui.containerScreen.onGamepadAction(1);if(pressY)m_ui.containerScreen.onGamepadAction(2);}
            else if(playerInventoryViewOpen()){
                if(pressA)m_ui.survivalInventory.onGamepadAction(0);
                if(pressY)m_ui.survivalInventory.onGamepadAction(2);
                if(pressX&&m_session.player.gameMode()==GameMode::Creative){m_ui.openCreativeCatalog();}
                else if(pressX)m_ui.survivalInventory.onGamepadAction(1);
            }else{
                if(pressA)m_ui.inventory.onGamepadAction(true,[this](ItemId id){giveCreativeItem(id);});
                if(pressX)openPlayerInventoryView();
            }
            if(pressB)closeInventory();
        }else if(m_ui.activeMenu){
            if(navigate){if(navY<0)m_ui.activeMenu->onKeyPress(Key::Up);else if(navY>0)m_ui.activeMenu->onKeyPress(Key::Down);else if(navX<0)m_ui.activeMenu->onKeyPress(Key::Left);else if(navX>0)m_ui.activeMenu->onKeyPress(Key::Right);}
            if(pressA)m_ui.activeMenu->onKeyPress(Key::Enter);
            if(pressB)m_ui.activeMenu->onKeyPress(Key::Escape);
        }
        m_inputs.previousGamepadButtons=m_inputs.gamepadButtons;
    }

    void openInventory() {
        if (m_session.player.isSpectator()) return;
        m_session.player.cancelBowCharge();
        if (m_session.player.isSurvival() && !m_ui.inventoryOpen)
            m_ui.survivalInventory.setCraftingTable(false);
        m_ui.openInventory(
            m_session.player.gameMode() == GameMode::Creative);
        m_window.setCursorLocked(false);
    }

    bool playerInventoryViewOpen() const {
        return m_ui.playerInventoryViewOpen(m_session.player);
    }

    void giveCreativeItem(ItemId id) {
        auto& slot=m_session.player.inventory().slot(
            static_cast<size_t>(m_ui.hotbar.getSelectedSlot()));
        InventoryInteraction::setCreativeItem(slot,id);
        m_ui.itemNameSeconds=2.0f;
    }

    void dropSelectedItem(){
        if(m_session.player.isSpectator())return;
        auto& slot=m_session.player.inventory().slot(
            static_cast<size_t>(m_ui.hotbar.getSelectedSlot()));
        const ItemStack dropped=InventoryInteraction::takeOne(slot);
        if(dropped.empty())return;
        const glm::vec3 forward=glm::normalize(m_session.player.getForward());
        m_session.entities.spawnItem(
            m_session.player.getEyePosition()+glm::dvec3(forward)*0.65,
            dropped,forward*4.5f+glm::vec3(0.0f,1.5f,0.0f),0.8f);
    }

    void openPlayerInventoryView() {
        if(m_session.player.gameMode()!=GameMode::Creative)return;
        m_ui.openPlayerInventoryTab();
    }

    void closeCommandInput() {
        m_ui.closeCommand();
        if (m_gameState == GameState::Playing) m_window.setCursorLocked(true);
    }

    void openCommandInput() {
        m_ui.openCommand();
        m_window.setCursorLocked(false);
    }

    void showCommandMessage(const std::string& message) {
        m_ui.showMessage(message);
    }

    void showCommandError(const std::string& submitted, const CommandError& error) {
        const std::string column = std::to_string(error.position + 1);
        showCommandMessage(error.kind == CommandErrorKind::UnknownCommand
            ? m_ui.localization.format("message.command_unknown", {column})
            : m_ui.localization.format("message.command_error", {column, error.expected}));
        showCommandMessage(submitted);
        showCommandMessage(std::string(error.position, ' ') + "^");
    }

    void executeCommand() {
        const std::string submitted = m_ui.commandInput.text();
        closeCommandInput();
        if (submitted.empty()) return;
        if (submitted.front() != '/') {
            showCommandMessage(m_ui.localization.format("message.chat_self", {submitted}));
            return;
        }

        const CommandParseResult result = parseCommand(submitted);
        if (result.error) {
            showCommandError(submitted, *result.error);
            return;
        }
        const GameSession::CommandResult execution =
            m_session.executeCommand(*result.command, m_ui.localization);
        if (execution.gameModeChanged) {
            const GameMode mode = *execution.gameModeChanged;
            m_ui.hotbar.setInventory(mode == GameMode::Spectator
                ? nullptr : &m_session.player.inventory());
            m_ui.survivalInventory.setCreativeAccess(
                mode == GameMode::Creative);
        }
        for (const std::string& message : execution.messages)
            showCommandMessage(message);
    }

    void updateMouseScreenPosition() {
        double windowX = 0.0;
        double windowY = 0.0;
        m_window.getCursorPos(windowX, windowY);

        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        windowWidth=m_window.windowWidth();windowHeight=m_window.windowHeight();
        framebufferWidth=m_window.width();framebufferHeight=m_window.height();

        const double scaleX = windowWidth > 0
            ? static_cast<double>(framebufferWidth) / windowWidth : 1.0;
        const double scaleY = windowHeight > 0
            ? static_cast<double>(framebufferHeight) / windowHeight : 1.0;
        const double uiScale = std::max(1, m_ui.guiScale);
        const WindowSafeArea safe = m_window.safeArea();
        m_ui.mouseScreenX = (windowX * scaleX - safe.x) / uiScale;
        m_ui.mouseScreenY =
            (static_cast<double>(framebufferHeight) - windowY * scaleY - safe.y) / uiScale;
    }

    void respawnPlayer() {
        m_session.respawn();
        m_scene.resetPlayerFeedback(m_session.player.getPosition());
        m_window.setCursorLocked(true);
    }

    void closeInventory() {
        if (m_ui.containerOpen) {
            m_ui.containerScreen.close([this](ItemStack stack) {
                m_session.entities.spawnItem(m_session.player.getPosition() + glm::dvec3(0.0, 0.5, 0.0), stack);
            });
        } else if (playerInventoryViewOpen()) m_ui.survivalInventory.onClose();
        m_ui.containerOpen = false;
        m_ui.inventoryOpen = false;
        m_window.setCursorLocked(true);
    }

    void cleanup() {
        if (!m_savedForTermination) saveCurrentWorld();
        Debug::Log::shutdown();
        // Resources cleaned up by destructors
    }

    void saveCurrentWorld() {
        m_session.saveNow([this] {
            showCommandMessage(m_ui.localization.text("message.save_log"));
        });
    }
};

std::unique_ptr<ApplicationHost> createGameApplication(
    RuntimePaths paths, GraphicsApi api) {
    auto app = std::make_unique<Application>(std::move(paths), api);
    app->start();
    return app;
}
