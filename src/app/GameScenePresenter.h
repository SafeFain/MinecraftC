#pragma once

#include "core/RuntimeClock.h"
#include "player/PlayerRenderer.h"
#include "player/PlayerVisual.h"
#include "renderer/Camera.h"
#include "renderer/CameraEffects.h"
#include "renderer/HeldItemRenderer.h"
#include "renderer/ParticleSystem.h"
#include "renderer/VisualQuality.h"

#include <filesystem>
#include <vector>

class Chunk;
class ClientSettings;
class GameSession;
class IGameRenderer;
class Localization;
class Player;
class Window;
class World;
enum class GameState;

class GameScenePresenter {
public:
    struct VisibleChunk {
        const Chunk* chunk = nullptr;
        glm::mat4 model{1.0f};
        float distance2 = 0.0f;
    };

    GameScenePresenter();

    void initialize(IGameRenderer& renderer,
                    const std::filesystem::path& assetRoot);
    void resetGraphics();
    void restoreGraphics(IGameRenderer& renderer,
                         const std::filesystem::path& assetRoot);
    void resetForWorld(const glm::dvec3& playerPosition);
    void resetPlayerFeedback(const glm::dvec3& playerPosition);
    void updateCamera(const World& world, const Player& player,
                      float dt, bool playerDead);
    void render(GameSession& session, IGameRenderer& renderer,
                const ClientSettings& settings, Window& window,
                const Localization& localization, GameState state,
                bool showFirstPersonItem, float dt,
                RuntimeClock::Tick now);
    void onPlayerDamaged(float amount);
    void cyclePerspective();

    Camera camera;
    CameraEffects cameraEffects;
    VisualExposure visualExposure;
    CameraPerspective perspective = CameraPerspective::FirstPerson;
    glm::dvec3 cameraWorldPosition{0.0};
    PlayerRenderer playerRenderer;
    HeldItemRenderer heldItemRenderer;
    std::vector<VisibleChunk> visibleChunks;
    std::vector<ParticleRenderData> particleRenderData;
    float titleUpdateSeconds = 0.0f;

private:
    void appendBowTrajectory(GameSession& session,
                             const glm::dvec3& renderOrigin);
};
