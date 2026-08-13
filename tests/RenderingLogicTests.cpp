#include "renderer/RenderEnvironment.h"
#include "Config.h"
#include "renderer/CameraEffects.h"
#include "renderer/CloudRenderData.h"
#include "model/ModelRenderLogic.h"
#include "renderer/ShaderDialect.h"
#include "renderer/RenderDevice.h"
#include "renderer/ParticleSystem.h"
#include "renderer/Shadow.h"
#include "renderer/VisualQuality.h"
#include "player/PlayerVisual.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool nearMatrix(const glm::mat4& a, const glm::mat4& b, float epsilon = 0.0001f) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (std::abs(a[column][row] - b[column][row]) > epsilon) return false;
    return true;
}
}

int main() {
    const VisualQualityConfig lowVisual = visualQualityConfig(VisualQuality::Low);
    const VisualQualityConfig mediumVisual = visualQualityConfig(VisualQuality::Medium);
    const VisualQualityConfig highVisual = visualQualityConfig(VisualQuality::High);
    const VisualQualityConfig ultraVisual = visualQualityConfig(VisualQuality::Ultra);
    require(lowVisual.sceneSamples == 1 && lowVisual.bloomLevels == 0 &&
            lowVisual.aoDirections == 0 && !lowVisual.materialNormals &&
            mediumVisual.sceneSamples == 2 && mediumVisual.bloomLevels == 3 &&
            mediumVisual.materialNormals && mediumVisual.voxelClouds &&
            highVisual.sceneSamples == 4 && highVisual.aoDirections == 6 &&
            highVisual.cirrusClouds && ultraVisual.aoDirections == 8 &&
            ultraVisual.bloomLevels == 6,
            "visual quality presets do not match the rendering contract");
    static_assert(static_cast<int>(ParticleKind::RainSplash) == 4,
                  "particle kind values are part of the shader contract");
    VisualExposure exposure;
    RenderEnvironment exposureEnvironment;
    const float brightExposure = exposure.update(1.0f, 0.0f,
                                                   exposureEnvironment, 0.016f);
    for (int i = 0; i < 120; ++i)
        exposure.update(0.0f, 0.0f, exposureEnvironment, 1.0f / 60.0f);
    require(exposure.value() > brightExposure && exposure.value() <= 1.65f,
            "visual exposure did not adapt toward a bounded cave value");
    require(nextPerspective(nextPerspective(nextPerspective(
                CameraPerspective::FirstPerson))) == CameraPerspective::FirstPerson,
            "perspective cycle did not return to first person");
    PlayerVisualState visualState;
    visualState.grounded = true;
    require(playerLocomotion(visualState) == PlayerLocomotion::Idle,
            "stationary player did not select the idle animation");
    visualState.velocity.x = Config::PLAYER_SPEED;
    require(playerLocomotion(visualState) == PlayerLocomotion::Walk,
            "actual ground velocity did not select the walk animation");
    visualState.sprinting = true;
    require(playerLocomotion(visualState) == PlayerLocomotion::Run,
            "sprinting velocity did not select the run animation");
    visualState.grounded = false;
    visualState.velocity.y = Config::JUMP_SPEED;
    require(playerLocomotion(visualState) == PlayerLocomotion::Jump,
            "positive airborne velocity did not select the jump animation");
    visualState.velocity.y = -1.0f;
    require(playerLocomotion(visualState) == PlayerLocomotion::Fall,
            "negative airborne velocity did not select the fall animation");
    require(firstPersonSwingTransform(0.0f) == glm::mat4(1.0f) &&
            firstPersonSwingTransform(1.0f) == glm::mat4(1.0f) &&
            firstPersonSwingTransform(0.5f) != glm::mat4(1.0f),
            "first-person swing curve endpoints or motion are incorrect");
    for (BlockId leaf : {BlockId::LEAVES, BlockId::BIRCH_LEAVES,
                         BlockId::SPRUCE_LEAVES, BlockId::JUNGLE_LEAVES,
                         BlockId::ACACIA_LEAVES}) {
        require(getBlockProps(leaf).layer == RenderLayer::Cutout &&
                    getBlockProps(leaf).alpha == 1.0f,
                "leaf blocks must use opaque cutout rendering");
    }

    static_assert(sizeof(MeshVertex) == 44);
    static_assert(offsetof(MeshVertex, px) == 0);
    static_assert(offsetof(MeshVertex, ao) == 12);
    static_assert(offsetof(MeshVertex, u) == 28);
    static_assert(offsetof(MeshVertex, tile) == 36);
    static_assert(offsetof(MeshVertex, face) == 40);
    static_assert(sizeof(ParticleRenderData) == 32);
    static_assert(offsetof(ParticleRenderData, position) == 0);
    static_assert(offsetof(ParticleRenderData, kind) == 12);
    static_assert(offsetof(ParticleRenderData, phase) == 16);
    static_assert(offsetof(ParticleRenderData, texture) == 20);
    static_assert(offsetof(ParticleRenderData, size) == 24);
    static_assert(offsetof(ParticleRenderData, rotation) == 28);
    static_assert(sizeof(CloudInstance) == 28);
    static_assert(offsetof(CloudInstance, x) == 0);
    static_assert(offsetof(CloudInstance, width) == 12);
    static_assert(offsetof(CloudInstance, height) == 20);
    static_assert(offsetof(CloudInstance, visibleFaces) == 24);
    const CloudView negativeCloud = cloudView({-0.5, 64.0, -0.5}, 0.0f, 192);
    require(negativeCloud.centerX == -1 && negativeCloud.centerZ == -1,
            "cloud grid did not use floor coordinates");
    require(negativeCloud.radius == 12,
            "cloud distance did not convert to the expected radius");
    require(cloudView({0.0, 0.0, 0.0}, 0.0f, 4096).radius == MAX_CLOUD_RADIUS,
            "cloud distance exceeded its supported maximum");
    const auto cloudsA = buildCloudInstances(0x123456789abcdef0ULL, -7, 11, 12);
    const auto cloudsB = buildCloudInstances(0x123456789abcdef0ULL, -7, 11, 12);
    const auto cloudsC = buildCloudInstances(0xfedcba9876543210ULL, -7, 11, 12);
    const auto sameClouds = [](const auto& a, const auto& b) {
        return a.size() == b.size() && std::equal(
            a.begin(), a.end(), b.begin(), [](const CloudInstance& left,
                                               const CloudInstance& right) {
                return left.x == right.x && left.y == right.y &&
                    left.z == right.z && left.width == right.width &&
                    left.depth == right.depth && left.height == right.height &&
                    left.visibleFaces == right.visibleFaces;
            });
    };
    require(!cloudsA.empty() && cloudsA.size() <= MAX_CLOUD_INSTANCES,
            "cloud layout was empty or exceeded capacity");
    require(sameClouds(cloudsA, cloudsB),
            "same-seed cloud layout was not deterministic");
    require(!sameClouds(cloudsA, cloudsC),
            "different cloud seeds produced the same layout");
    const auto cloudAt = [&cloudsA](float x, float y, float z) {
        return std::find_if(cloudsA.begin(), cloudsA.end(),
            [=](const CloudInstance& cloud) {
                return cloud.x == x && cloud.y == y && cloud.z == z;
            });
    };
    for (const CloudInstance& cloud : cloudsA) {
        require(cloud.width == CLOUD_CELL_SIZE && cloud.depth == CLOUD_CELL_SIZE,
                "cloud surface cell dimensions are inconsistent");
        require(cloud.visibleFaces != 0 &&
                    (cloud.visibleFaces & ~CLOUD_ALL_FACES) == 0,
                "cloud visible-face mask is invalid");
        const auto requireSide = [&](float dx, float dz, uint32_t face) {
            const bool neighbor = cloudAt(
                cloud.x + dx, cloud.y, cloud.z + dz) != cloudsA.end();
            require(((cloud.visibleFaces & face) != 0) == !neighbor,
                    "cloud shared side face was not culled");
        };
        requireSide(0.0f, -CLOUD_CELL_SIZE, CloudNegativeZ);
        requireSide(0.0f, CLOUD_CELL_SIZE, CloudPositiveZ);
        requireSide(-CLOUD_CELL_SIZE, 0.0f, CloudNegativeX);
        requireSide(CLOUD_CELL_SIZE, 0.0f, CloudPositiveX);
        const float otherLayerY = cloud.y == 192.0f ? 195.0f : 192.0f;
        const bool verticalNeighbor =
            cloudAt(cloud.x, otherLayerY, cloud.z) != cloudsA.end();
        const uint32_t verticalFace = cloud.y == 192.0f
            ? CloudPositiveY : CloudNegativeY;
        require(((cloud.visibleFaces & verticalFace) != 0) == !verticalNeighbor,
                "cloud shared vertical face was not culled");
    }
    MeshData validMesh;
    validMesh.vertices = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}},
                          {{0, 1, 0}, {0, 1}}};
    validMesh.indices = {0, 1, 2};
    validMesh.opaqueIndexCount = 3;
    validateMeshData(validMesh);
    bool rejectedMesh = false;
    try {
        MeshData invalid = validMesh;
        invalid.indices[2] = 3;
        validateMeshData(invalid);
    } catch (const std::invalid_argument&) {
        rejectedMesh = true;
    }
    require(rejectedMesh, "out-of-range mesh index was accepted");
    std::vector<uint8_t> blocks(Config::CHUNK_VOLUME,
                                static_cast<uint8_t>(BlockId::AIR));
    const int blockIndex = Config::worldYToStorageY(0) *
        Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
    blocks[static_cast<size_t>(blockIndex)] = static_cast<uint8_t>(BlockId::STONE);
    int columnMax[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z]{};
    ChunkMesh windingMesh;
    windingMesh.build(0, 0, blocks.data(), columnMax,
        [](int, int, int) { return BlockId::AIR; },
        [](int, int, int) { return LightSample{15, 0}; });
    for (size_t index = 0; index + 2 < windingMesh.indices.size(); index += 3) {
        const MeshVertex& a = windingMesh.vertices[windingMesh.indices[index]];
        const MeshVertex& b = windingMesh.vertices[windingMesh.indices[index + 1]];
        const MeshVertex& c = windingMesh.vertices[windingMesh.indices[index + 2]];
        const glm::vec3 normal = glm::cross(
            glm::vec3(b.px - a.px, b.py - a.py, b.pz - a.pz),
            glm::vec3(c.px - a.px, c.py - a.py, c.pz - a.pz));
        const glm::ivec3 expected = FACE_OFFSETS[static_cast<size_t>(a.face)];
        require(glm::dot(normal, glm::vec3(expected)) > 0.0f,
                "Chunk face winding is not outward CCW");
    }
    require(isMeshMaterialCompatible(MeshVertexLayout::PositionUv,
                MaterialPipeline::UnlitTextured) &&
            isMeshMaterialCompatible(MeshVertexLayout::Chunk,
                MaterialPipeline::ChunkOpaqueCutout) &&
            isMeshMaterialCompatible(MeshVertexLayout::Chunk,
                MaterialPipeline::ChunkTranslucent) &&
            !isMeshMaterialCompatible(MeshVertexLayout::PositionUv,
                MaterialPipeline::ChunkOpaqueCutout) &&
            !isMeshMaterialCompatible(MeshVertexLayout::Chunk,
                MaterialPipeline::UnlitTextured) &&
            !isMeshMaterialCompatible(MeshVertexLayout::PositionUv,
                MaterialPipeline::UiTextured),
            "mesh/material compatibility matrix is incorrect");
    TextureData validTexture;
    validTexture.width = 1;
    validTexture.height = 1;
    validTexture.pixels = {1, 2, 3, 4};
    validateTextureData(validTexture);
    bool rejectedTexture = false;
    try {
        validTexture.pixels.pop_back();
        validateTextureData(validTexture);
    } catch (const std::invalid_argument&) {
        rejectedTexture = true;
    }
    require(rejectedTexture, "invalid RGBA texture byte count was accepted");
    const glm::vec4 glNear = glm::vec4(0, 0, -1, 1);
    const glm::vec4 vkNear = clipSpaceCorrection(GraphicsApi::Vulkan) * glNear;
    require(std::abs(vkNear.z) < 0.0001f && vkNear.y == 0.0f,
            "Vulkan clip-space depth conversion is incorrect");
    const glm::vec4 glTop = glm::vec4(0, 1, 0, 1);
    require((clipSpaceCorrection(GraphicsApi::Vulkan) * glTop).y == -1.0f,
            "Vulkan clip-space Y conversion is incorrect");
    const std::string desktopShader = "#version 330 core\nvoid main(){}\n";
    require(shaderSourceForApi(desktopShader, GraphicsApi::OpenGL33) == desktopShader,
            "desktop shader source was unexpectedly rewritten");
    const std::string esShader = shaderSourceForApi(
        desktopShader, GraphicsApi::OpenGLES30);
    require(esShader.rfind("#version 300 es\nprecision highp float;\n"
                           "precision highp int;\n", 0) == 0 &&
            esShader.find("#version 330 core") == std::string::npos,
            "GLES shader preamble was not generated correctly");
    require(model::modelPass(model::AlphaMode::Opaque) ==
                model::ModelPass::Opaque &&
            model::modelPass(model::AlphaMode::Mask) ==
                model::ModelPass::Opaque,
            "opaque and masked model materials were split incorrectly");
    require(model::modelPass(model::AlphaMode::Blend) ==
                model::ModelPass::Blend,
            "blended model material used the opaque pass");
    std::vector<model::BlendSortEntry> blended{{2.0f}, {9.0f}, {4.0f}};
    model::sortBlended(blended);
    require(blended[0].distanceSquared == 9.0f &&
            blended[2].distanceSquared == 2.0f,
            "blended model draws were not sorted far-to-near");
    CameraEffects cameraEffects;
    cameraEffects.reset({0.0, 64.0, 0.0});
    cameraEffects.update({0.0, 64.0, 0.0}, true, false, 0.0f, 0.0f,
                         1.0f / 60.0f);
    require(cameraEffects.movementBlend() == 0.0f,
            "stationary player produced view bobbing");
    constexpr int walkFrames = 60;
    for (int i = 1; i <= walkFrames; ++i)
        cameraEffects.update({Config::PLAYER_SPEED *
                                  static_cast<double>(i) / walkFrames,
                              64.0, 0.0},
                             true, false, 0.0f, 0.0f, 1.0f / 60.0f);
    require(cameraEffects.movementBlend() > 0.8f &&
            glm::length(cameraEffects.translation()) > 0.001f,
            "ground movement did not produce view bobbing");
    require(std::abs(cameraEffects.walkPhase() /
                         (2.0f * 3.14159265358979323846f * Config::PLAYER_SPEED) -
                     0.44f) < 0.001f,
            "walking camera still uses an excessive step frequency");
    require(nearMatrix(cameraEffects.viewTransform(false), glm::mat4(1.0f)),
            "third-person view retained first-person movement motion");

    double fallY = 64.0;
    for (int i = 0; i < 20; ++i) {
        fallY -= 0.2;
        cameraEffects.update({Config::PLAYER_SPEED, fallY, 0.0}, false, false,
                             -12.0f, 0.0f,
                             1.0f / 60.0f);
    }
    require(cameraEffects.translation().y > 0.01f &&
                std::abs(cameraEffects.translation().y) <= 0.0451f &&
                std::abs(cameraEffects.rotationDegrees().x) <= 0.651f,
            "falling camera inertia is missing or exceeds its comfort bound");
    cameraEffects.update({Config::PLAYER_SPEED, fallY, 0.0}, true, false,
                         0.0f, 18.0f,
                         1.0f / 60.0f);
    require(cameraEffects.landingStrength() == 1.0f &&
                std::abs(cameraEffects.translation().y) <= 0.0601f,
            "landing compression did not use a bounded impact strength");
    for (int i = 0; i < 30; ++i)
        cameraEffects.update({Config::PLAYER_SPEED, fallY, 0.0}, true, false,
                             0.0f, 0.0f,
                             1.0f / 60.0f);
    require(cameraEffects.landingStrength() == 0.0f,
            "landing rebound did not settle within its target duration");
    cameraEffects.update({100.0, 90.0, -50.0}, true, false, 0.0f, 0.0f,
                         1.0f / 60.0f);
    require(cameraEffects.walkPhase() == 0.0f &&
                cameraEffects.translation() == glm::vec3(0.0f),
            "teleport generated false movement or landing feedback");

    cameraEffects.onDamage(6.0f);
    require(cameraEffects.trauma() > 0.6f,
            "damage did not produce camera trauma");
    cameraEffects.update({100.0, 90.0, -50.0}, true, false, 0.0f, 0.0f, 0.05f);
    require(glm::length(cameraEffects.rotationDegrees()) > 0.1f,
            "damage trauma did not produce rotational shake");
    for (int i = 0; i < 20; ++i)
        cameraEffects.update({100.0, 90.0, -50.0}, true, false, 0.0f, 0.0f, 0.05f);
    require(cameraEffects.trauma() == 0.0f,
            "damage trauma did not decay to zero");

    DayNightCycle cycle;
    require(DayNightCycle::isDayPhase(0.0f), "sunrise was not day");
    require(DayNightCycle::isDayPhase(0.25f), "noon was not day");
    require(DayNightCycle::isDayPhase(0.5f - 0.000001f),
            "the phase immediately before sunset was not day");
    require(!DayNightCycle::isDayPhase(0.5f),
            "sunset did not enter night immediately");
    require(!DayNightCycle::isDayPhase(0.999999f),
            "the phase immediately before sunrise was not night");
    cycle.setDay();
    require(cycle.phase() == 0.0f && cycle.isDay(),
            "set day did not select sunrise");
    cycle.setNight();
    require(cycle.phase() == 0.5f && cycle.isNight(),
            "set night did not select sunset");
    cycle.update(0.1f, 0, true);
    require(cycle.phase() == 0.5f,
            "a manually selected time was lost in static-cycle mode");
    cycle.resetMorning();
    const float morning = cycle.phase();
    require(!cycle.isNight(), "morning was classified as night");

    cycle.update(1.0f, 20, false);
    require(cycle.phase() == morning, "paused cycle advanced");

    cycle.update(0.1f, 20, true);
    require(cycle.phase() > morning, "active cycle did not advance");

    DayNightCycle nightCycle;
    for (int i = 0; i < 900; ++i) nightCycle.update(0.1f, 1, true);
    require(nightCycle.isNight(), "night phase was not recognized");

    cycle.update(0.1f, 0, false);
    require(std::abs(cycle.phase() - DayNightCycle::STATIC_DAY_PHASE) < 0.0001f,
            "static day did not select noon");

    const RenderEnvironment noon = cycle.evaluate();
    require(noon.daylight > 0.95f, "static noon is not daylight");
    require(noon.ambientIntensity >= Config::NIGHT_AMBIENT_MIN &&
            noon.ambientIntensity <= 1.0f,
            "noon ambient is outside expected range");
    const RenderEnvironment rain = applyWeather(noon, 1.0f, 0.0f, 0.0f);
    const RenderEnvironment thunder = applyWeather(noon, 1.0f, 1.0f, 0.0f);
    const RenderEnvironment flash = applyWeather(noon, 1.0f, 1.0f, 1.0f);
    require(rain.directIntensity < noon.directIntensity &&
            thunder.directIntensity < rain.directIntensity,
            "weather did not progressively darken direct light");
    require(rain.starIntensity == 0.0f,
            "overcast weather did not hide stars");
    require(flash.ambientIntensity > thunder.ambientIntensity,
            "lightning flash did not brighten the environment");
    const glm::vec3 noonCloud = cloudColorForEnvironment(noon);
    const glm::vec3 rainCloud = cloudColorForEnvironment(rain);
    const glm::vec3 thunderCloud = cloudColorForEnvironment(thunder);
    require(glm::length(rainCloud) < glm::length(noonCloud),
            "rain clouds should be darker than clear-day clouds");
    require(glm::length(thunderCloud) < glm::length(rainCloud),
            "thunder clouds should be darker than rain clouds");
    require(shadowConfig(ShadowQuality::Off).cascadeCount == 0 &&
            shadowConfig(ShadowQuality::Low).cascadeCount == 1 &&
            shadowConfig(ShadowQuality::Medium).cascadeCount == 2 &&
            shadowConfig(ShadowQuality::High).cascadeCount == 3,
            "shadow quality did not select the expected cascade count");
    require(shadowConfig(ShadowQuality::Medium).distance == 128.0f &&
            shadowFilterTaps(ShadowQuality::Medium, 0) == 4 &&
            shadowFilterTaps(ShadowQuality::Medium, 1) == 1,
            "medium shadow quality did not apply its reduced distance and filtering");
    require(shadowConfig(ShadowQuality::Low).resolution == 768 &&
            shadowUpdateHz(ShadowQuality::Low) == 10.0f &&
            shadowConfig(ShadowQuality::High).resolution == 1536 &&
            shadowConfig(ShadowQuality::High).cascadeCount == 3 &&
            shadowFilterTaps(ShadowQuality::High, 0) == 4 &&
            shadowFilterTaps(ShadowQuality::High, 2) == 1 &&
            shadowUpdateHz(ShadowQuality::High) == 20.0f,
            "low/high shadow quality performance budgets are invalid");
    const glm::mat4 shadowView = glm::lookAt(glm::vec3(0.0f, 80.0f, 0.0f),
        glm::vec3(0.0f, 80.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 shadowProjection = glm::perspective(
        glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 640.0f);
    const auto cascades = buildShadowCascades(ShadowQuality::Medium,
        glm::inverse(shadowProjection * shadowView), shadowView,
        noon.lightDirection, 0.1f, 128.0f);
    require(cascades.count == 2 && cascades.resolution == 1024 &&
            cascades.splits.x > 0.1f && cascades.splits.x < cascades.splits.y &&
            cascades.splits.y <= 128.0f,
            "shadow cascade splits are invalid or ignored fog-distance clamping");

    // Switching back to an automatic cycle resumes from noon. Advance half a
    // cycle in bounded frame-sized steps to reach midnight.
    for (int i = 0; i < 6000; ++i) cycle.update(0.1f, 20, true);
    const RenderEnvironment midnight = cycle.evaluate();
    require(midnight.daylight < 0.05f, "half cycle did not reach night");
    require(midnight.starIntensity > 0.9f, "night sky has no stars");
    require(midnight.ambientIntensity >= Config::NIGHT_AMBIENT_MIN,
            "night ambient fell below playable minimum");
    require(glm::length(cloudColorForEnvironment(midnight)) <
                glm::length(noonCloud),
            "night clouds should be darker than daytime clouds");

    // One complete 10-minute cycle must wrap back to its starting phase.
    cycle.update(0.1f, 0, false);
    const float start = cycle.phase();
    for (int i = 0; i < 6000; ++i) cycle.update(0.1f, 10, true);
    require(std::abs(cycle.phase() - start) < 0.001f,
            "day cycle did not wrap deterministically");

    std::cout << "render environment logic passed\n";
}
