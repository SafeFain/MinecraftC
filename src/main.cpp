#include "core/Window.h"
#include "renderer/Renderer.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "Config.h"
#include "world/World.h"
#include "player/Player.h"
#include "threading/ThreadPool.h"
#include "ui/UIRenderer.h"
#include "ui/Menu.h"
#include "ui/SettingsMenu.h"
#include "ui/Hotbar.h"
#include "ui/Inventory.h"
#include "debug/Log.h"
#include "debug/CrashHandler.h"
#include <glad/glad.h>

#include <stdexcept>
#include <chrono>
#include <cmath>

class Application {
public:
    Application()
        : m_camera(Config::FOV, Config::NEAR_PLANE, Config::FAR_PLANE)
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
    Window      m_window{Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, "MinecraftC"};
    Renderer    m_renderer;
    Camera      m_camera;
    ThreadPool  m_threadPool;
    World       m_world;
    Player      m_player{m_world};
    bool        m_running = true;

    // ── UI / State ────────────────────────────────────────────────────
    GameState             m_gameState = GameState::MainMenu;
    UIRenderer            m_uiRenderer;
    std::unique_ptr<Menu> m_activeMenu;
    MenuCallbacks         m_menuCallbacks;
    bool                  m_terrainGenerated = false;

    Hotbar                m_hotbar;
    CreativeInventory     m_inventory;
    bool                  m_inventoryOpen = false;
    double                m_mouseScreenX = 0.0;
    double                m_mouseScreenY = 0.0;

    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_lastFrame;

    // Key state tracking
    bool m_keys[512] = {};

    void initialize() {
        // ── Debug infrastructure ────────────────────────────────────────
        Debug::Log::init(Debug::LogLevel::Trace, Config::LogConfig::FILE_OUTPUT,
                         Config::LogConfig::LOG_PATH);
        Debug::installCrashHandlers();

        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
            throw std::runtime_error("Failed to load OpenGL functions");
        }

        // Set initial viewport (framebuffer size already queried in Window constructor)
        glViewport(0, 0, m_window.width(), m_window.height());

        m_renderer.initialize();
        m_uiRenderer.initialize();

        // Start with cursor visible (main menu)
        m_window.setCursorLocked(false);

        // Set up thread pool for async mesh building
        m_world.setThreadPool(&m_threadPool);

        // ── Menu callbacks ────────────────────────────────────────────
        m_menuCallbacks.onStartGame = [this]() { startGame(); };
        m_menuCallbacks.onResume = [this]() {
            m_gameState = GameState::Playing;
            m_window.setCursorLocked(true);
            m_activeMenu.reset();
        };
        m_menuCallbacks.onBackToMenu = [this]() {
            m_gameState = GameState::MainMenu;
            m_window.setCursorLocked(false);
            m_activeMenu = std::make_unique<MainMenu>(m_menuCallbacks);
        };
        m_menuCallbacks.onQuit = [this]() { m_running = false; };

        m_menuCallbacks.onOpenSettings = [this]() {
            // Save current state to restore the correct menu on back
            GameState prevState = m_gameState;
            MenuCallbacks prevCallbacks = m_menuCallbacks;
            m_activeMenu = std::make_unique<SettingsMenu>([this, prevState, prevCallbacks]() {
                m_gameState = prevState;
                if (prevState == GameState::Paused) {
                    m_activeMenu = std::make_unique<PauseMenu>(prevCallbacks);
                } else {
                    m_activeMenu = std::make_unique<MainMenu>(prevCallbacks);
                }
            });
        };

        // ── Input callbacks ───────────────────────────────────────────
        m_window.setKeyCallback([this](int key, int /*scancode*/, int action, int /*mods*/) {
            // Track key state for polling
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key < 512) m_keys[key] = true;
            } else if (action == GLFW_RELEASE) {
                if (key < 512) m_keys[key] = false;
            }

            // E key — toggle creative inventory (Playing only, no menu active)
            if (key == GLFW_KEY_E && action == GLFW_PRESS) {
                if (m_gameState == GameState::Playing && !m_activeMenu) {
                    if (m_inventoryOpen) {
                        closeInventory();
                    } else {
                        openInventory();
                    }
                    return;
                }
            }

            // Number keys 1-9 — hotbar selection (Playing only)
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && action == GLFW_PRESS) {
                if (m_gameState == GameState::Playing) {
                    m_hotbar.onKeyPress(key);
                    m_player.setSelectedBlock(m_hotbar.getSelectedBlock());
                }
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
                    m_activeMenu = std::make_unique<PauseMenu>(m_menuCallbacks);
                } else if (m_gameState == GameState::Paused) {
                    // Resume (ESC in pause menu handled by menu itself)
                    // But just in case the menu hasn't handled it:
                    m_menuCallbacks.onResume();
                }
                // In MainMenu, ESC does nothing
                return;
            }

            // Route to active menu for key presses
            if (action == GLFW_PRESS && m_activeMenu) {
                m_activeMenu->onKeyPress(key);
            }
        });

        m_window.setMouseButtonCallback([this](int button, int action, int /*mods*/) {
            if (action == GLFW_PRESS) {
                if (m_inventoryOpen) {
                    // Click in inventory grid
                    m_inventory.onMouseClick(button,
                        static_cast<int>(m_mouseScreenX),
                        static_cast<int>(m_mouseScreenY),
                        [this](BlockId id) {
                            // Select block: update hotbar slot + player
                            m_hotbar.setSlotBlock(m_hotbar.getSelectedSlot(), id);
                            m_player.setSelectedBlock(id);
                        });
                } else if (m_gameState == GameState::Playing) {
                    m_player.handleMouseButton(button);
                } else if (m_activeMenu) {
                    m_activeMenu->onMouseClick(button);
                }
            }
        });

        m_window.setScrollCallback([this](double /*xoffset*/, double yoffset) {
            if (m_gameState == GameState::Playing && !m_activeMenu && !m_inventoryOpen) {
                m_hotbar.onScroll(yoffset);
                m_player.setSelectedBlock(m_hotbar.getSelectedBlock());
            }
        });

        // ── Show main menu ────────────────────────────────────────────
        m_activeMenu = std::make_unique<MainMenu>(m_menuCallbacks);

        m_lastFrame = Clock::now();

        LOG_INFO("MinecraftC initialized");
        LOG_INFO("OpenGL: " << glGetString(GL_VERSION));
        LOG_INFO("Renderer: " << glGetString(GL_RENDERER));
    }

    void startGame() {
        m_gameState = GameState::Playing;
        m_window.setCursorLocked(true);
        m_activeMenu.reset();

        // If returning from menu, regenerate world with current seed
        if (m_terrainGenerated) {
            LOG_INFO("Regenerating world with seed " << Config::WORLD_SEED);
            m_world.resetForNewSeed(Config::WORLD_SEED);
            m_terrainGenerated = false;
        }

        if (!m_terrainGenerated) {
            // Terrain generation — first batch: create chunks, generate async, wait briefly
            auto t0 = Clock::now();
            m_world.update(m_player.getPosition());
            m_world.enqueueGeneration();
            m_world.waitForInitialGeneration(150);  // wait up to 150ms for first gen wave
            m_world.processCompletedGenerations();
            m_world.buildMeshesSync(&m_renderer, 16);
            auto t1 = Clock::now();
            float genTime = std::chrono::duration<float>(t1 - t0).count();
            LOG_INFO("Seed: " << Config::WORLD_SEED);
            LOG_INFO("Chunks: " << m_world.getActiveChunks().size());
            LOG_INFO("Terrain generated in " << genTime << "s");
            LOG_INFO("Thread pool: " << m_threadPool.threadCount() << " workers");

            safeSpawn();
            m_terrainGenerated = true;
        }

        LOG_INFO("WASD=move | Mouse=look | Space=jump | Ctrl=sprint");
        LOG_INFO("Left-click=break | Right-click=place | ESC=pause");
    }

    void safeSpawn() {
        // Scan down from sky to find ground under player
        int px = static_cast<int>(std::floor(m_player.getPosition().x));
        int pz = static_cast<int>(std::floor(m_player.getPosition().z));

        for (int wy = Config::CHUNK_SIZE_Y - 1; wy >= 0; --wy) {
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
        for (int y = 30; y <= 33; ++y) {
            m_world.setBlock(px, y, pz, BlockId::STONE);
        }
        m_world.setBlock(px, 34, pz, BlockId::GRASS);
        auto pos = m_player.getPosition();
        pos.y = 34.01f;
        m_player.setPosition(pos);
    }

    void mainLoop() {
        while (!m_window.shouldClose() && m_running) {
            auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - m_lastFrame).count();
            m_lastFrame = now;
            dt = std::min(dt, 0.1f);

            m_window.pollEvents();

            // Skip rendering when minimized to save resources
            if (m_window.isMinimized()) {
                continue;
            }

            // ── Handle input ──────────────────────────────────────────
            if (m_gameState == GameState::Playing && !m_inventoryOpen) {
                double dx, dy;
                m_window.getCursorDelta(dx, dy);
                m_player.handleMouseDelta(static_cast<float>(dx), static_cast<float>(dy));
                m_player.handleMovement(m_keys, dt);
            }

            // Track mouse position (always, for inventory/menu hover)
            {
                double mx, my;
                m_window.getCursorPos(mx, my);
                int fbWidth, fbHeight;
                glfwGetFramebufferSize(m_window.native(), &fbWidth, &fbHeight);
                m_mouseScreenX = mx;
                m_mouseScreenY = static_cast<double>(fbHeight) - my;

                // Route to inventory hover if open
                if (m_inventoryOpen) {
                    m_inventory.onMouseMove(
                        static_cast<int>(m_mouseScreenX),
                        static_cast<int>(m_mouseScreenY));
                }

                // Route to menu hover
                if (m_activeMenu) {
                    m_activeMenu->onMouseMove(m_mouseScreenX, m_mouseScreenY);
                }
            }

            // ── Update ────────────────────────────────────────────────
            if (m_gameState == GameState::Playing) {
                m_player.update(dt);
                m_world.update(m_player.getPosition());

                // Async generation pipeline: terrain gen → mesh build → GPU upload
                m_world.enqueueGeneration();
                m_world.processCompletedGenerations();

                // Async mesh building
                m_world.enqueueMeshBuilds();
                m_world.processCompletedMeshes(&m_renderer, Config::MESH_UPLOADS_PER_FRAME);

                // Sync camera to player
                m_camera.setPosition(m_player.getEyePosition());
                m_camera.updateVectors(m_player.getYaw(), m_player.getPitch());
            }

            // ── 3D Rendering ──────────────────────────────────────────
            if (m_gameState == GameState::Playing ||
                m_gameState == GameState::Paused) {
                glm::mat4 view       = m_camera.getViewMatrix();
                glm::mat4 projection = m_camera.getProjectionMatrix(m_window.aspectRatio());
                glm::mat4 vp         = projection * view;

                Frustum frustum;
                frustum.extractFromVP(vp);

                m_renderer.beginFrame();
                m_renderer.setViewProjection(vp);
                m_renderer.setFrustum(frustum);

                // Bind block shader once for all chunks (saves ~N glUseProgram calls)
                m_renderer.bindBlockShader();

                int rendered = 0;
                for (const auto* chunk : m_world.getActiveChunks()) {
                    const ChunkMesh& mesh = chunk->getMesh();
                    if (!mesh.gpuReady || mesh.indexCount == 0) continue;

                    // Tighter AABB: use actual max block height instead of full chunk height
                    int chunkMaxY = chunk->getGlobalMaxY();
                    glm::vec3 aabbMin(chunk->worldX(), 0.0f, chunk->worldZ());
                    glm::vec3 aabbMax(aabbMin.x + Config::CHUNK_SIZE_X,
                                      static_cast<float>(chunkMaxY + 1),
                                      aabbMin.z + Config::CHUNK_SIZE_Z);

                    if (!frustum.intersectsAABB(aabbMin, aabbMax)) continue;

                    glm::mat4 model = glm::translate(glm::mat4(1.0f), aabbMin);
                    m_renderer.renderChunk(mesh, model, vp);
                    ++rendered;
                }

                // Wireframe highlight
                auto highlighted = m_player.getHighlightedBlock();
                if (highlighted) {
                    glm::vec3 pos(highlighted->x, highlighted->y, highlighted->z);
                    m_renderer.renderWireframe(pos, vp);
                }

                // Title bar info
                if (m_gameState == GameState::Playing) {
                    int fps = dt > 0.0f ? static_cast<int>(1.0f / dt) : 999;
                    m_window.setTitle(
                        "MinecraftC" + std::string(m_player.isFlying() ? " [FLY]" : "") +
                        " | FPS: " + std::to_string(fps) +
                        " | XYZ: " + std::to_string(static_cast<int>(std::floor(m_player.getPosition().x))) +
                        "," + std::to_string(static_cast<int>(std::floor(m_player.getPosition().y))) +
                        "," + std::to_string(static_cast<int>(std::floor(m_player.getPosition().z))) +
                        " | Chunks: " + std::to_string(rendered) +
                        "/" + std::to_string(m_world.getActiveChunks().size())
                    );
                } else {
                    m_window.setTitle("MinecraftC [PAUSED]");
                }
            } else {
                // MainMenu: just clear the screen
                m_renderer.beginFrame();
            }

            // ── UI Rendering ──────────────────────────────────────────
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(m_window.native(), &fbWidth, &fbHeight);

            // Phase 1: Inventory overlay (on top of 3D world)
            if (m_inventoryOpen) {
                m_uiRenderer.beginUIFrame(fbWidth, fbHeight);
                m_inventory.render(m_uiRenderer, fbWidth, fbHeight,
                                   static_cast<int>(m_mouseScreenX),
                                   static_cast<int>(m_mouseScreenY));
                m_uiRenderer.endUIFrame();
            }

            // Phase 2: Hotbar HUD (Playing, no inventory, no menu)
            if (m_gameState == GameState::Playing && !m_inventoryOpen && !m_activeMenu) {
                m_uiRenderer.beginUIFrame(fbWidth, fbHeight);
                m_hotbar.render(m_uiRenderer, fbWidth, fbHeight);
                m_uiRenderer.endUIFrame();
            }

            // Phase 3: Active menu (overlays everything)
            if (m_activeMenu) {
                m_uiRenderer.beginUIFrame(fbWidth, fbHeight);
                m_activeMenu->render(m_uiRenderer, fbWidth, fbHeight);
                m_uiRenderer.endUIFrame();
            }

            // ── Finish frame ──────────────────────────────────────────
            m_renderer.endFrame();
            m_window.swapBuffers();

            // Alt+F4 to quit
            if (m_window.isKeyPressed(GLFW_KEY_F4) &&
                (m_keys[GLFW_KEY_LEFT_ALT] || m_keys[GLFW_KEY_RIGHT_ALT])) {
                m_running = false;
            }
        }
    }

    void openInventory() {
        m_inventoryOpen = true;
        m_window.setCursorLocked(false);
    }

    void closeInventory() {
        m_inventoryOpen = false;
        m_window.setCursorLocked(true);
    }

    void cleanup() {
        Debug::Log::shutdown();
        // Resources cleaned up by destructors
    }
};

int main() {
    Application app;
    return app.run();
}
