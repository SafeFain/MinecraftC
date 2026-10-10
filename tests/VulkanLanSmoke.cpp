#include "app/GameSession.h"
#include "app/GameScenePresenter.h"
#include "app/GameUiController.h"
#include "app/ApplicationInputController.h"
#include "core/Window.h"
#include "game/ClientSettings.h"
#include "platform/Clipboard.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class SmokeClipboard final : public platform::Clipboard {
public:
    bool writeText(std::string_view value) override { text = value; return true; }
    bool readText(std::string& value) override { value = text; return true; }
private:
    std::string text;
};
}
struct GameSessionTestAccess {
    static void scene(GameSession& session) {
        session.world().setThreadPool(nullptr);
        for (int z = -2; z <= 2; ++z) for (int x = -2; x <= 2; ++x) {
            auto* chunk = session.world().getChunk(x,z);
            for (int lz = 0; lz < 16; ++lz) for (int lx = 0; lx < 16; ++lx) chunk->setBlock(lx,0,lz,BlockId::GRASS);
            chunk->generated = true; chunk->lifecycle = Chunk::LifecycleState::Renderable;
        }
        session.worldMetadata.worldSpawn = {0,0,0}; session.terrainGenerated = true;
        session.world().update({.5,1.01,.5},0); session.entities().setNaturalSpawningEnabled(false);
        session.player.setPosition({.5,1.01,8.5}); session.player.setOrientation(180,0);
        session.player.inventory().slot(0) = {ItemId::DIAMOND_SWORD,1,0};
        WorldMetadata::PersistedEntity villager; villager.type = static_cast<uint8_t>(EntityType::Villager);
        villager.position = {-2,1.01,6.5}; villager.health = 20; villager.villager.profession = VillagerProfession::Farmer;
        session.entities().loadEntities({villager}); session.entities().spawnItem({2.5,1.3,6.5},{ItemId::EMERALD,2,0});
        session.dayNightCycle().setPhase(.25f);
    }
    static Player& player(GameSession& session) { return session.player; }
    static void serverTick(GameSession& session, float dt) { session.updateLanPlayers(dt); }
    static bool moved(const GameSession& session) {
        return !session.guests.empty() && session.guests.begin()->second->player.getPosition().z > .75;
    }
};

int main(int argc, char** argv) {
    if (argc != 4 && argc != 6) { std::cerr << "Usage: vulkan_lan_smoke --host|--client <assets> <isolated-run-directory> [width height]\n"; return 2; }
    try {
        const int width = argc == 6 ? std::stoi(argv[4]) : 960, height = argc == 6 ? std::stoi(argv[5]) : 640;
        require(width >= 320 && width <= 1920 && height >= 320 && height <= 1080,"invalid smoke viewport");
        const std::string role = argv[1]; const auto assets = std::filesystem::absolute(argv[2]); const std::filesystem::path root = argv[3];
        std::filesystem::create_directories(root);
        Config::RENDER_DISTANCE = 2;
        const auto started = std::chrono::steady_clock::now();
        auto now = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
        if (role == "--host") {
            GameSession session(root / "host" / "saves"); session.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
            const auto world = session.createWorld("Rendered LAN",42,GameMode::Survival,Difficulty::Peaceful,true,WorldType::Superflat);
            session.startWorld(world,true,0); GameSessionTestAccess::scene(session); session.setLanNickname("Host");
            require(session.openLanRoom(0,2,true),"listen room failed");
            { std::ofstream port(root / "port"); port << session.lanPort(); }
            double previous = now(); bool moved = false;
            while (now() < 90 && !std::filesystem::exists(root / "client-done")) {
                const double elapsed = now(); session.pollLan(elapsed);
                GameSessionTestAccess::serverTick(session, static_cast<float>(std::clamp(elapsed - previous, 0.0, .05))); previous = elapsed;
                moved = moved || GameSessionTestAccess::moved(session);
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            require(std::filesystem::exists(root / "client-done") && moved,"separate client did not complete authoritative movement/rendering");
            session.closeLanRoom(); session.leaveWorld(); std::cout << "Separate host accepted and simulated client movement\n";
        } else if (role == "--client") {
            uint16_t port = 0; { std::ifstream input(root / "port"); input >> port; } require(port != 0,"missing host port");
            Window window(width,height,"MinecraftC LAN smoke",Window::SurfaceMode::Vulkan,false,false);
            VulkanRenderer renderer; renderer.initialize(window,assets); renderer.setVisualQuality(VisualQuality::Medium); renderer.setEnhancedVisuals(false);
            std::cout << VulkanGiSmokeProbe::deviceDescription(renderer) << '\n';
            GameSession session(root / "client" / "saves"); session.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low}); session.setLanNickname("Guest");
            session.initializeEntityModels(assets,renderer);
            GameScenePresenter presenter; presenter.initialize(renderer,assets);
            SmokeClipboard clipboard; GameUiController ui(GameSessionTestAccess::player(session).inventory(),clipboard);
            ui.localization.load(assets); ui.renderer.initialize(renderer,renderer.getBlockAtlasTexture(),assets); ui.renderer.setLocalization(ui.localization);
            ClientSettings settings; settings.renderClouds = false; settings.guiScale = 1;
            ApplicationInputController inputs;
            require(session.joinLanRoom("::1",port,now()),"replica could not connect");
            int frames = 0; double previous = now(); bool loaded = false;
            while (now() < 75 && frames < 100) {
                window.finishEventFrame(); const double elapsed = now(); const float dt = static_cast<float>(std::clamp(elapsed - previous,.001,.05)); previous = elapsed;
                session.pollLan(elapsed); require(!session.lanConnectionFailed(),session.lanError().c_str());
                if (!session.lanWorldReady()) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
                if (!loaded) {
                    loaded = session.advanceLoading(&renderer, RuntimeClock{}.now());
                    if (!loaded) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
                }
                session.setLocalControl(true); InputState movement;
                movement.setVirtual(InputAction::MoveForward,frames < 18 ? 1.0f : 0.0f); movement.update({});
                session.handleMovement(movement,dt); session.updatePlaying(dt,&renderer,{},true);
                presenter.updateCamera(session.worldState(),session.playerState(),dt,false);
                const bool capture = frames == 99; if (capture) VulkanGiSmokeProbe::requestCapture(renderer);
                presenter.render(session,renderer,settings,window,ui.localization,GameState::Playing,true,dt,RuntimeClock{}.now());
                ui.render(session,settings,inputs,window,GameState::Playing,true); renderer.endFrame();
                if (capture) {
                    require(session.remotePlayers().size() == 1 && session.entityState().entities().size() == 2 &&
                            !presenter.visibleChunks.empty() && session.roomRoster().size() == 2,"joined scene is missing replicated players/entities/terrain/roster");
                    const auto rgba = VulkanGiSmokeProbe::readCapture(renderer); require(rgba.size() == static_cast<size_t>(width)*height*4,"invalid Vulkan capture");
                    std::ofstream output(root / "joined.ppm",std::ios::binary); output << "P6\n" << width << ' ' << height << "\n255\n";
                    for (size_t i = 0; i < rgba.size(); i += 4) output.write(reinterpret_cast<const char*>(rgba.data()+i),3);
                    require(bool(output),"capture write failed");
                }
                ++frames; std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            require(frames == 100,"joined Vulkan scene timed out");
            renderer.waitIdle(); ui.renderer.resetGraphics(); presenter.resetGraphics(); session.invalidateGpuMeshes(); session.leaveWorld();
            { std::ofstream done(root / "client-done"); done << "100 joined Vulkan frames\n"; }
            std::cout << "Guest rendered replicated host, villager, item, terrain and global roster\n";
        } else throw std::runtime_error("unknown smoke role");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
