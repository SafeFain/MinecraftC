#pragma once

#include "renderer/RenderDevice.h"
#include "renderer/Frustum.h"
#include "renderer/ParticleSystem.h"
#include "renderer/RenderEnvironment.h"
#include "renderer/Shadow.h"
#include "renderer/VisualQuality.h"
#include "world/BlockLightLogic.h"

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <vector>

class Window;
struct ChunkMesh;
struct ShadowChunkSubmission {
    const ChunkMesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};
namespace model { class ModelRenderer; }

class IGameRenderer : public IRenderDevice {
public:
    using IRenderDevice::beginFrame;
    virtual void initialize(Window& window,
                            const std::filesystem::path& assetRoot) = 0;
    virtual void reinitialize(const std::filesystem::path& assetRoot) = 0;
    virtual void suspendPresentation() {}
    virtual void resumePresentation() {}
    virtual void beginFrame() = 0;
    virtual void setVisualQuality(VisualQuality) = 0;
    virtual void setLeafTransparency(bool) = 0;
    virtual void finishScene(const PostProcessState&) = 0;
    virtual void setEnvironment(const RenderEnvironment&, const glm::vec3&) = 0;
    virtual void renderSky(const RenderEnvironment&, const glm::mat4&,
                           const glm::vec3&, bool) = 0;
    virtual void renderChunk(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                             bool translucent = false) = 0;
    virtual void renderLod(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                           const glm::vec2& worldOffset,
                           float minimumDistance, float maximumDistance,
                           bool translucent = false) = 0;
    virtual void renderChunkShadows(ShadowQuality, const glm::mat4& inverseViewProjection,
        const glm::mat4& view, const glm::dvec3& worldOrigin,
        const std::vector<ShadowChunkSubmission>&) = 0;
    virtual void uploadChunkMesh(ChunkMesh&) = 0;
    virtual void releaseChunkMesh(ChunkMesh&) = 0;
    virtual void beginTranslucent() = 0;
    virtual void endTranslucent() = 0;
    virtual void bindBlockShader() const = 0;
    virtual void unbindBlockShader() const = 0;
    virtual void renderWireframe(const glm::vec3&, const glm::vec3&,
                                 const glm::mat4&) = 0;
    virtual void renderEntity(const glm::vec3&, const glm::vec3&, const glm::vec3&,
                              int, const glm::mat4&) = 0;
    virtual void renderCompatibilityEntityCube(
        const glm::vec3&, const glm::vec3&, const glm::vec3&, int,
        float, const glm::mat4&, SmoothLightSample light = {}) = 0;
    virtual model::ModelRenderer& modelRenderer() = 0;
    virtual void flushModels(const glm::mat4&) = 0;
    virtual void beginViewModel(const glm::mat4& projection) = 0;
    virtual void renderEntityPart(const glm::vec3&, const glm::vec3&,
                                  const glm::vec3&, float, const glm::vec3&, int,
                                  const glm::mat4&,
                                  SmoothLightSample light = {1.0f, 0.0f}) = 0;
    virtual void renderParticles(const std::vector<ParticleRenderData>&,
                                 const glm::mat4&, const glm::vec3&,
                                 const glm::vec3&, float) = 0;
    virtual void renderClouds(const glm::dvec3&, const glm::mat4&, uint64_t,
                              float, int) = 0;
    virtual void setViewProjection(const glm::mat4&) = 0;
    virtual void setFrustum(const Frustum&) = 0;
    virtual const Frustum& getFrustum() const = 0;
    virtual RenderTextureHandle getBlockAtlasTexture() const = 0;
    virtual uint32_t blockAtlasTilesPerSide() const = 0;
};
