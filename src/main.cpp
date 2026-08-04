#include "core/Window.h"
#include "core/Input.h"
#include "core/Platform.h"
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "renderer/RenderEnvironment.h"
#include "renderer/ParticleSystem.h"
#include "renderer/CameraEffects.h"
#include "Config.h"
#include "world/World.h"
#include "player/Player.h"
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
#include "world/WorldGenContext.h"
#include "entity/EntityManager.h"
#include "audio/AudioSystem.h"
#include <glad/glad.h>

#include <stdexcept>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <random>
#include <iostream>
#include <limits>
#include <unordered_map>

class Application {
public:
    explicit Application(RuntimePaths paths)
        : m_paths(std::move(paths)),
          m_camera(Config::FOV, Config::NEAR_PLANE, Config::FAR_PLANE),
          m_worldCatalog(m_paths.savesDirectory())
    {}

    int run() {
        try {
            initialize();
            mainLoop();
            cleanup();
            return 0;
        } catch (const std::exception& e) {
            LOG_FATAL("Fatal error: " << e.what());
            return 1;
        }
    }

private:
    RuntimePaths m_paths;
    Window      m_window{Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, "MinecraftC"};
    Renderer    m_renderer;
    Camera      m_camera;
    CameraEffects m_cameraEffects;
    ThreadPool  m_threadPool;
    World       m_world;
    Player      m_player{m_world};
    EntityManager m_entities{m_world};
    bool        m_running = true;

    // ── UI / State ────────────────────────────────────────────────────
    GameState             m_gameState = GameState::MainMenu;
    UIRenderer            m_uiRenderer;
    Localization          m_localization;
    std::unique_ptr<Menu> m_activeMenu;
    MenuCallbacks         m_menuCallbacks;
    bool                  m_terrainGenerated = false;
    bool                  m_loadingNewWorld = false;
    bool                  m_loadingGenerationComplete = false;
    std::chrono::high_resolution_clock::time_point m_worldLoadingStarted;
    std::unique_ptr<SaveStore> m_saveStore;
    WorldCatalog          m_worldCatalog;
    WorldMetadata         m_worldMetadata;
    float                 m_autosaveSeconds = 0.0f;
    bool                  m_autosavePending = false;
    bool                  m_autosaveEntityTurn = true;
    float                 m_titleUpdateSeconds = 0.0f;
    Debug::FrameTimer     m_frameTimer{600};
    bool                  m_playerDead = false;
    uint64_t              m_survivalTicks = 0;
    float                 m_survivalWorldTickRemainder = 0.0f;

    Hotbar                m_hotbar;
    CreativeInventory     m_inventory;
    SurvivalInventoryScreen m_survivalInventory{m_player.inventory()};
    ContainerScreen         m_containerScreen{m_player.inventory()};
    bool                    m_containerOpen = false;
    bool                  m_inventoryOpen = false;
    double                m_mouseScreenX = 0.0;
    double                m_mouseScreenY = 0.0;
    bool                  m_commandOpen = false;
    bool                  m_suppressCommandChar = false;
    std::string           m_commandInput;
    std::string           m_commandMessage;
    float                 m_commandMessageSeconds = 0.0f;

    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_lastFrame;
    DayNightCycle m_dayNightCycle;
    WeatherSystem m_weather;
    ParticleSystem m_particles;
    std::vector<ParticleRenderData> m_particleRenderData;
    AudioSystem m_audio;
    struct LightningEvent {
        glm::dvec3 position{0.0};
        float seconds = 0.0f;
    };
    std::vector<LightningEvent> m_lightningEvents;
    ClientSettings m_clientSettings;
    InputState m_input;
    TouchControls m_touchControls;
    bool m_touchHudVisible = false;
    std::unordered_map<int32_t, bool> m_touchGameplay;
    struct UiTouchState {
        int32_t id = -1;
        glm::vec2 position{0.0f};
        glm::vec2 origin{0.0f};
        Clock::time_point started{};
        bool active = false;
        bool buttonDown = false;
        bool rightButton = false;
        bool scrolling = false;
    } m_uiTouch;
    int m_guiScale = 1;
    int m_lastHudSlot = -1;
    float m_itemNameSeconds = 0.0f;

    // Key state tracking
    bool m_keys[512] = {};

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
        m_localization.load(m_paths.assetRoot);
        m_localization.setLanguage(m_clientSettings.language);
        applyClientSettings(false);

        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
            throw std::runtime_error("Failed to load OpenGL functions");
        }

        // Set initial viewport (framebuffer size already queried in Window constructor)
        glViewport(0, 0, m_window.width(), m_window.height());

        m_renderer.initialize(m_window.isSrgbCapable(), m_paths.assetRoot);
        m_entities.initializeModels(m_paths.assetRoot, m_renderer);
        m_audio.initialize();
        m_uiRenderer.initialize(
            m_renderer.getBlockAtlasTexture(), m_renderer.usesFramebufferSrgb(),
            m_paths.assetRoot);
        m_uiRenderer.setLocalization(m_localization);

        // Start with cursor visible (main menu)
        m_window.setCursorLocked(false);

        // Set up thread pool for async mesh building
        m_world.setThreadPool(&m_threadPool);
        m_player.setEntityManager(&m_entities);
        m_player.setBlockBreakCallback(
            [this](const glm::ivec3& position, BlockId block) {
                m_particles.emitBlockBreak(position, block);
            });
        m_player.setDamageCallback(
            [this](float amount) { m_cameraEffects.onDamage(amount); });
        m_player.setBedCallback([this](const glm::ivec3& bed) {
            m_worldMetadata.bedSpawn = bed;
            if (!m_dayNightCycle.isNight()) {
                showCommandMessage(m_localization.text("message.respawn_set"));
            } else if (m_entities.hasHostileNear(glm::vec3(bed), 8.0f)) {
                showCommandMessage(m_localization.text("message.monsters_nearby"));
            } else {
                m_dayNightCycle.resetMorning();
                m_weather.setWeather(WeatherType::Clear);
                showCommandMessage(m_localization.text("message.slept"));
            }
        });

        // ── Menu callbacks ────────────────────────────────────────────
        m_menuCallbacks.onOpenWorld = [this](const std::string& id) {
            startGame(id, false);
        };
        m_menuCallbacks.onRefreshWorlds = [this]() {
            return m_worldCatalog.list();
        };
        m_menuCallbacks.onDeleteWorld = [this](const std::string& id) {
            try {
                return m_worldCatalog.deleteWorld(id);
            } catch (const std::exception& error) {
                LOG_ERROR("Could not delete world '" << id << "': "
                          << error.what());
                return false;
            }
        };
        m_menuCallbacks.onCreateWorld =
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
                const std::string id = m_worldCatalog.create(
                    name.empty() ? m_localization.text("menu.create.default_name") : name,
                    seed, mode, Difficulty::Normal, cheatsEnabled);
                startGame(id, true);
            };
        m_menuCallbacks.onResume = [this]() {
            m_gameState = GameState::Playing;
            m_window.setCursorLocked(true);
            m_activeMenu.reset();
        };
        m_menuCallbacks.onBackToMenu = [this]() {
            saveCurrentWorld();
            m_world.setSaveStore(nullptr);
            m_entities.setSaveStore(nullptr);
            m_saveStore.reset();
            m_terrainGenerated = false;
            m_audio.stopRain();
            m_gameState = GameState::MainMenu;
            m_window.setCursorLocked(false);
            showMainMenu();
        };
        m_menuCallbacks.onQuit = [this]() { m_running = false; };
        m_menuCallbacks.onSettingsChanged = [this]() { applyClientSettings(); };

        m_menuCallbacks.onOpenSettings = [this]() {
            // Save current state to restore the correct menu on back
            GameState prevState = m_gameState;
            MenuCallbacks prevCallbacks = m_menuCallbacks;
            m_activeMenu = std::make_unique<SettingsMenu>(m_clientSettings,
                [this]() { applyClientSettings(); },
                [this, prevState, prevCallbacks]() {
                m_gameState = prevState;
                if (prevState == GameState::Paused) {
                    m_activeMenu = std::make_unique<PauseMenu>(
                        prevCallbacks, m_localization);
                } else {
                    showMainMenu();
                }
            }, m_localization);
        };

        // ── Input callbacks ───────────────────────────────────────────
        m_window.setKeyCallback([this](int key, int /*scancode*/, int action, int /*mods*/) {
            if (action == GLFW_PRESS && m_clientSettings.controlMode == ControlMode::Auto)
                m_touchHudVisible = false;
            m_input.keyEvent(key, action);
            m_input.update(m_clientSettings.bindings);
            auto keyBound = [this, key](InputAction inputAction) {
                const auto& binding = m_clientSettings.bindings[static_cast<size_t>(inputAction)];
                return binding.device == InputDevice::Keyboard && binding.code == key;
            };
            // Track key state for legacy window/system shortcuts.
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key < 512) m_keys[key] = true;
            } else if (action == GLFW_RELEASE) {
                if (key < 512) m_keys[key] = false;
            }

            if (m_commandOpen) {
                if (action == GLFW_PRESS) {
                    if (key == GLFW_KEY_ESCAPE) {
                        closeCommandInput();
                    } else if (key == GLFW_KEY_ENTER) {
                        executeCommand();
                    } else if (key == GLFW_KEY_BACKSPACE &&
                               !m_commandInput.empty()) {
                        m_commandInput.pop_back();
                    }
                }
                return;
            }

            if (action == GLFW_PRESS && keyBound(InputAction::Command) &&
                m_gameState == GameState::Playing && !m_activeMenu &&
                !m_inventoryOpen && !m_playerDead) {
                m_commandOpen = true;
                m_commandInput.clear();
                m_suppressCommandChar = true;
                m_window.setCursorLocked(false);
                return;
            }

            // E key — toggle creative inventory (Playing only, no menu active)
            if (action == GLFW_PRESS && keyBound(InputAction::Inventory)) {
                if (m_gameState == GameState::Playing && !m_activeMenu &&
                    !m_player.isSpectator()) {
                    if (m_inventoryOpen) {
                        closeInventory();
                    } else {
                        openInventory();
                    }
                    return;
                }
            }

            // Number keys 1-9 — hotbar selection (Playing only)
            for (int slot = 0; slot < 9 && action == GLFW_PRESS; ++slot) {
                if (!keyBound(static_cast<InputAction>(
                        static_cast<int>(InputAction::Hotbar1) + slot))) continue;
                if (m_gameState == GameState::Playing) {
                    m_hotbar.selectSlot(slot);
                    m_player.setSelectedSlot(m_hotbar.getSelectedSlot());
                    m_player.setSelectedCreativeItem(m_hotbar.getSelectedItem());
                    m_player.setSelectedBlock(m_hotbar.getSelectedBlock());
                }
                break;
            }

            if ((keyBound(InputAction::Attack) || keyBound(InputAction::Use)) &&
                m_gameState == GameState::Playing && !m_inventoryOpen && !m_commandOpen) {
                if (keyBound(InputAction::Attack)) handleGameplayAction(false, action);
                if (keyBound(InputAction::Use) && !m_inventoryOpen) handleGameplayAction(true, action);
                return;
            }

            // ESC handling
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
                // Close inventory first if open
                if (m_inventoryOpen) {
                    closeInventory();
                    return;
                }

                if (m_gameState == GameState::Playing) {
                    // Pause the game
                    m_gameState = GameState::Paused;
                    m_window.setCursorLocked(false);
                    m_activeMenu = std::make_unique<PauseMenu>(
                        m_menuCallbacks, m_localization);
                } else if (m_gameState == GameState::Paused) {
                    // Resume (ESC in pause menu handled by menu itself)
                    // But just in case the menu hasn't handled it:
                    m_menuCallbacks.onResume();
                }
                // In MainMenu, ESC does nothing
                return;
            }

            if (m_playerDead && action == GLFW_PRESS &&
                (key == GLFW_KEY_ENTER || key == GLFW_KEY_SPACE)) {
                respawnPlayer();
                return;
            }

            // Route to active menu for key presses
            if (action == GLFW_PRESS && m_activeMenu) {
                m_activeMenu->onKeyPress(key);
            }
        });
        m_window.setCharCallback([this](unsigned int codepoint) {
            if (m_commandOpen) {
                if (m_suppressCommandChar) {
                    m_suppressCommandChar = false;
                    return;
                }
                if (codepoint >= 32 && codepoint <= 126 &&
                    m_commandInput.size() < 80) {
                    m_commandInput.push_back(static_cast<char>(codepoint));
                }
            } else if (m_activeMenu) {
                m_activeMenu->onChar(codepoint);
            }
        });

        m_window.setMouseButtonCallback([this](int button, int action, int mods) {
            if (m_uiTouch.active) return;
            if (action == GLFW_PRESS && m_clientSettings.controlMode == ControlMode::Auto)
                m_touchHudVisible = false;
            m_input.mouseEvent(button, action);
            m_input.update(m_clientSettings.bindings);
            auto mouseBound = [this, button](InputAction inputAction) {
                const auto& binding = m_clientSettings.bindings[static_cast<size_t>(inputAction)];
                return binding.device == InputDevice::Mouse && binding.code == button;
            };
            updateMouseScreenPosition();
            if (action == GLFW_PRESS && m_gameState == GameState::Playing && !m_activeMenu) {
                if (mouseBound(InputAction::Command) && !m_inventoryOpen && !m_playerDead) {
                    m_commandOpen=true;m_commandInput.clear();m_window.setCursorLocked(false);return;
                }
                if (mouseBound(InputAction::Inventory) && !m_commandOpen && !m_player.isSpectator()) {
                    if(m_inventoryOpen)closeInventory();else openInventory();return;
                }
                for(int slot=0;slot<9;++slot)if(mouseBound(static_cast<InputAction>(
                    static_cast<int>(InputAction::Hotbar1)+slot))){m_hotbar.selectSlot(slot);
                    m_player.setSelectedSlot(slot);
                    m_player.setSelectedCreativeItem(m_hotbar.getSelectedItem());
                    m_player.setSelectedBlock(m_hotbar.getSelectedBlock());}
            }
            if (m_inventoryOpen && (m_player.isSurvival() || m_containerOpen) &&
                (action == GLFW_PRESS || action == GLFW_RELEASE)) {
                if (m_containerOpen) m_containerScreen.onMouseButton(
                    button, action, static_cast<int>(m_mouseScreenX), static_cast<int>(m_mouseScreenY), mods);
                else m_survivalInventory.onMouseButton(button, action,
                    static_cast<int>(m_mouseScreenX), static_cast<int>(m_mouseScreenY), mods);
                return;
            }
            if (!m_inventoryOpen && !m_commandOpen && m_gameState == GameState::Playing &&
                (mouseBound(InputAction::Attack) || mouseBound(InputAction::Use))) {
                if (mouseBound(InputAction::Attack)) handleGameplayAction(false, action);
                if (mouseBound(InputAction::Use) && !m_inventoryOpen) handleGameplayAction(true, action);
                return;
            }
            if (action == GLFW_PRESS || action == GLFW_RELEASE) {
                if (m_inventoryOpen) {
                    if (!m_player.isSurvival() && action == GLFW_PRESS) {
                        m_inventory.onMouseClick(button,
                            static_cast<int>(m_mouseScreenX),
                            static_cast<int>(m_mouseScreenY),
                            [this](ItemId id) {
                                m_hotbar.setSlotItem(m_hotbar.getSelectedSlot(), id);
                                m_player.setSelectedCreativeItem(id);
                                m_player.setSelectedBlock(m_hotbar.getSelectedBlock());
                            });
                    }
                } else if (m_activeMenu) {
                    m_activeMenu->onMouseButton(button, action,
                                                m_mouseScreenX, m_mouseScreenY);
                }
            }
        });

        m_window.setScrollCallback([this](double /*xoffset*/, double yoffset) {
            m_input.scrollEvent(yoffset);
            m_input.update(m_clientSettings.bindings);
            if (m_activeMenu) { m_activeMenu->onScroll(yoffset); return; }
            if (m_inventoryOpen && !m_player.isSurvival() && !m_containerOpen) {
                m_inventory.onScroll(yoffset);
                return;
            }
            auto wheelBound=[this,yoffset](InputAction action){const auto& binding=m_clientSettings.bindings[static_cast<size_t>(action)];
                return binding.device==InputDevice::Wheel&&binding.code==(yoffset>0?1:-1);};
            if(m_gameState==GameState::Playing&&!m_commandOpen){
                if(wheelBound(InputAction::Inventory)&&!m_player.isSpectator()){if(m_inventoryOpen)closeInventory();else openInventory();return;}
                if(wheelBound(InputAction::Command)&&!m_inventoryOpen&&!m_playerDead){m_commandOpen=true;m_commandInput.clear();m_window.setCursorLocked(false);return;}
                for(int slot=0;slot<9;++slot)if(wheelBound(static_cast<InputAction>(static_cast<int>(InputAction::Hotbar1)+slot)))m_hotbar.selectSlot(slot);
                if(!m_inventoryOpen){if(wheelBound(InputAction::Attack)){handleGameplayAction(false,GLFW_PRESS);handleGameplayAction(false,GLFW_RELEASE);}
                    if(wheelBound(InputAction::Use)){handleGameplayAction(true,GLFW_PRESS);if(!m_inventoryOpen)handleGameplayAction(true,GLFW_RELEASE);}}
            }
            if (m_gameState == GameState::Playing && !m_activeMenu &&
                !m_inventoryOpen && !m_commandOpen) {
                if (m_input.pressed(InputAction::PreviousSlot)) m_hotbar.onScroll(1.0);
                if (m_input.pressed(InputAction::NextSlot)) m_hotbar.onScroll(-1.0);
                m_player.setSelectedSlot(m_hotbar.getSelectedSlot());
                m_player.setSelectedCreativeItem(m_hotbar.getSelectedItem());
                m_player.setSelectedBlock(m_hotbar.getSelectedBlock());
            }
        });

        m_window.setTouchCallback([this](const TouchEvent& event) { handleTouch(event); });

        // ── Show main menu ────────────────────────────────────────────
        showMainMenu();

        m_lastFrame = Clock::now();

        LOG_INFO("MinecraftC initialized");
        LOG_INFO("OpenGL: " << glGetString(GL_VERSION));
        LOG_INFO("Renderer: " << glGetString(GL_RENDERER));
    }

    void showMainMenu() {
        m_activeMenu = std::make_unique<MainMenu>(
            m_menuCallbacks, m_worldCatalog.list(), m_clientSettings, m_localization);
    }

    void applyClientSettings(bool persist = true) {
        m_clientSettings.validate();
        Config::RENDER_DISTANCE = m_clientSettings.renderDistance;
        Config::DAY_CYCLE_MINUTES = m_clientSettings.dayCycleMinutes;
        Config::SMOOTH_LIGHTING = m_clientSettings.smoothLighting;
        Config::AUTO_JUMP = m_clientSettings.autoJump;
        m_window.setRawMouseInput(m_clientSettings.rawMouseInput);
        if (persist && !m_clientSettings.save(m_paths.settingsFile()))
            LOG_WARN("Could not save client settings");
        if (m_clientSettings.controlMode == ControlMode::KeyboardMouse) {
            handleGameplayAction(false,GLFW_RELEASE);
            handleGameplayAction(true,GLFW_RELEASE);
            m_touchControls.cancelAll();
            m_touchHudVisible = false;
        }
    }

    TouchControlConfig touchConfig() const {
        return {m_clientSettings.touchSensitivity,m_clientSettings.touchControlSize,
                m_clientSettings.touchControlOpacity,m_clientSettings.touchLeftHanded};
    }

    glm::vec2 touchToUi(double x,double y) const {
        int windowWidth=0,windowHeight=0,framebufferWidth=0,framebufferHeight=0;
        glfwGetWindowSize(m_window.native(),&windowWidth,&windowHeight);
        glfwGetFramebufferSize(m_window.native(),&framebufferWidth,&framebufferHeight);
        const double scaleX=windowWidth>0?static_cast<double>(framebufferWidth)/windowWidth:1.0;
        const double scaleY=windowHeight>0?static_cast<double>(framebufferHeight)/windowHeight:1.0;
        const double uiScale=std::max(1,m_guiScale);
        return {static_cast<float>(x*scaleX/uiScale),
                static_cast<float>((framebufferHeight-y*scaleY)/uiScale)};
    }

    void dispatchTouchCommands(const std::vector<TouchCommandEvent>& commands) {
        for(const auto& command:commands){
            switch(command.command){
                case TouchCommand::AttackPress:handleGameplayAction(false,GLFW_PRESS);break;
                case TouchCommand::AttackRelease:handleGameplayAction(false,GLFW_RELEASE);break;
                case TouchCommand::UsePress:handleGameplayAction(true,GLFW_PRESS);break;
                case TouchCommand::UseRelease:handleGameplayAction(true,GLFW_RELEASE);break;
                case TouchCommand::OpenInventory:
                    if(!m_player.isSpectator()) {
                        handleGameplayAction(false,GLFW_RELEASE);
                        handleGameplayAction(true,GLFW_RELEASE);
                        m_touchControls.cancelAll();
                        openInventory();
                    }
                    break;
                case TouchCommand::Pause:
                    handleGameplayAction(false,GLFW_RELEASE);
                    handleGameplayAction(true,GLFW_RELEASE);
                    m_touchControls.cancelAll();m_gameState=GameState::Paused;m_window.setCursorLocked(false);
                    m_activeMenu=std::make_unique<PauseMenu>(m_menuCallbacks,m_localization);break;
                case TouchCommand::SelectHotbar:
                    m_hotbar.selectSlot(command.value);m_player.setSelectedSlot(command.value);
                    m_player.setSelectedCreativeItem(m_hotbar.getSelectedItem());
                    m_player.setSelectedBlock(m_hotbar.getSelectedBlock());break;
            }
        }
    }

    void dispatchUiTouchButton(int button,int action,const glm::vec2& position) {
        const int x=static_cast<int>(position.x),y=static_cast<int>(position.y);
        if(m_inventoryOpen&&(m_player.isSurvival()||m_containerOpen)){
            if(m_containerOpen)m_containerScreen.onMouseButton(button,action,x,y);
            else m_survivalInventory.onMouseButton(button,action,x,y);
        }else if(m_inventoryOpen){
            if(action==GLFW_PRESS)m_inventory.onMouseClick(button,x,y,[this](ItemId id){
                m_hotbar.setSlotItem(m_hotbar.getSelectedSlot(),id);
                m_player.setSelectedCreativeItem(id);m_player.setSelectedBlock(m_hotbar.getSelectedBlock());});
        }else if(m_activeMenu)m_activeMenu->onMouseButton(button,action,position.x,position.y);
        else if(m_playerDead&&action==GLFW_RELEASE)respawnPlayer();
    }

    void dispatchUiTouchMove(const glm::vec2& position) {
        const int x=static_cast<int>(position.x),y=static_cast<int>(position.y);
        if(m_inventoryOpen){m_inventory.onMouseMove(x,y);
            if(m_containerOpen)m_containerScreen.onMouseMove(x,y);
            else if(m_player.isSurvival())m_survivalInventory.onMouseMove(x,y);
        }
        if(m_activeMenu)m_activeMenu->onMouseMove(position.x,position.y);
    }

    void handleUiTouch(const TouchEvent& event,const glm::vec2& position) {
        if(event.phase==TouchPhase::Begin){
            if(m_uiTouch.active)return;
            m_uiTouch={event.id,position,position,Clock::now(),true,false,false,false};
            dispatchUiTouchMove(position);return;
        }
        if(!m_uiTouch.active||event.id!=m_uiTouch.id)return;
        if(event.phase==TouchPhase::Move){
            m_uiTouch.position=position;
            const glm::vec2 delta=position-m_uiTouch.origin;
            const bool scrollSurface=m_activeMenu||(m_inventoryOpen&&!m_player.isSurvival()&&!m_containerOpen);
            if(scrollSurface&&!m_uiTouch.buttonDown&&std::abs(delta.y)>24.0f){
                const double scroll=delta.y>0.0f?-1.0:1.0;
                if(m_activeMenu)m_activeMenu->onScroll(scroll);else m_inventory.onScroll(scroll);
                m_uiTouch.origin=position;m_uiTouch.scrolling=true;
            }else if(!scrollSurface&&!m_uiTouch.buttonDown&&glm::length(delta)>8.0f){
                dispatchUiTouchButton(GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,m_uiTouch.origin);
                m_uiTouch.buttonDown=true;
            }
            dispatchUiTouchMove(position);return;
        }
        if(event.phase==TouchPhase::End){
            if(m_uiTouch.buttonDown)dispatchUiTouchButton(
                m_uiTouch.rightButton?GLFW_MOUSE_BUTTON_RIGHT:GLFW_MOUSE_BUTTON_LEFT,
                GLFW_RELEASE,m_uiTouch.position);
            else if(!m_uiTouch.scrolling){
                dispatchUiTouchButton(GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,m_uiTouch.position);
                dispatchUiTouchButton(GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,m_uiTouch.position);
            }
            m_uiTouch={};
        }
    }

    void updateLongPress() {
        if(!m_uiTouch.active||m_uiTouch.buttonDown||m_uiTouch.scrolling||
           !m_inventoryOpen||(!m_player.isSurvival()&&!m_containerOpen))return;
        if(std::chrono::duration<float>(Clock::now()-m_uiTouch.started).count()<.45f)return;
        dispatchUiTouchButton(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS,m_uiTouch.position);
        m_uiTouch.buttonDown=true;m_uiTouch.rightButton=true;
    }

    void handleTouch(const TouchEvent& event) {
        if(event.phase==TouchPhase::Cancel){
            handleGameplayAction(false,GLFW_RELEASE);
            handleGameplayAction(true,GLFW_RELEASE);
            m_touchControls.cancelAll();m_touchGameplay.clear();
            if(m_uiTouch.active&&m_uiTouch.buttonDown)dispatchUiTouchButton(
                m_uiTouch.rightButton?GLFW_MOUSE_BUTTON_RIGHT:GLFW_MOUSE_BUTTON_LEFT,
                GLFW_RELEASE,m_uiTouch.position);
            m_uiTouch={};return;
        }
        m_touchControls.configure(
            std::max(1,m_window.width()/std::max(1,m_guiScale)),
            std::max(1,m_window.height()/std::max(1,m_guiScale)),touchConfig());
        glm::vec2 position=event.phase==TouchPhase::End&&m_uiTouch.active&&event.id==m_uiTouch.id
            ?m_uiTouch.position:touchToUi(event.x,event.y);
        bool gameplay=false;
        if(event.phase==TouchPhase::Begin){
            gameplay=m_gameState==GameState::Playing&&!m_inventoryOpen&&!m_activeMenu&&
                !m_commandOpen&&!m_playerDead&&m_clientSettings.controlMode!=ControlMode::KeyboardMouse;
            m_touchGameplay[event.id]=gameplay;
        }else{const auto it=m_touchGameplay.find(event.id);gameplay=it!=m_touchGameplay.end()&&it->second;}
        if(gameplay){
            m_touchHudVisible=true;
            TouchEvent converted=event;converted.x=position.x;converted.y=position.y;
            dispatchTouchCommands(m_touchControls.onTouch(converted));
            // Preserve press/release edges even when a quick tap begins and
            // ends within one glfwPollEvents call.
            m_input.clearVirtual();
            m_touchControls.applyTo(m_input);
            m_input.update(m_clientSettings.bindings);
        }else handleUiTouch(event,position);
        if(event.phase==TouchPhase::End)m_touchGameplay.erase(event.id);
    }

    void handleGameplayAction(bool use, int action) {
        const int logicalButton = use ? GLFW_MOUSE_BUTTON_RIGHT : GLFW_MOUSE_BUTTON_LEFT;
        if (action == GLFW_PRESS && use && !m_player.isSpectator()) {
            auto hit = m_world.raycast(m_player.getEyePosition(), m_player.getForward(),
                                       Config::REACH_DISTANCE);
            if (hit) {
                const BlockId target = m_world.getBlock(
                    hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
                if (target == BlockId::CRAFTING_TABLE && m_player.isSurvival()) {
                    openInventory();
                    m_survivalInventory.setCraftingTable(true);
                    return;
                }
                if (target == BlockId::CHEST || target == BlockId::FURNACE) {
                    if (m_containerScreen.open(m_world, hit->blockPos)) {
                        m_containerOpen = true;
                        m_inventoryOpen = true;
                        m_window.setCursorLocked(false);
                        return;
                    }
                }
            }
        }
        m_player.handleMouseButton(logicalButton, action);
    }

    void startGame(const std::string& worldId, bool newWorld) {
        saveCurrentWorld();
        m_saveStore = std::make_unique<SaveStore>(m_worldCatalog.open(worldId));
        m_worldMetadata = m_saveStore->loadMetadata();
        if (m_worldMetadata.generationVersion != WorldGenContext::GENERATION_VERSION)
            throw std::runtime_error("World generation version is incompatible");
        const GameMode mode = m_worldMetadata.gameMode;

        m_gameState = GameState::LoadingWorld;
        m_loadingNewWorld = newWorld;
        m_loadingGenerationComplete = false;
        m_player.configureRules(mode, m_worldMetadata.difficulty);
        m_hotbar.setSurvivalInventory(
            mode == GameMode::Survival ? &m_player.inventory() : nullptr);
        m_player.inventory() = m_worldMetadata.inventory;
        m_player.survivalStats().set(
            m_worldMetadata.health, m_worldMetadata.hunger,
            m_worldMetadata.saturation, m_worldMetadata.exhaustion);
        m_player.setPosition(m_worldMetadata.playerPosition);
        m_dayNightCycle.resetMorning();
        m_weather.reset(m_worldMetadata.seed, m_worldMetadata.weather);
        m_audio.stopRain();
        m_lightningEvents.clear();
        m_particles.clear();
        m_cameraEffects.reset(m_player.getPosition());
        m_window.setCursorLocked(false);
        m_activeMenu.reset();

        m_world.setSaveStore(m_saveStore.get());
        m_entities.setSaveStore(m_saveStore.get());
        LOG_INFO("Loading world with seed " << m_worldMetadata.seed);
        m_world.resetForNewSeed(m_worldMetadata.seed);
        m_entities.clear();
        if (!newWorld) m_entities.loadEntities(m_worldMetadata.entities);
        m_terrainGenerated = false;

        m_autosaveSeconds = 0.0f;
        m_autosavePending = false;
        m_playerDead = false;
        m_commandOpen = false;
        m_commandInput.clear();
        m_survivalTicks = m_worldMetadata.worldTicks;
        m_survivalWorldTickRemainder = 0.0f;

        m_world.update(m_player.getPosition());
        m_world.enqueueGeneration();
        m_worldLoadingStarted = Clock::now();
        LOG_INFO((newWorld ? "Pregenerating new world around spawn"
                           : "Loading existing world around saved position")
                 << " at render distance " << Config::RENDER_DISTANCE);
    }

    void safeSpawn() {
        // Scan down from sky to find ground under player
        int px = static_cast<int>(std::floor(m_player.getPosition().x));
        int pz = static_cast<int>(std::floor(m_player.getPosition().z));

        for (int wy = Config::WORLD_MAX_Y - 1; wy >= Config::WORLD_MIN_Y; --wy) {
            BlockId id = m_world.getBlock(px, wy, pz);
            const BlockProperties& props = getBlockProps(id);
            if (props.solid) {
                float groundY = static_cast<float>(wy + 1);
                auto pos = m_player.getPosition();
                pos.y = groundY + 0.01f;
                m_player.setPosition(pos);
                LOG_INFO("Spawn: ground at y=" << wy
                         << ", player at y=" << pos.y);
                return;
            }
        }

        // No ground found — create a platform
        LOG_INFO("No ground found at spawn, creating platform");
        for (int y = Config::SEA_LEVEL - 4; y <= Config::SEA_LEVEL - 1; ++y) {
            m_world.setBlock(px, y, pz, BlockId::STONE);
        }
        m_world.setBlock(px, Config::SEA_LEVEL, pz, BlockId::GRASS);
        auto pos = m_player.getPosition();
        pos.y = Config::SEA_LEVEL + 1.01f;
        m_player.setPosition(pos);
    }

    void mainLoop() {
        struct VisibleChunk {
            const Chunk* chunk;
            glm::mat4 model;
            float distance2;
        };
        std::vector<VisibleChunk> visibleChunks;
        while (!m_window.shouldClose() && m_running) {
            m_frameTimer.beginFrame();
            auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - m_lastFrame).count();
            m_lastFrame = now;
            dt = std::min(dt, 0.1f);
            if (m_commandMessageSeconds > 0.0f)
                m_commandMessageSeconds =
                    std::max(0.0f, m_commandMessageSeconds - dt);
            if (m_itemNameSeconds > 0.0f)
                m_itemNameSeconds = std::max(0.0f, m_itemNameSeconds - dt);
            if (m_hotbar.getSelectedSlot() != m_lastHudSlot) {
                m_lastHudSlot = m_hotbar.getSelectedSlot();
                m_itemNameSeconds = 2.0f;
            }

            m_input.beginFrame();
            m_input.clearVirtual();
            m_window.pollEvents();
            updateLongPress();
            const int touchWidth = std::max(1, m_window.width() / std::max(1,m_guiScale));
            const int touchHeight = std::max(1, m_window.height() / std::max(1,m_guiScale));
            m_touchControls.configure(touchWidth,touchHeight,touchConfig());
            if (m_clientSettings.controlMode != ControlMode::KeyboardMouse)
                m_touchControls.applyTo(m_input);
            m_input.update(m_clientSettings.bindings);

            // Skip rendering when minimized to save resources
            if (m_window.isMinimized()) {
                m_window.waitEvents(0.1);
                continue;
            }

            // ── Handle input ──────────────────────────────────────────
            if (m_gameState == GameState::Playing && !m_inventoryOpen &&
                !m_commandOpen) {
                double dx, dy;
                m_window.getCursorDelta(dx, dy);
                m_player.handleMouseDelta(static_cast<float>(dx), static_cast<float>(dy),
                    m_clientSettings.mouseSensitivity, m_clientSettings.invertMouseY);
                const glm::vec2 touchLook=m_touchControls.consumeLookDelta();
                m_player.handleMouseDelta(touchLook.x,-touchLook.y,.15f,false);
                if (!m_playerDead) m_player.handleMovement(m_input, dt);
            }

            // Track mouse position (always, for inventory/menu hover)
            {
                if (!m_uiTouch.active) updateMouseScreenPosition();
                else {
                    m_mouseScreenX=m_uiTouch.position.x;
                    m_mouseScreenY=m_uiTouch.position.y;
                }

                // Route to inventory hover if open
                if (m_inventoryOpen) {
                    m_inventory.onMouseMove(
                        static_cast<int>(m_mouseScreenX),
                        static_cast<int>(m_mouseScreenY));
                    if (m_containerOpen) m_containerScreen.onMouseMove(
                        static_cast<int>(m_mouseScreenX), static_cast<int>(m_mouseScreenY));
                    else if (m_player.isSurvival()) m_survivalInventory.onMouseMove(
                        static_cast<int>(m_mouseScreenX), static_cast<int>(m_mouseScreenY));
                }

                // Route to menu hover
                if (m_activeMenu) {
                    m_activeMenu->onMouseMove(m_mouseScreenX, m_mouseScreenY);
                }
            }

            // ── Update ────────────────────────────────────────────────
            m_dayNightCycle.update(
                dt, Config::DAY_CYCLE_MINUTES, m_gameState == GameState::Playing);
            if (m_gameState == GameState::Playing) {
                if (m_containerOpen && (!m_containerScreen.valid())) closeInventory();
                const glm::dvec3 playerEye = m_player.getEyePosition();
                const int rainX = static_cast<int>(std::floor(playerEye.x));
                const int rainY = static_cast<int>(std::floor(playerEye.y));
                const int rainZ = static_cast<int>(std::floor(playerEye.z));
                const bool rainExposure =
                    m_weather.raining() &&
                    m_world.precipitationAt(rainX, rainY, rainZ) ==
                        PrecipitationType::Rain &&
                    m_world.hasSkyAccess(rainX, rainY, rainZ);
                m_player.setRainExposure(rainExposure);
                m_audio.setRainVolume(m_weather.rainGradient() *
                                      (rainExposure ? 0.72f : 0.06f));
                if (!m_playerDead) m_player.update(dt);
                m_cameraEffects.update(m_player.getPosition(), m_player.onGround(),
                                       m_player.isFlying(), dt);
                m_particles.update(m_world, m_player.getPosition(), dt,
                                   m_weather.rainGradient(),
                                   m_worldMetadata.seed ^ m_survivalTicks);
                const bool peaceful =
                    m_player.difficulty() == Difficulty::Peaceful;
                m_entities.update(
                    m_player, dt, m_dayNightCycle.isDay(), peaceful,
                    m_player.isSurvival(), !m_player.isSpectator(),
                    m_weather.thundering(), m_weather.raining());
                for (const glm::dvec3& explosion : m_entities.takeExplosionEvents()) {
                    m_particles.emitExplosion(explosion);
                    const glm::dvec3 delta = explosion - m_player.getPosition();
                    const float distance = static_cast<float>(glm::length(delta));
                    m_audio.playExplosion(
                        std::clamp(static_cast<float>(delta.x) / 24.0f, -1.0f, 1.0f),
                        std::clamp(1.0f - distance / 96.0f, .16f, 1.0f));
                }
                if (m_player.isSurvival() && !m_playerDead &&
                    m_player.survivalStats().dead()) beginPlayerDeath();
                m_survivalWorldTickRemainder += dt * 20.0f;
                while (m_survivalWorldTickRemainder >= 1.0f) {
                    ++m_survivalTicks;
                    m_survivalWorldTickRemainder -= 1.0f;
                    m_weather.tick();
                    tickLightning();
                    m_world.tickBlockEntities();
                    m_world.tickFluids(m_survivalTicks);
                    if ((m_survivalTicks % 20) == 0) {
                        m_world.tickSurvival(
                            m_player.getPosition(), m_survivalTicks,
                            m_weather.raining());
                        m_world.tickWeather(
                            m_weather, m_dayNightCycle.isDay(), m_survivalTicks);
                    }
                    for (const glm::ivec3& position : m_world.takeTntIgnitions())
                        m_entities.primeTnt(position, 4.0f, false);
                }
                for (auto& lightning : m_lightningEvents)
                    lightning.seconds -= dt;
                m_lightningEvents.erase(std::remove_if(
                    m_lightningEvents.begin(), m_lightningEvents.end(),
                    [](const LightningEvent& event) { return event.seconds <= 0.0f; }),
                    m_lightningEvents.end());
                m_world.update(m_player.getPosition());

                // Async generation pipeline: terrain gen → mesh build → GPU upload
                m_world.enqueueGeneration();
                m_world.processCompletedGenerations();
                m_entities.syncChunks();

                // Async mesh building
                m_world.enqueueMeshBuilds();
                m_world.processCompletedMeshes(&m_renderer, Config::MESH_UPLOADS_PER_FRAME);

                // Camera-relative rendering keeps all GPU coordinates near
                // zero even when the logical world position is millions of
                // blocks from spawn.
                const glm::dvec3 eye = m_player.getEyePosition();
                m_camera.setPosition(glm::vec3(
                    0.0f, static_cast<float>(eye.y), 0.0f));
                m_camera.updateVectors(m_player.getYaw(), m_player.getPitch());
                m_autosaveSeconds += dt;
                if (m_autosaveSeconds >= 30.0f) {
                    beginAutosave();
                    m_autosaveSeconds = 0.0f;
                }
                processAutosave();
            } else if (m_gameState == GameState::LoadingWorld) {
                m_world.update(
                    m_player.getPosition(),
                    Config::LOADING_CHUNK_LOADS_PER_FRAME);
                if (!m_loadingGenerationComplete) {
                    m_world.enqueueGeneration();
                    m_world.processCompletedGenerations(false);
                    const auto generation = m_world.generationProgress();
                    if (m_world.streamingTargetReady() && generation.total > 0 &&
                        generation.completed == generation.total &&
                        m_threadPool.idle()) {
                        m_world.processCompletedGenerations();
                        if (m_loadingNewWorld) {
                            m_world.persistGeneratedChunks();
                            safeSpawn();
                            const auto position = m_player.getPosition();
                            m_worldMetadata.playerPosition = position;
                            m_worldMetadata.worldSpawn = glm::ivec3(
                                static_cast<int>(std::floor(position.x)),
                                static_cast<int>(std::floor(position.y)),
                                static_cast<int>(std::floor(position.z)));
                            m_worldMetadata.worldTicks = m_survivalTicks;
                            m_worldMetadata.weather = m_weather.saveState();
                            m_saveStore->saveMetadata(m_worldMetadata);
                        }
                        m_loadingGenerationComplete = true;
                    }
                }
                if (m_loadingGenerationComplete) {
                    m_world.enqueueMeshBuilds(
                        Config::LOADING_MESH_TASKS_IN_FLIGHT);
                    m_world.processCompletedMeshes(
                        &m_renderer, Config::LOADING_MESH_UPLOADS_PER_FRAME,
                        Config::LOADING_MESH_UPLOAD_BYTES_PER_FRAME);
                }
                const auto progress = m_world.loadingProgress();
                if (m_loadingGenerationComplete &&
                    m_world.streamingTargetReady() && progress.total > 0 &&
                    progress.completed == progress.total &&
                    m_threadPool.idle()) {
                    m_terrainGenerated = true;
                    m_gameState = GameState::Playing;
                    m_window.setCursorLocked(true);
                    const float seconds = std::chrono::duration<float>(
                        Clock::now() - m_worldLoadingStarted).count();
                    LOG_INFO("World render target loaded in " << seconds
                             << "s (" << progress.total << " chunks)");
                    LOG_INFO("WASD=move | Mouse=look | Space=jump | Ctrl=sprint");
                    LOG_INFO("Left-click=break | Right-click=place | ESC=pause");
                }
            }

            // ── 3D Rendering ──────────────────────────────────────────
            if (m_gameState == GameState::Playing ||
                m_gameState == GameState::Paused) {
                glm::mat4 view       = m_cameraEffects.viewTransform() *
                                       m_camera.getViewMatrix();
                glm::mat4 projection = m_camera.getProjectionMatrix(m_window.aspectRatio());
                glm::mat4 vp         = projection * view;

                Frustum frustum;
                frustum.extractFromVP(vp);
                float lightningFlash = 0.0f;
                for (const auto& event : m_lightningEvents)
                    lightningFlash = std::max(
                        lightningFlash, std::clamp(event.seconds / 0.2f, 0.0f, 1.0f));
                const RenderEnvironment environment = applyWeather(
                    m_dayNightCycle.evaluate(), m_weather.rainGradient(),
                    m_weather.thunderGradient(), lightningFlash);

                m_renderer.beginFrame();
                m_renderer.renderSky(
                    environment, glm::inverse(vp), m_camera.m_position,
                    m_clientSettings.renderClouds);
                m_renderer.setEnvironment(environment, m_camera.m_position);
                m_renderer.setViewProjection(vp);
                m_renderer.setFrustum(frustum);

                const glm::dvec3 playerPosition = m_player.getPosition();
                const glm::dvec3 renderOrigin(
                    playerPosition.x, 0.0, playerPosition.z);
                if (m_clientSettings.renderClouds) {
                    m_renderer.renderClouds(
                        playerPosition, vp, m_worldMetadata.seed,
                        static_cast<float>(glfwGetTime()),
                        m_clientSettings.cloudRenderDistance);
                }

                // Bind block shader once for all chunks (saves ~N glUseProgram calls)
                m_renderer.bindBlockShader();

                visibleChunks.clear();
                if (visibleChunks.capacity() < m_world.getActiveChunks().size())
                    visibleChunks.reserve(m_world.getActiveChunks().size());
                int rendered = 0;
                for (const auto* chunk : m_world.getActiveChunks()) {
                    const ChunkMesh& mesh = chunk->getMesh();
                    if (!mesh.gpuReady || mesh.indexCount == 0) continue;

                    // Tighter AABB: use actual max block height instead of full chunk height
                    int chunkMaxY = chunk->getGlobalMaxY();
                    glm::vec3 aabbMin(
                        static_cast<float>(chunk->worldX() - renderOrigin.x),
                        static_cast<float>(Config::WORLD_MIN_Y),
                        static_cast<float>(chunk->worldZ() - renderOrigin.z));
                    glm::vec3 aabbMax(aabbMin.x + Config::CHUNK_SIZE_X,
                                      static_cast<float>(chunkMaxY + 1),
                                      aabbMin.z + Config::CHUNK_SIZE_Z);

                    if (!frustum.intersectsAABB(aabbMin, aabbMax)) continue;

                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
                        aabbMin.x, 0.0f, aabbMin.z));
                    glm::vec3 center(
                        aabbMin.x + Config::CHUNK_SIZE_X * 0.5f,
                        (Config::WORLD_MIN_Y + chunkMaxY + 1) * 0.5f,
                        aabbMin.z + Config::CHUNK_SIZE_Z * 0.5f);
                    glm::vec3 delta = center - m_camera.m_position;
                    visibleChunks.push_back({chunk, model, glm::dot(delta, delta)});
                    m_renderer.renderChunk(mesh, model, vp, false);
                    ++rendered;
                }

                std::sort(visibleChunks.begin(), visibleChunks.end(),
                    [](const VisibleChunk& a, const VisibleChunk& b) {
                        return a.distance2 > b.distance2;
                    });
                m_renderer.beginTranslucent();
                for (const auto& visible : visibleChunks) {
                    m_renderer.renderChunk(
                        visible.chunk->getMesh(), visible.model, vp, true);
                }
                m_renderer.endTranslucent();

                m_entities.render(m_renderer, vp, renderOrigin);
                m_particles.buildRenderData(renderOrigin, m_particleRenderData);
                m_renderer.renderParticles(
                    m_particleRenderData, vp,
                    m_camera.right,
                    glm::normalize(glm::cross(m_camera.forward, m_camera.right)),
                    m_weather.rainGradient());

                // Wireframe highlight
                auto highlighted = m_player.getHighlightedBlock();
                if (highlighted) {
                    glm::vec3 pos(
                        static_cast<float>(highlighted->x - renderOrigin.x),
                        static_cast<float>(highlighted->y),
                        static_cast<float>(highlighted->z - renderOrigin.z));
                    m_renderer.renderWireframe(pos, vp);
                }

                // Title bar info
                if (m_gameState == GameState::Playing) {
                    m_titleUpdateSeconds += dt;
                    if (m_titleUpdateSeconds >= 0.25f) {
                        m_titleUpdateSeconds = 0.0f;
                        int fps = dt > 0.0f ? static_cast<int>(1.0f / dt) : 999;
                        m_window.setTitle(
                            "MinecraftC" + (m_player.isFlying()
                                ? " [" + m_localization.text("window.fly") + "]" : "") +
                            " | FPS: " + std::to_string(fps) +
                            " | XYZ: " + std::to_string(static_cast<int>(std::floor(m_player.getPosition().x))) +
                            "," + std::to_string(static_cast<int>(std::floor(m_player.getPosition().y))) +
                            "," + std::to_string(static_cast<int>(std::floor(m_player.getPosition().z))) +
                            " | " + m_localization.text("window.chunks") + ": " +
                            std::to_string(rendered) +
                            "/" + std::to_string(m_world.getActiveChunks().size())
                        );
                    }
                } else {
                    m_window.setTitle(
                        "MinecraftC [" + m_localization.text("window.paused") + "]");
                }
            } else {
                // MainMenu: just clear the screen
                m_renderer.beginFrame();
            }

            // ── UI Rendering ──────────────────────────────────────────
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(m_window.native(), &fbWidth, &fbHeight);
            m_guiScale = effectiveGuiScale(fbWidth, fbHeight, m_clientSettings.guiScale);
            const int uiWidth = std::max(1, fbWidth / m_guiScale);
            const int uiHeight = std::max(1, fbHeight / m_guiScale);

            // Phase 1: Inventory overlay (on top of 3D world)
            if (m_inventoryOpen) {
                m_uiRenderer.beginUIFrame(uiWidth, uiHeight);
                if (m_containerOpen) {
                    m_containerScreen.render(
                        m_uiRenderer, uiWidth, uiHeight, static_cast<int>(m_mouseScreenX),
                        static_cast<int>(m_mouseScreenY));
                } else if (m_player.isSurvival()) {
                    m_survivalInventory.render(m_uiRenderer, uiWidth, uiHeight,
                        static_cast<int>(m_mouseScreenX), static_cast<int>(m_mouseScreenY));
                } else {
                    m_inventory.render(m_uiRenderer, uiWidth, uiHeight,
                                       static_cast<int>(m_mouseScreenX),
                                       static_cast<int>(m_mouseScreenY));
                }
                m_uiRenderer.endUIFrame();
            }

            // Phase 2: Hotbar HUD (Playing, no inventory, no menu)
            if (m_gameState == GameState::Playing && !m_inventoryOpen && !m_activeMenu) {
                m_uiRenderer.beginUIFrame(uiWidth, uiHeight);
                if (!m_player.isSpectator()) {
                    m_hotbar.render(m_uiRenderer, uiWidth, uiHeight);
                    if (m_player.isSurvival()) renderSurvivalHud(uiWidth);
                    renderCrosshairAndMiningProgress(uiWidth, uiHeight);
                    if (m_itemNameSeconds > 0.0f) renderSelectedItemName(uiWidth);
                }
                if (m_clientSettings.controlMode == ControlMode::Touch ||
                    (m_clientSettings.controlMode == ControlMode::Auto && m_touchHudVisible))
                    m_touchControls.render(m_uiRenderer);
                m_uiRenderer.endUIFrame();
            }

            if (m_commandOpen || m_commandMessageSeconds > 0.0f) {
                m_uiRenderer.beginUIFrame(uiWidth, uiHeight);
                const std::string text = m_commandOpen
                    ? "> " + m_commandInput + "_"
                    : m_commandMessage;
                m_uiRenderer.drawRect(
                    12.0f, 18.0f, static_cast<float>(uiWidth - 24), 36.0f,
                    glm::vec4(0.02f, 0.02f, 0.03f, 0.82f));
                m_uiRenderer.renderText(
                    text, 20.0f, 27.0f, 1.25f,
                    m_commandOpen ? glm::vec3(1.0f)
                                  : glm::vec3(1.0f, 0.82f, 0.35f));
                m_uiRenderer.endUIFrame();
            }

            // Phase 3: Active menu (overlays everything)
            if (m_activeMenu) {
                m_uiRenderer.beginUIFrame(uiWidth, uiHeight);
                m_activeMenu->render(m_uiRenderer, uiWidth, uiHeight);
                m_uiRenderer.endUIFrame();
            }

            if (m_gameState == GameState::LoadingWorld) {
                const auto progress = m_loadingGenerationComplete
                    ? m_world.loadingProgress() : m_world.generationProgress();
                const float phaseFraction = progress.total == 0 ? 0.0f :
                    static_cast<float>(progress.completed) /
                    static_cast<float>(progress.total);
                const float fraction = m_loadingGenerationComplete
                    ? 0.75f + phaseFraction * 0.25f : phaseFraction * 0.75f;
                m_uiRenderer.beginUIFrame(uiWidth, uiHeight);
                m_uiRenderer.drawRect(0, 0, static_cast<float>(uiWidth),
                                      static_cast<float>(uiHeight),
                                      glm::vec4(.055f, .065f, .08f, 1.0f));
                const std::string title = m_localization.text("loading.title");
                const auto titleSize = m_uiRenderer.measureText(title, 3.0f);
                m_uiRenderer.renderText(title, (uiWidth - titleSize.x) * 0.5f,
                                        uiHeight * 0.58f, 3.0f,
                                        glm::vec3(1.0f, .85f, .3f));
                const float barWidth = std::min(420.0f, uiWidth - 48.0f);
                const float barX = (uiWidth - barWidth) * 0.5f;
                const float barY = uiHeight * 0.46f;
                m_uiRenderer.drawRect(barX - 2, barY - 2, barWidth + 4, 18,
                                      glm::vec4(.02f, .02f, .025f, 1.0f));
                m_uiRenderer.drawRect(barX, barY, barWidth, 14,
                                      glm::vec4(.18f, .18f, .2f, 1.0f));
                m_uiRenderer.drawRect(barX, barY, barWidth * fraction, 14,
                                      glm::vec4(.36f, .72f, .3f, 1.0f));
                const std::string status = m_localization.format(
                    m_loadingGenerationComplete ? "loading.preparing" :
                    (m_loadingNewWorld ? "loading.generating"
                                       : "loading.cached"), {
                    std::to_string(progress.completed), std::to_string(progress.total)});
                const auto statusSize = m_uiRenderer.measureText(status, 1.25f);
                m_uiRenderer.renderText(status, (uiWidth - statusSize.x) * 0.5f,
                                        barY - 28.0f, 1.25f, glm::vec3(.82f));
                m_uiRenderer.endUIFrame();
            }

            if (m_playerDead) {
                m_uiRenderer.beginUIFrame(uiWidth, uiHeight);
                m_uiRenderer.drawRect(0, 0, static_cast<float>(uiWidth),
                                      static_cast<float>(uiHeight),
                                      glm::vec4(0.28f, 0.0f, 0.0f, 0.62f));
                const std::string title = m_localization.text("death.title");
                auto titleSize = m_uiRenderer.measureText(title, 4.0f);
                m_uiRenderer.renderText(title, (uiWidth - titleSize.x) * 0.5f,
                                        uiHeight * 0.58f, 4.0f,
                                        glm::vec3(1.0f, 0.82f, 0.82f));
                const std::string prompt = m_localization.text("death.respawn");
                auto promptSize = m_uiRenderer.measureText(prompt, 1.5f);
                m_uiRenderer.renderText(prompt, (uiWidth - promptSize.x) * 0.5f,
                                        uiHeight * 0.46f, 1.5f, glm::vec3(1.0f));
                m_uiRenderer.endUIFrame();
            }

            // ── Finish frame ──────────────────────────────────────────
            m_renderer.endFrame();
            m_window.swapBuffers();
            m_frameTimer.endFrame();

            // Alt+F4 to quit
            if (m_window.isKeyPressed(GLFW_KEY_F4) &&
                (m_keys[GLFW_KEY_LEFT_ALT] || m_keys[GLFW_KEY_RIGHT_ALT])) {
                m_running = false;
            }
        }
    }

    void tickLightning() {
        if (!m_weather.thundering()) return;
        auto hash = [](uint64_t value) {
            value ^= value >> 30;
            value *= 0xbf58476d1ce4e5b9ULL;
            value ^= value >> 27;
            value *= 0x94d049bb133111ebULL;
            return value ^ (value >> 31);
        };
        for (const Chunk* chunk : m_world.getActiveChunks()) {
            if (!chunk->generated.load()) continue;
            uint64_t random = m_worldMetadata.seed ^ m_survivalTicks * 131ULL;
            random ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk->cx));
            random ^= static_cast<uint64_t>(static_cast<uint32_t>(chunk->cz)) << 32;
            random = hash(random);
            if (random % 100000 != 0) continue;
            const int x = chunk->worldX() + static_cast<int>((random >> 17) % 16);
            const int z = chunk->worldZ() + static_cast<int>((random >> 25) % 16);
            const int surface = m_world.getSurfaceY(x, z);
            if (!Config::isValidWorldY(surface)) continue;
            const BlockId top = m_world.getBlock(x, surface, z);
            int strikeY = surface + 1;
            if (top == BlockId::SNOW_LAYER || top == BlockId::FIRE) strikeY = surface;
            if (!Config::isValidWorldY(strikeY)) continue;
            const glm::ivec3 strike{x, strikeY, z};
            m_entities.strikeLightning(m_player, strike);
            const glm::dvec3 delta = glm::dvec3(strike) - m_player.getPosition();
            const float distance = static_cast<float>(glm::length(delta));
            m_audio.playThunder(
                std::clamp(static_cast<float>(delta.x) / 32.0f, -1.0f, 1.0f),
                std::clamp(1.0f - distance / 160.0f, 0.18f, 1.0f));
            if (m_world.getBlock(x, strikeY, z) == BlockId::AIR ||
                m_world.getBlock(x, strikeY, z) == BlockId::SNOW_LAYER)
                m_world.setBlock(x, strikeY, z, BlockId::FIRE);
            m_lightningEvents.push_back({glm::dvec3(strike), 0.5f});
            m_particles.appendLightning(glm::dvec3(strike));
        }
    }

    void openInventory() {
        if (m_player.isSpectator()) return;
        if (m_player.isSurvival() && !m_inventoryOpen)
            m_survivalInventory.setCraftingTable(false);
        m_containerOpen = false;
        m_inventoryOpen = true;
        m_window.setCursorLocked(false);
    }

    void closeCommandInput() {
        m_commandOpen = false;
        m_commandInput.clear();
        m_suppressCommandChar = false;
        if (m_gameState == GameState::Playing) m_window.setCursorLocked(true);
    }

    void showCommandMessage(const std::string& message) {
        m_commandMessage = message;
        m_commandMessageSeconds = 4.0f;
    }

    void executeCommand() {
        const std::string submitted = m_commandInput;
        closeCommandInput();
        if (!m_worldMetadata.cheatsEnabled) {
            showCommandMessage(m_localization.text("message.cheats_disabled"));
            return;
        }
        const auto mode = parseGamemodeCommand(submitted);
        if (mode) {
            m_player.configureRules(*mode, m_worldMetadata.difficulty);
            m_worldMetadata.gameMode = *mode;
            m_hotbar.setSurvivalInventory(
                *mode == GameMode::Survival ? &m_player.inventory() : nullptr);
            const std::string name = m_localization.text(
                *mode == GameMode::Survival ? "common.survival" :
                *mode == GameMode::Creative ? "common.creative" : "common.spectator");
            showCommandMessage(m_localization.format("message.mode_changed", {name}));
            return;
        }
        const auto target = parseTeleportCommand(submitted);
        if (target) {
            m_player.teleport({target->x, target->y, target->z});
            m_world.update(m_player.getPosition());
            m_world.enqueueGeneration();
            showCommandMessage(m_localization.format("message.teleported", {
                std::to_string(target->x), std::to_string(target->y),
                std::to_string(target->z)}));
            return;
        }
        const auto time = parseTimeSetCommand(submitted);
        if (time) {
            if (*time == TimePreset::Day) {
                m_dayNightCycle.setDay();
                showCommandMessage(m_localization.text("message.time_day"));
            } else {
                m_dayNightCycle.setNight();
                showCommandMessage(m_localization.text("message.time_night"));
            }
            return;
        }
        const auto weather = parseWeatherCommand(submitted);
        if (weather) {
            m_weather.setWeather(*weather);
            showCommandMessage(m_localization.text(
                *weather == WeatherType::Clear ? "message.weather_clear" :
                *weather == WeatherType::Rain ? "message.weather_rain" :
                "message.weather_thunder"));
            return;
        }
        showCommandMessage(m_localization.text("message.command_usage"));
    }

    void updateMouseScreenPosition() {
        double windowX = 0.0;
        double windowY = 0.0;
        m_window.getCursorPos(windowX, windowY);

        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetWindowSize(m_window.native(), &windowWidth, &windowHeight);
        glfwGetFramebufferSize(
            m_window.native(), &framebufferWidth, &framebufferHeight);

        const double scaleX = windowWidth > 0
            ? static_cast<double>(framebufferWidth) / windowWidth : 1.0;
        const double scaleY = windowHeight > 0
            ? static_cast<double>(framebufferHeight) / windowHeight : 1.0;
        const double uiScale = std::max(1, m_guiScale);
        m_mouseScreenX = windowX * scaleX / uiScale;
        m_mouseScreenY =
            (static_cast<double>(framebufferHeight) - windowY * scaleY) / uiScale;
    }

    void renderSurvivalHud(int screenWidth) {
        const auto& stats = m_player.survivalStats();
        constexpr float unitW = 12.0f;
        constexpr float unitH = 10.0f;
        constexpr float gap = 2.0f;
        constexpr float y = 76.0f;
        const float groupW = 10.0f * unitW + 9.0f * gap;
        const float leftX = screenWidth * 0.5f - groupW - 10.0f;
        const float rightX = screenWidth * 0.5f + 10.0f;
        for (int i = 0; i < 10; ++i) {
            const float healthFill = std::clamp(stats.health() - i * 2.0f, 0.0f, 2.0f) * 0.5f;
            const float hungerFill = std::clamp(
                static_cast<float>(stats.hunger()) - i * 2.0f, 0.0f, 2.0f) * 0.5f;
            const float hx = leftX + i * (unitW + gap);
            const float fx = rightX + (9 - i) * (unitW + gap);
            auto heart = [&](float x, float fill) {
                m_uiRenderer.drawRect(x+2,y+2,8,7,{.20f,.03f,.04f,.95f});
                m_uiRenderer.drawRect(x+1,y+5,10,4,{.20f,.03f,.04f,.95f});
                if (fill > 0) {
                    const float width = fill < 1 ? 5.0f : 10.0f;
                    m_uiRenderer.drawRect(x+1,y+5,width,4,{.90f,.07f,.10f,1});
                    m_uiRenderer.drawRect(x+2,y+2,std::max(0.0f,width-2),3,{.90f,.07f,.10f,1});
                }
            };
            auto food = [&](float x, float fill) {
                m_uiRenderer.drawRect(x+3,y+1,7,8,{.18f,.08f,.02f,.95f});
                m_uiRenderer.drawRect(x+1,y+3,4,5,{.18f,.08f,.02f,.95f});
                if (fill > 0) m_uiRenderer.drawRect(x+(fill<1?6:2),y+3,
                    fill<1?4:8,5,{.90f,.46f,.06f,1});
            };
            heart(hx, healthFill);
            food(fx, hungerFill);
        }
        const int armor = totalArmorPoints(m_player.inventory());
        if (armor > 0) {
            constexpr float armorY = y + 14.0f;
            for (int i = 0; i < 10; ++i) {
                const float fill = std::clamp((armor - i * 2) * 0.5f, 0.0f, 1.0f);
                const float x = leftX + i * (unitW + gap);
                m_uiRenderer.drawRect(x, armorY, unitW, unitH,
                                      glm::vec4(0.08f, 0.10f, 0.13f, 0.9f));
                m_uiRenderer.drawRect(x, armorY, unitW * fill, unitH,
                                      glm::vec4(0.62f, 0.72f, 0.82f, 1.0f));
            }
        }
        if (m_player.underwater()) {
            const int bubbles = static_cast<int>(std::ceil(m_player.airFraction() * 10.0f));
            for (int i=0;i<10;++i) {
                const float x = rightX + (9-i)*(unitW+gap);
                m_uiRenderer.drawRect(x+2,y+15,8,8,{.06f,.18f,.25f,.9f});
                m_uiRenderer.drawRect(x+4,y+17,4,4,
                    i < bubbles ? glm::vec4(.45f,.82f,1,1) : glm::vec4(.08f,.12f,.16f,1));
            }
        }
    }

    void renderSelectedItemName(int screenWidth) {
        std::string name;
        if (m_player.isSurvival()) {
            const auto& stack = m_player.inventory().slot(
                static_cast<size_t>(m_hotbar.getSelectedSlot()));
            if (!stack.empty()) name = m_localization.itemName(stack.id);
        } else name = m_localization.itemName(m_hotbar.getSelectedItem());
        if (name.empty()) return;
        const auto size = m_uiRenderer.measureText(name, 1.0f);
        const float x = (screenWidth - size.x) * .5f;
        m_uiRenderer.renderText(name, x+1, 66, 1.0f, {.05f,.05f,.05f});
        m_uiRenderer.renderText(name, x, 67, 1.0f, {.95f,.95f,.95f});
    }

    void renderCrosshairAndMiningProgress(int screenWidth, int screenHeight) {
        const float centerX = static_cast<float>(screenWidth) * 0.5f;
        const float centerY = static_cast<float>(screenHeight) * 0.5f;
        constexpr float armLength = 8.0f;
        constexpr float thickness = 2.0f;
        constexpr float centerGap = 3.0f;

        const glm::vec4 shadow(0.0f, 0.0f, 0.0f, 0.85f);
        const glm::vec4 foreground(1.0f, 1.0f, 1.0f, 0.95f);
        auto drawCrossPart = [&](float x, float y, float width, float height) {
            m_uiRenderer.drawRect(x - 1.0f, y - 1.0f,
                                  width + 2.0f, height + 2.0f, shadow);
            m_uiRenderer.drawRect(x, y, width, height, foreground);
        };
        drawCrossPart(centerX - centerGap - armLength, centerY - thickness * 0.5f,
                      armLength, thickness);
        drawCrossPart(centerX + centerGap, centerY - thickness * 0.5f,
                      armLength, thickness);
        drawCrossPart(centerX - thickness * 0.5f, centerY + centerGap,
                      thickness, armLength);
        drawCrossPart(centerX - thickness * 0.5f,
                      centerY - centerGap - armLength, thickness, armLength);

        const float progress = m_player.getMiningProgress();
        if (progress <= 0.0f) return;
        constexpr float barWidth = 112.0f;
        constexpr float barHeight = 8.0f;
        const float barX = centerX - barWidth * 0.5f;
        const float barY = centerY - 42.0f;
        m_uiRenderer.drawRect(barX - 2.0f, barY - 2.0f,
                              barWidth + 4.0f, barHeight + 4.0f,
                              glm::vec4(0.0f, 0.0f, 0.0f, 0.82f));
        m_uiRenderer.drawRect(barX, barY, barWidth, barHeight,
                              glm::vec4(0.18f, 0.18f, 0.20f, 0.92f));
        m_uiRenderer.drawRect(barX, barY, barWidth * progress, barHeight,
                              glm::vec4(0.92f, 0.74f, 0.25f, 1.0f));
    }

    void beginPlayerDeath() {
        m_playerDead = true;
        const glm::vec3 deathPosition = glm::vec3(
            m_player.getPosition() + glm::dvec3(0.0, 0.5, 0.0));
        for (const auto& stack : takeDeathDrops(m_player.inventory()))
            m_entities.spawnItem(deathPosition, stack);
        m_window.setCursorLocked(false);
    }

    void respawnPlayer() {
        const bool bedValid = m_worldMetadata.bedSpawn &&
            m_world.getBlock(m_worldMetadata.bedSpawn->x, m_worldMetadata.bedSpawn->y,
                             m_worldMetadata.bedSpawn->z) == BlockId::WHITE_BED;
        const glm::ivec3 spawn = chooseRespawnPosition(
            m_worldMetadata.worldSpawn, m_worldMetadata.bedSpawn, bedValid);
        m_player.setPosition(glm::vec3(spawn) + glm::vec3(0.5f, 1.01f, 0.5f));
        m_player.survivalStats().resetAfterRespawn();
        m_player.extinguish();
        m_player.resetDamageImmunity();
        m_cameraEffects.reset(m_player.getPosition());
        m_world.update(m_player.getPosition());
        m_world.enqueueGeneration();
        m_world.waitForInitialGeneration(150);
        m_world.processCompletedGenerations();
        m_playerDead = false;
        m_window.setCursorLocked(true);
    }

    void closeInventory() {
        if (m_containerOpen) {
            m_containerScreen.close([this](ItemStack stack) {
                m_entities.spawnItem(m_player.getPosition() + glm::dvec3(0.0, 0.5, 0.0), stack);
            });
        } else if (m_player.isSurvival()) m_survivalInventory.onClose();
        m_containerOpen = false;
        m_inventoryOpen = false;
        m_window.setCursorLocked(true);
    }

    void cleanup() {
        saveCurrentWorld();
        Debug::Log::shutdown();
        // Resources cleaned up by destructors
    }

    void updateSaveMetadata() {
        m_worldMetadata.playerPosition = m_player.getPosition();
        m_worldMetadata.inventory = m_player.inventory();
        m_worldMetadata.health = m_player.survivalStats().health();
        m_worldMetadata.hunger = m_player.survivalStats().hunger();
        m_worldMetadata.saturation = m_player.survivalStats().saturation();
        m_worldMetadata.exhaustion = m_player.survivalStats().exhaustion();
        m_worldMetadata.worldTicks = m_survivalTicks;
        m_worldMetadata.weather = m_weather.saveState();
        m_worldMetadata.entities.clear();
    }

    void beginAutosave() {
        if (!m_saveStore || !m_terrainGenerated || m_autosavePending) return;
        try {
            updateSaveMetadata();
            m_saveStore->saveMetadata(m_worldMetadata);
            m_entities.beginChunkEntityAutosave();
            m_world.beginModifiedChunkAutosave();
            m_autosavePending = m_world.hasPendingModifiedChunkSaves() ||
                                m_entities.hasPendingChunkEntitySaves();
            m_autosaveEntityTurn = true;
        } catch (const std::exception& error) {
            LOG_ERROR("Autosave metadata failed: " << error.what());
            showCommandMessage(m_localization.text("message.autosave_log"));
        }
    }

    void processAutosave() {
        if (!m_autosavePending) return;
        try {
            if (m_autosaveEntityTurn &&
                m_entities.hasPendingChunkEntitySaves()) {
                m_entities.flushChunkEntities(1);
            } else if (m_world.hasPendingModifiedChunkSaves()) {
                m_world.flushModifiedChunks(1);
            } else if (m_entities.hasPendingChunkEntitySaves()) {
                m_entities.flushChunkEntities(1);
            }
            m_autosaveEntityTurn = !m_autosaveEntityTurn;
            m_autosavePending = m_world.hasPendingModifiedChunkSaves() ||
                                m_entities.hasPendingChunkEntitySaves();
        } catch (const std::exception& error) {
            m_autosavePending = false;
            LOG_ERROR("Autosave chunk flush failed: " << error.what());
            showCommandMessage(m_localization.text("message.autosave_retry"));
        }
    }

    void saveCurrentWorld() {
        if (!m_saveStore || !m_terrainGenerated) return;
        try {
            updateSaveMetadata();
            m_entities.beginChunkEntityAutosave();
            m_world.beginModifiedChunkAutosave();
            m_entities.flushChunkEntities(
                std::numeric_limits<size_t>::max(), true);
            m_world.flushModifiedChunks();
            m_saveStore->saveMetadata(m_worldMetadata);
            m_autosavePending = false;
        } catch (const std::exception& error) {
            LOG_ERROR("Could not save world: " << error.what());
            showCommandMessage(m_localization.text("message.save_log"));
        }
    }
};

int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            const std::string argument(argv[1]);
            if (argument == "--version") {
                std::cout << "MinecraftC " << Config::GAME_VERSION << '\n';
                return 0;
            }
            if (argument == "--help" || argument == "-h") {
                std::cout << "MinecraftC " << Config::GAME_VERSION << "\n"
                          << "Usage: minecraftc [--help] [--version]\n"
                          << "Worlds and settings are stored in the platform user-data directory.\n";
                return 0;
            }
        }
        RuntimePaths paths = discoverRuntimePaths(argc > 0 ? argv[0] : nullptr);
        Application app(std::move(paths));
        return app.run();
    } catch (const std::exception& error) {
        std::cerr << "MinecraftC startup failed: " << error.what() << '\n';
        return 1;
    }
}
