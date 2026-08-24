#include "app/GameScenePresenter.h"

#include "Config.h"
#include "app/GameSession.h"
#include "core/Window.h"
#include "entity/ProjectileLogic.h"
#include "game/ClientSettings.h"
#include "game/Localization.h"
#include "player/Player.h"
#include "renderer/GameRenderer.h"
#include "renderer/Shadow.h"
#include "ui/Menu.h"
#include "world/ChunkMesh.h"
#include "world/World.h"

#include <algorithm>
#include <cmath>

void GameScenePresenter::render(
    GameSession& session, IGameRenderer& renderer,
    const ClientSettings& settings, Window& window,
    const Localization& localization, GameState state,
    bool showFirstPersonItem, float dt, RuntimeClock::Tick now) {
    // ── 3D Rendering ──────────────────────────────────────────
    if (state == GameState::Playing ||
        state == GameState::Paused) {
        glm::mat4 view       = cameraEffects.viewTransform(
                                   perspective ==
                                       CameraPerspective::FirstPerson) *
                               camera.getViewMatrix();
        glm::mat4 projection = camera.getProjectionMatrix(window.aspectRatio());
        glm::mat4 vp         = projection * view;

        Frustum frustum;
        frustum.extractFromVP(vp);
        float lightningFlash = 0.0f;
        for (const auto& event : session.lightningEvents)
            lightningFlash = std::max(
                lightningFlash, std::clamp(event.seconds / 0.2f, 0.0f, 1.0f));
        RenderEnvironment environment = applyWeather(
            session.dayNightCycle.evaluate(), session.weather.rainGradient(),
            session.weather.thunderGradient(), lightningFlash);
        if (session.world.isHeaven())
            environment = applyHeavenEnvironment(environment);

        renderer.beginFrame();
        renderer.renderSky(
            environment, glm::inverse(vp), camera.m_position,
            settings.renderClouds);
        renderer.setEnvironment(environment, camera.m_position);
        renderer.setViewProjection(vp);
        renderer.setFrustum(frustum);

        const glm::dvec3 playerPosition = session.player.getPosition();
        const glm::dvec3 renderOrigin(
            playerPosition.x, 0.0, playerPosition.z);
        if (settings.renderClouds) {
            renderer.renderClouds(
                playerPosition, vp, session.worldMetadata.seed,
                static_cast<float>(RuntimeClock::seconds(now)),
                settings.cloudRenderDistance);
        }

        if (session.world.lodEnabled() &&
            !session.world.lodSubmissions().empty()) {
            const float lodNear = std::max(16.0f,
                (settings.renderDistance - 2.0f) * Config::CHUNK_SIZE_X);
            const float lodFar = session.world.lodDistanceChunks() *
                Config::CHUNK_SIZE_X + 64.0f;
            const glm::mat4 lodProjection = glm::perspective(
                glm::radians(camera.fovDeg()), window.aspectRatio(),
                lodNear, lodFar);
            const glm::mat4 lodVp = lodProjection * view;
            for (const auto& lod : session.world.lodSubmissions()) {
                renderer.renderLod(*lod.mesh, lod.model, lodVp,
                    lod.minimumDistance, lod.maximumDistance, false);
            }
            std::vector<const LodRenderSubmission*> translucentLod;
            translucentLod.reserve(session.world.lodSubmissions().size());
            for (const auto& lod : session.world.lodSubmissions())
                translucentLod.push_back(&lod);
            std::sort(translucentLod.begin(), translucentLod.end(),
                [](const LodRenderSubmission* a, const LodRenderSubmission* b) {
                    return a->distance2 > b->distance2;
                });
            for (const LodRenderSubmission* lod : translucentLod) {
                renderer.renderLod(*lod->mesh, lod->model, lodVp,
                    lod->minimumDistance, lod->maximumDistance, true);
            }
        }

        visibleChunks.clear();
        std::vector<ShadowChunkSubmission> shadowChunks;
        const float shadowDistance = shadowConfig(settings.shadowQuality).distance;
        if (visibleChunks.capacity() < session.world.getActiveChunks().size())
            visibleChunks.reserve(session.world.getActiveChunks().size());
        int rendered = 0;
        for (const auto* chunk : session.world.getActiveChunks()) {
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

            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(
                aabbMin.x, 0.0f, aabbMin.z));
            const float shadowMargin = shadowDistance + 32.0f;
            if (shadowDistance > 0.0f &&
                aabbMin.x <= shadowMargin && aabbMax.x >= -shadowMargin &&
                aabbMin.z <= shadowMargin && aabbMax.z >= -shadowMargin)
                shadowChunks.push_back({&mesh, model, aabbMin, aabbMax});
            if (!frustum.intersectsAABB(aabbMin, aabbMax)) continue;

            glm::vec3 center(
                aabbMin.x + Config::CHUNK_SIZE_X * 0.5f,
                (Config::WORLD_MIN_Y + chunkMaxY + 1) * 0.5f,
                aabbMin.z + Config::CHUNK_SIZE_Z * 0.5f);
            glm::vec3 delta = center - camera.m_position;
            visibleChunks.push_back({chunk, model, glm::dot(delta, delta)});
        }

        renderer.renderChunkShadows(settings.shadowQuality,
            glm::inverse(vp), view, renderOrigin, shadowChunks);
        renderer.bindBlockShader();
        for (const auto& visible : visibleChunks) {
            renderer.renderChunk(
                visible.chunk->getMesh(), visible.model, vp, false);
            ++rendered;
        }

        std::sort(visibleChunks.begin(), visibleChunks.end(),
            [](const GameScenePresenter::VisibleChunk& a,
               const GameScenePresenter::VisibleChunk& b) {
                return a.distance2 > b.distance2;
            });
        renderer.beginTranslucent();
        for (const auto& visible : visibleChunks) {
            renderer.renderChunk(
                visible.chunk->getMesh(), visible.model, vp, true);
        }
        renderer.endTranslucent();

        session.entities.render(renderer, vp, renderOrigin);
        if (perspective != CameraPerspective::FirstPerson &&
            !session.player.isSpectator()) {
            const glm::mat4 hand = playerRenderer.renderThirdPerson(
                renderer, session.player.getPosition(), renderOrigin,
                session.player.getYaw(), session.player.getPitch(), vp,
                session.world.sampleLight(session.player.getEyePosition()),
                session.sleepFacing());
            if (!session.isSleeping())
                heldItemRenderer.renderThirdPerson(
                    session.player.activeItem(), vp, hand);
        }
        session.particles.buildRenderData(renderOrigin, particleRenderData);
        appendBowTrajectory(session, renderOrigin);
        const float particleIntensity = session.world.isHeaven()
            ? 0.80f : session.weather.rainGradient();
        renderer.renderParticles(
            particleRenderData, vp,
            camera.right,
            glm::normalize(glm::cross(camera.forward, camera.right)),
            particleIntensity);

        // Wireframe highlight
        auto highlighted = session.player.getHighlightedBlock();
        if (highlighted) {
            glm::vec3 pos(
                static_cast<float>(highlighted->x - renderOrigin.x),
                static_cast<float>(highlighted->y),
                static_cast<float>(highlighted->z - renderOrigin.z));
            const BlockId highlightedBlock = session.world.getBlock(
                highlighted->x, highlighted->y, highlighted->z);
            renderer.renderWireframe(
                pos, glm::vec3(1.0f, blockCollisionHeight(highlightedBlock), 1.0f), vp);
        }

        if (perspective == CameraPerspective::FirstPerson &&
            showFirstPersonItem && !session.player.isSpectator() &&
            !session.playerDead && !session.isSleeping())
            heldItemRenderer.renderFirstPerson(
                session.player.activeItem(), session.player.visualState().swingProgress,
                session.player.attackStrength(),
                window.aspectRatio(),
                cameraEffects.viewModelTransform());

        const SmoothLightSample eyeLight =
            session.world.sampleLight(session.player.getEyePosition());
        PostProcessState postProcess;
        postProcess.environment = environment;
        postProcess.inverseViewProjection = glm::inverse(vp);
        postProcess.cameraPosition = camera.m_position;
        postProcess.exposure = visualExposure.update(
            eyeLight.sky, eyeLight.block, environment, dt);
        postProcess.underwater = session.player.underwater() ? 1.0f : 0.0f;
        postProcess.hurt = cameraEffects.trauma();
        renderer.finishScene(postProcess);

        // Title bar info
        if (state == GameState::Playing) {
            titleUpdateSeconds += dt;
            if (titleUpdateSeconds >= 0.25f) {
                titleUpdateSeconds = 0.0f;
                int fps = dt > 0.0f ? static_cast<int>(1.0f / dt) : 999;
                window.setTitle(
                    "MinecraftC" + (session.player.isFlying()
                        ? " [" + localization.text("window.fly") + "]" : "") +
                    " | FPS: " + std::to_string(fps) +
                    " | XYZ: " + std::to_string(static_cast<int>(std::floor(session.player.getPosition().x))) +
                    "," + std::to_string(static_cast<int>(std::floor(session.player.getPosition().y))) +
                    "," + std::to_string(static_cast<int>(std::floor(session.player.getPosition().z))) +
                    " | " + localization.text("window.chunks") + ": " +
                    std::to_string(rendered) +
                    "/" + std::to_string(session.world.getActiveChunks().size())
                );
            }
        } else {
            window.setTitle(
                "MinecraftC [" + localization.text("window.paused") + "]");
        }
    } else {
        // MainMenu: just clear the screen
        renderer.beginFrame();
        renderer.finishScene({});
    }
}

void GameScenePresenter::appendBowTrajectory(
    GameSession& session, const glm::dvec3& renderOrigin) {
    const auto launch = session.player.bowLaunchPreview();
    if (!launch) return;
    constexpr double stepSeconds = 0.10;
    constexpr int pointCount = 32;
    glm::dvec3 previous = launch->origin;
    for (int point = 1; point <= pointCount; ++point) {
        const glm::dvec3 current = projectilePosition(
            launch->origin, launch->velocity, point * stepSeconds);
        const glm::dvec3 segment = current - previous;
        const float segmentLength = static_cast<float>(glm::length(segment));
        if (segmentLength <= 0.0001f) break;
        if (session.world.raycast(
                previous, glm::normalize(glm::vec3(segment)), segmentLength))
            break;
        if (particleRenderData.size() >= ParticleSystem::MAX_PARTICLES) break;
        const float fade = static_cast<float>(point - 1) /
            static_cast<float>(pointCount);
        particleRenderData.push_back({
            glm::vec3(current - renderOrigin),
            static_cast<float>(ParticleKind::Trajectory),
            0.08f + fade * 0.68f, 0.0f, 0.13f, 0.0f});
        previous = current;
    }
}


GameScenePresenter::GameScenePresenter()
    : camera(Config::FOV, Config::NEAR_PLANE, Config::FAR_PLANE) {}

void GameScenePresenter::initialize(
    IGameRenderer& renderer, const std::filesystem::path& assetRoot) {
    playerRenderer.initialize(assetRoot, renderer);
    heldItemRenderer.initialize(renderer, assetRoot);
}

void GameScenePresenter::resetGraphics() {
    heldItemRenderer.reset();
}

void GameScenePresenter::restoreGraphics(
    IGameRenderer& renderer, const std::filesystem::path& assetRoot) {
    initialize(renderer, assetRoot);
}

void GameScenePresenter::resetForWorld(const glm::dvec3& playerPosition) {
    perspective = CameraPerspective::FirstPerson;
    resetPlayerFeedback(playerPosition);
    visibleChunks.clear();
    particleRenderData.clear();
    titleUpdateSeconds = 0.0f;
}

void GameScenePresenter::resetPlayerFeedback(
    const glm::dvec3& playerPosition) {
    cameraEffects.reset(playerPosition);
    visualExposure.reset();
}

void GameScenePresenter::updateCamera(
    const World& world, const Player& player, float dt, bool playerDead,
    bool sleeping, const glm::ivec3& sleepBed,
    const glm::vec3& sleepFacing, float sleepProgress) {
    const PlayerVisualState visual = player.visualState();
    playerRenderer.update(visual, dt);
    camera.updateFov(dynamicViewFov(
        Config::FOV, Config::SPRINT_FOV_BOOST, Config::BOW_FOV_REDUCTION,
        visual, perspective, player.isFlying(),
        playerDead ? 0.0f : player.bowCharge()), dt);
    cameraEffects.update(player.getPosition(), player.onGround(),
        player.isFlying(), player.velocity().y, player.landingSpeed(), dt);
    glm::dvec3 eye = player.getEyePosition();
    if (sleeping) {
        const glm::vec3 facing = glm::length(sleepFacing) > 0.01f
            ? glm::normalize(sleepFacing) : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::dvec3 bedView = glm::dvec3(sleepBed) +
            glm::dvec3(0.5, 0.72, 0.5) + glm::dvec3(facing * 0.30f);
        // PlayerVisualState carries the direction of the animation: entering
        // goes 0→1, while leaving goes 1→0. Using it here keeps the camera
        // attached to the bed during the first leave frame and restores the
        // original eye position smoothly.
        const float cameraProgress = std::clamp(
            visual.sleeping ? visual.sleepProgress : sleepProgress, 0.0f, 1.0f);
        const float eased = cameraProgress * cameraProgress *
            (3.0f - 2.0f * cameraProgress);
        eye = glm::mix(eye, bedView, static_cast<double>(eased));
        cameraWorldPosition = eye;
    } else {
        cameraWorldPosition = resolveThirdPersonCamera(
            world, eye, player.getForward(), perspective);
    }
    const glm::dvec3 cameraLocal = cameraWorldPosition -
        glm::dvec3(player.getPosition().x, 0.0, player.getPosition().z);
    camera.setPosition(glm::vec3(cameraLocal));
    if (sleeping) {
        const float yaw = glm::degrees(std::atan2(sleepFacing.x, sleepFacing.z));
        camera.updateVectors(yaw, -18.0f);
    } else if (perspective == CameraPerspective::ThirdPersonFront)
        camera.updateVectors(player.getYaw() + 180.0f, -player.getPitch());
    else
        camera.updateVectors(player.getYaw(), player.getPitch());
}

void GameScenePresenter::onPlayerDamaged(float amount) {
    cameraEffects.onDamage(amount);
}

void GameScenePresenter::cyclePerspective() {
    perspective = nextPerspective(perspective);
}
