#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "renderer/backend/vulkan/VulkanRendererInternal.h"

#include "core/AssetStore.h"
#include "core/RuntimeClock.h"
#include "core/Window.h"
#include "debug/Log.h"
#include "model/ModelRenderer.h"
#include "model/ModelRenderLogic.h"
#include "renderer/BlockAtlasData.h"
#include "renderer/CloudRenderData.h"
#include "renderer/backend/vulkan/VulkanHelpers.h"
#include "renderer/backend/vulkan/VulkanIndexRebase.h"
#include "renderer/backend/vulkan/VulkanPipelineFactory.h"
#include "renderer/backend/vulkan/VulkanResources.h"
#include "Config.h"
#include "world/Block.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <stb_image.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>




VulkanRenderer::VulkanRenderer() = default;

VulkanRenderer::VulkanRenderer(Window& window, const std::filesystem::path& assetRoot)
    : m_impl(std::make_unique<Impl>(window, assetRoot)) {}

VulkanRenderer::~VulkanRenderer() {
    if (!m_impl) return;
    waitIdle();
    m_modelRenderer.reset();
    for (RenderMeshHandle mesh : m_compatibilityCubes)
        if (mesh) destroyMesh(mesh);
    if (m_entityMaterial) destroyMaterial(m_entityMaterial);
    if (m_entityAtlas) destroyTexture(m_entityAtlas);
    if (m_chunkTranslucent) destroyMaterial(m_chunkTranslucent);
    if (m_chunkOpaque) destroyMaterial(m_chunkOpaque);
    if (m_blockPropertyAtlas) destroyTexture(m_blockPropertyAtlas);
    if (m_blockNormalAtlas) destroyTexture(m_blockNormalAtlas);
    if (m_neutralPropertyTexture) destroyTexture(m_neutralPropertyTexture);
    if (m_neutralNormalTexture) destroyTexture(m_neutralNormalTexture);
    if (m_blockAtlas) destroyTexture(m_blockAtlas);
}

RenderDeviceCapabilities VulkanRenderer::capabilities() const {
    return {true, true, true};
}

RenderMeshHandle VulkanRenderer::createMesh(const MeshData& data) {
    validateMeshData(data);
    if (m_impl->nextMeshHandle == 0)
        throw std::runtime_error("Vulkan mesh handle space exhausted");
    const RenderMeshHandle handle{m_impl->nextMeshHandle++};
    m_impl->meshes.emplace(handle.value, m_impl->createGeometry(data));
    return handle;
}

void VulkanRenderer::destroyMesh(RenderMeshHandle handle) {
    const auto found = m_impl->meshes.find(handle.value);
    if (found == m_impl->meshes.end())
        throw std::invalid_argument("Unknown Vulkan mesh handle");
    const size_t lastSubmittedFrame =
        (m_impl->currentFrame + Impl::FRAMES_IN_FLIGHT - 1) %
        Impl::FRAMES_IN_FLIGHT;
    m_impl->retiredMeshes[lastSubmittedFrame].push_back(
        std::move(found->second));
    m_impl->meshes.erase(found);
}

RenderTextureHandle VulkanRenderer::createTexture(
    const TextureData& data, const TextureSamplerDesc& sampler) {
    validateTextureData(data);
    if (m_impl->nextTextureHandle == 0)
        throw std::runtime_error("Vulkan texture handle space exhausted");
    const RenderTextureHandle handle{m_impl->nextTextureHandle++};
    m_impl->textures.emplace(handle.value,
                             m_impl->createTextureResource(data, sampler));
    return handle;
}

void VulkanRenderer::destroyTexture(RenderTextureHandle handle) {
    const auto found = m_impl->textures.find(handle.value);
    if (found == m_impl->textures.end())
        throw std::invalid_argument("Unknown Vulkan texture handle");
    for (const auto& [id, material] : m_impl->materials) {
        (void)id;
        if (material.desc.baseColorTexture == handle)
            throw std::logic_error("Texture is still referenced by a material");
    }
    auto& texture = found->second;
    m_impl->pendingImageUploads.erase(std::remove_if(
        m_impl->pendingImageUploads.begin(), m_impl->pendingImageUploads.end(),
        [&](const Impl::PendingImageUpload& upload) {
            return upload.destination == texture.image;
        }), m_impl->pendingImageUploads.end());
    if (texture.sampler) vkDestroySampler(m_impl->device, texture.sampler, nullptr);
    if (texture.view) vkDestroyImageView(m_impl->device, texture.view, nullptr);
    if (texture.image)
        vmaDestroyImage(m_impl->allocator, texture.image, texture.allocation);
    m_impl->textures.erase(found);
}

RenderMaterialHandle VulkanRenderer::createMaterial(const MaterialDesc& desc) {
    const auto texture = m_impl->textures.find(desc.baseColorTexture.value);
    if ((desc.pipeline != MaterialPipeline::UnlitTextured &&
         desc.pipeline != MaterialPipeline::ChunkOpaqueCutout &&
         desc.pipeline != MaterialPipeline::ChunkTranslucent &&
         desc.pipeline != MaterialPipeline::UiTextured) ||
        texture == m_impl->textures.end() ||
        (desc.pipeline != MaterialPipeline::UnlitTextured &&
         (!desc.depthTest || !desc.backfaceCull)))
        throw std::invalid_argument("Invalid Vulkan textured material");
    if (m_impl->nextMaterialHandle == 0)
        throw std::runtime_error("Vulkan material handle space exhausted");
    const RenderMaterialHandle handle{m_impl->nextMaterialHandle++};
    VulkanRenderer::Impl::GpuMaterial material;
    material.desc = desc;
    const bool blockAtlas = desc.baseColorTexture == m_blockAtlas;
    const auto normal = blockAtlas && m_blockNormalAtlas
        ? m_impl->textures.find(m_blockNormalAtlas.value)
        : m_neutralNormalTexture
            ? m_impl->textures.find(m_neutralNormalTexture.value) : texture;
    const auto properties = blockAtlas && m_blockPropertyAtlas
        ? m_impl->textures.find(m_blockPropertyAtlas.value)
        : m_neutralPropertyTexture
            ? m_impl->textures.find(m_neutralPropertyTexture.value) : texture;
    if (normal == m_impl->textures.end() || properties == m_impl->textures.end())
        throw std::logic_error("Vulkan block material maps are unavailable");
    material.descriptorSet = m_impl->createMaterialDescriptor(
        texture->second, normal->second, properties->second);
    m_impl->materials.emplace(handle.value, material);
    return handle;
}

void VulkanRenderer::destroyMaterial(RenderMaterialHandle handle) {
    const auto found = m_impl->materials.find(handle.value);
    if (found == m_impl->materials.end())
        throw std::invalid_argument("Unknown Vulkan material handle");
    require(vkFreeDescriptorSets(m_impl->device, m_impl->descriptors.descriptorPool, 1,
                                 &found->second.descriptorSet),
            "vkFreeDescriptorSets");
    m_impl->materials.erase(found);
}

void VulkanRenderer::beginFrame(const FrameData& frame) {
    if (m_impl->frameBegun)
        throw std::logic_error("Vulkan frame is already active");
    m_impl->submittedFrame = frame;
    m_impl->submittedChunkEnvironment.cameraPosition =
        glm::vec4(m_cameraPosition, 0.0f);
    m_impl->submittedChunkEnvironment.lightDirection =
        glm::vec4(frame.lightDirection, 0.0f);
    m_impl->submittedChunkEnvironment.directColorIntensity =
        glm::vec4(frame.directColor, 1.0f);
    m_impl->submittedChunkEnvironment.ambientColorIntensity =
        glm::vec4(frame.ambientColor, 1.0f);
    m_impl->submittedChunkEnvironment.fogColorDistance = glm::vec4(
        m_environment.fogColor,
        (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
            Config::CHUNK_SIZE_X);
    const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
    m_impl->submittedChunkEnvironment.materialParams = {
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Lava)),
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Water)),
        Config::FOG_START_FRACTION, visual.normalStrength};
    m_impl->submittedChunkEnvironment.weatherParams = {
        static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now())),
        m_environment.rainIntensity,
        visual.aoDirections > 0 ? std::min(1.0f, 0.58f +
            visual.aoDirections * 0.055f) : 0.0f,
        visual.cloudShadowSamples > 0
            ? 0.12f + 0.035f * visual.cloudShadowSamples : 0.0f};
    m_impl->frameBegun = true;
    m_impl->drawQueued = false;
    m_impl->sceneFinished = false;
    m_impl->postProcess = {};
    m_impl->submittedDraws.clear();
    m_impl->submittedLodDraws.clear();
    m_impl->submittedViewModels.clear();
    m_impl->queueViewModel = false;
    m_impl->submittedShadowChunks.clear();
    m_impl->shadowUpdateQueued = false;
    m_impl->submittedChunkEnvironment.shadowOptions = glm::vec4(0.0f);
    m_impl->submittedUi.clear();
    m_impl->submittedParticles.clear();
    m_impl->submittedModelOpaque.clear();
    m_impl->submittedModelBlend.clear();
    m_impl->skyQueued = false;
    m_impl->cloudsQueued = false;
    m_impl->wireQueued = false;
}

void VulkanRenderer::draw(const DrawCommand& command) {
    const auto mesh = m_impl->meshes.find(command.mesh.value);
    const auto material = m_impl->materials.find(command.material.value);
    if (!m_impl->frameBegun || mesh == m_impl->meshes.end() ||
        material == m_impl->materials.end())
        throw std::invalid_argument("Invalid Vulkan draw command");
    if (!isMeshMaterialCompatible(mesh->second.layout,
                                  material->second.desc.pipeline))
        throw std::invalid_argument("Vulkan mesh/material layout mismatch");
    const uint64_t count = command.indexCount ? command.indexCount : mesh->second.indexCount;
    if (static_cast<uint64_t>(command.firstIndex) + count > mesh->second.indexCount)
        throw std::invalid_argument("Vulkan draw range exceeds mesh indices");
    if (command.lod) m_impl->submittedLodDraws.push_back(command);
    else if (m_impl->queueViewModel) m_impl->submittedViewModels.push_back(command);
    else m_impl->submittedDraws.push_back(command);
    m_impl->drawQueued = true;
}

void VulkanRenderer::endFrame() {
    if (!m_impl->frameBegun)
        throw std::logic_error("No active Vulkan frame");
    const bool drawQueued = m_impl->drawQueued;
    m_impl->frameBegun = false;
    m_impl->drawQueued = false;
    if (drawQueued) m_impl->drawFrame();
}

void VulkanRenderer::resize(int, int) { m_impl->swapchainDirty = true; }

void VulkanRenderer::waitIdle() {
    if (m_impl->device) require(vkDeviceWaitIdle(m_impl->device), "vkDeviceWaitIdle");
}

RendererPerformanceStats VulkanRenderer::performanceStats() const {
    return m_impl ? m_impl->performance : RendererPerformanceStats{};
}

void VulkanRenderer::initialize(Window& window,
                                const std::filesystem::path& assetRoot) {
    if (m_impl) throw std::logic_error("VulkanRenderer is already initialized");
    m_window = &window;
    m_assetRoot = assetRoot;
    m_impl = std::make_unique<Impl>(window, assetRoot);
    const BlockAtlasData atlas = buildBlockAtlasData(assetRoot);
    m_blockAtlasTilesPerSide = atlas.tilesPerSide;
    TextureSamplerDesc sampler;
    sampler.minFilter = TextureFilter::NearestMipmapLinear;
    sampler.addressU = TextureAddressMode::ClampToEdge;
    sampler.addressV = TextureAddressMode::ClampToEdge;
    m_blockAtlas = createTexture(atlas.texture, sampler);
    m_blockNormalAtlas = createTexture(atlas.normalTexture, sampler);
    m_blockPropertyAtlas = createTexture(atlas.propertyTexture, sampler);
    TextureData neutralNormal;
    neutralNormal.width = neutralNormal.height = 1;
    neutralNormal.format = TextureFormat::Rgba8Unorm;
    neutralNormal.pixels = {128, 128, 255, 255};
    TextureData neutralProperties = neutralNormal;
    neutralProperties.pixels = {202, 0, 0, 128};
    m_neutralNormalTexture = createTexture(neutralNormal, {});
    m_neutralPropertyTexture = createTexture(neutralProperties, {});
    MaterialDesc material;
    material.pipeline = MaterialPipeline::ChunkOpaqueCutout;
    material.baseColorTexture = m_blockAtlas;
    material.atlasTilesPerSide = atlas.tilesPerSide;
    m_chunkOpaque = createMaterial(material);
    m_impl->particleMaterial=m_chunkOpaque;
    m_impl->modelFallbackMaterial=m_chunkOpaque;
    material.pipeline = MaterialPipeline::ChunkTranslucent;
    m_chunkTranslucent = createMaterial(material);
    TextureSamplerDesc entitySampler;
    entitySampler.addressU = TextureAddressMode::ClampToEdge;
    entitySampler.addressV = TextureAddressMode::ClampToEdge;
    m_entityAtlas = createTexture(loadRgbaTexture(
        assetRoot / "textures" / "generated" / "entity_atlas.png"), entitySampler);
    material.pipeline = MaterialPipeline::ChunkOpaqueCutout;
    material.baseColorTexture = m_entityAtlas;
    material.atlasTilesPerSide = 3;
    m_entityMaterial = createMaterial(material);
    m_modelRenderer = model::createVulkanModelRenderer(*this);
}

void VulkanRenderer::reinitialize(const std::filesystem::path& assetRoot) {
    if (!m_window) throw std::logic_error("VulkanRenderer has no window");
    waitIdle();
    m_modelRenderer.reset();
    for (RenderMeshHandle mesh : m_compatibilityCubes)
        if (mesh) destroyMesh(mesh);
    if (m_entityMaterial) destroyMaterial(m_entityMaterial);
    if (m_entityAtlas) destroyTexture(m_entityAtlas);
    if (m_chunkTranslucent) destroyMaterial(m_chunkTranslucent);
    if (m_chunkOpaque) destroyMaterial(m_chunkOpaque);
    if (m_blockPropertyAtlas) destroyTexture(m_blockPropertyAtlas);
    if (m_blockNormalAtlas) destroyTexture(m_blockNormalAtlas);
    if (m_neutralPropertyTexture) destroyTexture(m_neutralPropertyTexture);
    if (m_neutralNormalTexture) destroyTexture(m_neutralNormalTexture);
    if (m_blockAtlas) destroyTexture(m_blockAtlas);
    m_chunkTranslucent = {};
    m_chunkOpaque = {};
    m_blockAtlas = {};
    m_blockNormalAtlas = {};
    m_blockPropertyAtlas = {};
    m_neutralNormalTexture = {};
    m_neutralPropertyTexture = {};
    m_blockAtlasTilesPerSide = 0;
    m_entityMaterial = {};
    m_entityAtlas = {};
    m_compatibilityCubes.fill({});
    m_impl.reset();
    initialize(*m_window, assetRoot);
}

void VulkanRenderer::suspendPresentation() {
    if (m_impl) m_impl->suspendPresentation();
}

void VulkanRenderer::resumePresentation() {
    if (m_impl) m_impl->resumePresentation();
}

void VulkanRenderer::beginFrame() {
    FrameData frame;
    frame.projection = m_viewProjection;
    frame.ambientColor = m_environment.ambientColor * m_environment.ambientIntensity;
    frame.directColor = m_environment.directColor * m_environment.directIntensity;
    frame.lightDirection = m_environment.lightDirection;
    beginFrame(frame);
}

void VulkanRenderer::setVisualQuality(VisualQuality quality) {
    m_visualQuality = quality;
    if (m_impl) m_impl->configureVisualQuality(quality);
}

void VulkanRenderer::finishScene(const PostProcessState& state) {
    if (!m_impl || !m_impl->frameBegun)
        throw std::logic_error("Vulkan scene composition requires an active frame");
    m_impl->postProcess = state;
    m_impl->sceneFinished = true;
    // Composition also presents an intentionally empty scene, such as menus.
    m_impl->drawQueued = true;
}

void VulkanRenderer::setViewProjection(const glm::mat4& value) {
    m_viewProjection = value;
    // Gameplay supplies the camera after beginFrame(). Keep the active frame
    // synchronized instead of rendering Chunks with the previous frame's view.
    if (m_impl && m_impl->frameBegun) {
        m_impl->submittedFrame.projection = value;
        m_impl->submittedFrame.view = glm::mat4(1.0f);
    }
}

void VulkanRenderer::setEnvironment(const RenderEnvironment& environment,
                                    const glm::vec3& cameraPosition) {
    m_environment = environment;
    m_cameraPosition = cameraPosition;
    if (!m_impl) return;
    ChunkEnvironmentUniforms& chunk = m_impl->submittedChunkEnvironment;
    chunk.cameraPosition = glm::vec4(cameraPosition, 0.0f);
    chunk.lightDirection = glm::vec4(environment.lightDirection, 0.0f);
    chunk.directColorIntensity = glm::vec4(
        environment.directColor, environment.directIntensity);
    chunk.ambientColorIntensity = glm::vec4(
        environment.ambientColor, environment.ambientIntensity);
    chunk.fogColorDistance = glm::vec4(
        environment.fogColor,
        (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
            Config::CHUNK_SIZE_X);
    const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
    chunk.materialParams = {
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Lava)),
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Water)),
        Config::FOG_START_FRACTION, visual.normalStrength};
    chunk.weatherParams = {
        static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now())),
        environment.rainIntensity,
        visual.aoDirections > 0 ? std::min(1.0f, 0.58f +
            visual.aoDirections * 0.055f) : 0.0f,
        visual.cloudShadowSamples > 0
            ? 0.12f + 0.035f * visual.cloudShadowSamples : 0.0f};
}

void VulkanRenderer::renderSky(const RenderEnvironment& environment,
                               const glm::mat4& inverseViewProjection,
                               const glm::vec3& cameraPosition,
                               bool renderClouds) {
    if (!m_impl || !m_impl->frameBegun) return;
    m_impl->submittedFrame.clearColor = glm::vec4(environment.zenithColor, 1.0f);
    SkyUniforms& sky = m_impl->submittedSky;
    const glm::mat4 viewProjection = glm::inverse(inverseViewProjection);
    sky.inverseViewProjection = glm::inverse(
        clipSpaceCorrection() * viewProjection);
    sky.cameraPosition = glm::vec4(cameraPosition, 0.0f);
    sky.sunDirection = glm::vec4(environment.sunDirection, 0.0f);
    sky.moonDirection = glm::vec4(environment.moonDirection, 0.0f);
    sky.zenithColor = glm::vec4(environment.zenithColor, 0.0f);
    sky.horizonColor = glm::vec4(environment.horizonColor, 0.0f);
    sky.weather = {environment.starIntensity, environment.rainIntensity,
                   environment.thunderIntensity,
                   static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now()))};
    const VisualQualityConfig visual = visualQualityConfig(m_visualQuality);
    sky.options = {renderClouds && visual.voxelClouds ? 1.0f : 0.0f,
                   environment.skyStyle == RenderSkyStyle::Heaven ? 1.0f : 0.0f,
                   visual.cirrusClouds ? 1.0f : 0.0f,
                   static_cast<float>(static_cast<int>(m_visualQuality))};
    m_impl->skyQueued = true;
    m_impl->drawQueued = true;
}

void VulkanRenderer::uploadChunkMesh(ChunkMesh& mesh) {
    if (mesh.empty()) {
        releaseChunkMesh(mesh);
        return;
    }
    if (mesh.renderHandle) destroyMesh(mesh.renderHandle);
    if (m_impl->nextMeshHandle == 0)
        throw std::runtime_error("Vulkan mesh handle space exhausted");
    const RenderMeshHandle handle{m_impl->nextMeshHandle++};
    Impl::GpuMesh geometry = m_impl->createChunkGeometry(mesh);
    try {
        m_impl->meshes.emplace(handle.value, std::move(geometry));
    } catch (...) {
        m_impl->destroyGpuMesh(geometry);
        throw;
    }
    mesh.renderHandle = handle;
    mesh.indexCount = mesh.indices.size();
    mesh.gpuReady = true;
}

void VulkanRenderer::releaseChunkMesh(ChunkMesh& mesh) {
    if (mesh.renderHandle) destroyMesh(mesh.renderHandle);
    mesh.abandonGpuResources();
}

void VulkanRenderer::renderChunk(const ChunkMesh& mesh,
                                 const glm::mat4& model,
                                 const glm::mat4& viewProjection,
                                 bool translucent) {
    if (!mesh.gpuReady || !mesh.renderHandle) return;
    // The explicit argument is authoritative for this draw path. This also
    // prevents future call-order changes from reintroducing one-frame camera
    // latency into the world while other passes use the current matrix.
    m_impl->submittedFrame.projection = viewProjection;
    m_impl->submittedFrame.view = glm::mat4(1.0f);
    const size_t count = translucent ? mesh.translucentIndexCount
                                     : mesh.opaqueIndexCount;
    if (count == 0) return;
    DrawCommand command;
    command.mesh = mesh.renderHandle;
    command.material = translucent ? m_chunkTranslucent : m_chunkOpaque;
    command.model = model;
    command.firstIndex = static_cast<uint32_t>(translucent
        ? mesh.translucentIndexOffset : 0);
    command.indexCount = static_cast<uint32_t>(count);
    draw(command);
}

void VulkanRenderer::renderLod(const ChunkMesh& mesh,
                               const glm::mat4& model,
                               const glm::mat4& viewProjection,
                               float minimumDistance,
                               float maximumDistance,
                               bool translucent) {
    if (!mesh.gpuReady || !mesh.renderHandle) return;
    const size_t count = translucent ? mesh.translucentIndexCount
                                     : mesh.opaqueIndexCount;
    if (count == 0) return;
    DrawCommand command;
    command.mesh = mesh.renderHandle;
    command.material = translucent ? m_chunkTranslucent : m_chunkOpaque;
    command.model = model;
    command.viewProjection = viewProjection;
    command.useCustomViewProjection = true;
    command.firstIndex = static_cast<uint32_t>(translucent
        ? mesh.translucentIndexOffset : 0);
    command.indexCount = static_cast<uint32_t>(count);
    command.lod = true;
    command.lodMinimumDistance = minimumDistance;
    command.lodMaximumDistance = maximumDistance;
    m_impl->submittedChunkEnvironment.fogColorDistance.a = std::max(
        m_impl->submittedChunkEnvironment.fogColorDistance.a,
        maximumDistance + Config::CHUNK_SIZE_X);
    draw(command);
}

void VulkanRenderer::renderChunkShadows(
        ShadowQuality quality, const glm::mat4& inverseViewProjection,
        const glm::mat4& view, const glm::dvec3& worldOrigin,
        const std::vector<ShadowChunkSubmission>& chunks) {
    m_impl->submittedShadowChunks.clear();
    m_impl->shadowUpdateQueued=false;
    ChunkEnvironmentUniforms& environment=m_impl->submittedChunkEnvironment;
    environment.shadowOptions=glm::vec4(0.0f);
    if(quality==ShadowQuality::Off||m_environment.daylight<0.12f||
       m_environment.directIntensity<0.08f||chunks.empty())return;
    const bool qualityChanged=quality!=m_impl->shadow.shadowQuality;
    if(qualityChanged){
        require(vkDeviceWaitIdle(m_impl->device),"vkDeviceWaitIdle(shadow quality)");
        m_impl->createShadowImage(quality);
    }
    const RuntimeClock::Tick now=RuntimeClock{}.now();
    const bool moved=glm::distance(worldOrigin,m_impl->lastShadowWorldOrigin)>=
        shadowMovementThreshold(quality);
    const float lightDelta=glm::length(glm::normalize(m_environment.lightDirection)-
        glm::normalize(m_impl->lastShadowDirection));
    const bool timeDue=RuntimeClock::elapsed(m_impl->lastShadowUpdate,now)>=
        RuntimeClock::fromSeconds(1.0/shadowUpdateHz(quality));
    const bool updateShadow=qualityChanged||m_impl->lastShadowUpdate==0||
        moved||lightDelta>=0.01f||(timeDue&&lightDelta>=0.0002f);
    const float fogDistance=(static_cast<float>(Config::RENDER_DISTANCE)+0.5f)*
                            Config::CHUNK_SIZE_X;
    const glm::mat4 correction=clipSpaceCorrection();
    if(updateShadow){
        m_impl->shadowCascades=buildShadowCascades(quality,inverseViewProjection,view,
            m_environment.lightDirection,Config::NEAR_PLANE,fogDistance);
        for(int i=0;i<m_impl->shadowCascades.count;++i)
            m_impl->shadowCascades.lightViewProjection[i]=correction*
                m_impl->shadowCascades.lightViewProjection[i];
        m_impl->shadowBaseCascades=m_impl->shadowCascades;
        m_impl->lastShadowUpdate=now;
        m_impl->lastShadowWorldOrigin=worldOrigin;
        m_impl->lastShadowDirection=m_environment.lightDirection;
        m_impl->submittedShadowChunks=chunks;
        m_impl->shadowUpdateQueued=true;
    }else{
        m_impl->shadowCascades=m_impl->shadowBaseCascades;
        const glm::dvec3 delta=worldOrigin-m_impl->lastShadowWorldOrigin;
        const glm::mat4 translation=glm::translate(glm::mat4(1.0f),glm::vec3(
            static_cast<float>(delta.x),0.0f,static_cast<float>(delta.z)));
        for(int i=0;i<m_impl->shadowCascades.count;++i)
            m_impl->shadowCascades.lightViewProjection[i]*=translation;
    }
    for(int i=0;i<m_impl->shadowCascades.count;++i){
        environment.shadowMatrices[i]=m_impl->shadowCascades.lightViewProjection[i];
    }
    environment.shadowSplits=m_impl->shadowCascades.splits;
    environment.shadowOptions={static_cast<float>(m_impl->shadowCascades.count),
        static_cast<float>(m_impl->shadowCascades.resolution),
        m_impl->shadowCascades.count<=1?1.0f:2.0f,0.0f};
    const auto material=m_impl->materials.find(m_chunkOpaque.value);
    m_impl->shadowAtlasSet=material==m_impl->materials.end()?VK_NULL_HANDLE:
        material->second.descriptorSet;
    m_impl->submittedShadowAtlasTiles=m_blockAtlasTilesPerSide;
}

void VulkanRenderer::beginTranslucent() {}
void VulkanRenderer::endTranslucent() {}
void VulkanRenderer::bindBlockShader() const {}
void VulkanRenderer::unbindBlockShader() const {}
void VulkanRenderer::renderWireframe(const glm::vec3& blockPosition,
                                     const glm::vec3& blockSize,
                                     const glm::mat4& viewProjection) {
    if (!m_impl || !m_impl->frameBegun) return;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), blockPosition);
    model = glm::translate(model, blockSize * 0.5f);
    model = glm::scale(model, blockSize * 1.003f);
    model = glm::translate(model, glm::vec3(-0.5f));
    m_impl->wireModelViewProjection =
        clipSpaceCorrection() * viewProjection * model;
    m_impl->wireQueued = true;
    m_impl->drawQueued = true;
}
void VulkanRenderer::renderEntity(const glm::vec3& position, const glm::vec3& size,
                                  const glm::vec3& color, int textureIndex,
                                  const glm::mat4& viewProjection) {
    renderCompatibilityEntityCube(position, size, color, textureIndex,
                                  0.0f, viewProjection);
}
void VulkanRenderer::renderCompatibilityEntityCube(
    const glm::vec3& position, const glm::vec3& size, const glm::vec3& color,
    int textureIndex, float yaw, const glm::mat4&, SmoothLightSample light) {
    const size_t slot = static_cast<size_t>(std::clamp(textureIndex, 0, 8));
    RenderMeshHandle& compatibilityCube = m_compatibilityCubes[slot];
    if (!compatibilityCube) {
        MeshData mesh;
        mesh.layout = MeshVertexLayout::Chunk;
        const float tile = static_cast<float>(slot);
        constexpr float sky = 1.0f;
        constexpr float block = 0.0f;
        const auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c,
                              glm::vec3 d, float direction) {
            const uint32_t base = static_cast<uint32_t>(mesh.chunkVertices.size());
            mesh.chunkVertices.insert(mesh.chunkVertices.end(), {
                {a.x,a.y,a.z,1,sky,block,1,0,0,tile,direction},
                {b.x,b.y,b.z,1,sky,block,1,1,0,tile,direction},
                {c.x,c.y,c.z,1,sky,block,1,1,1,tile,direction},
                {d.x,d.y,d.z,1,sky,block,1,0,1,tile,direction}});
            mesh.indices.insert(mesh.indices.end(),
                {base,base+1,base+2,base,base+2,base+3});
        };
        face({-.5f,0,-.5f},{.5f,0,-.5f},{.5f,1,-.5f},{-.5f,1,-.5f},0);
        face({.5f,0,.5f},{-.5f,0,.5f},{-.5f,1,.5f},{.5f,1,.5f},1);
        face({-.5f,0,.5f},{-.5f,0,-.5f},{-.5f,1,-.5f},{-.5f,1,.5f},2);
        face({.5f,0,-.5f},{.5f,0,.5f},{.5f,1,.5f},{.5f,1,-.5f},3);
        face({-.5f,1,-.5f},{.5f,1,-.5f},{.5f,1,.5f},{-.5f,1,.5f},4);
        face({-.5f,0,.5f},{.5f,0,.5f},{.5f,0,-.5f},{-.5f,0,-.5f},5);
        mesh.opaqueIndexCount = static_cast<uint32_t>(mesh.indices.size());
        compatibilityCube = createMesh(mesh);
    }
    DrawCommand command;
    command.mesh = compatibilityCube;
    command.material = m_entityMaterial;
    command.model = glm::translate(glm::mat4(1.0f), position) *
                    glm::rotate(glm::mat4(1.0f), yaw,
                                glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), size);
    const float skyLight = std::pow(std::clamp(light.sky, 0.0f, 1.0f), 1.20f);
    const float blockLight = std::pow(std::clamp(light.block, 0.0f, 1.0f), 1.35f);
    const glm::vec3 illumination = glm::max(glm::vec3(skyLight),
        glm::vec3(1.0f, 0.72f, 0.38f) * blockLight);
    command.tint = glm::vec4(color * glm::max(illumination, glm::vec3(0.025f)), 1.0f);
    draw(command);
}
model::ModelRenderer& VulkanRenderer::modelRenderer() { return *m_modelRenderer; }
void VulkanRenderer::flushModels(const glm::mat4& viewProjection) {
    const float fogEnd = (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
                         Config::CHUNK_SIZE_X;
    m_modelRenderer->flushOpaque(viewProjection, m_environment,
        glm::vec3(0.0f), fogEnd * Config::FOG_START_FRACTION, fogEnd);
    m_modelRenderer->flushBlend(viewProjection, m_environment,
        glm::vec3(0.0f), fogEnd * Config::FOG_START_FRACTION, fogEnd);
}
void VulkanRenderer::beginViewModel(const glm::mat4& projection) {
    if (!m_impl || !m_impl->frameBegun)
        throw std::logic_error("Vulkan view model requires an active frame");
    m_impl->queueViewModel = true;
    (void)projection;
}
void VulkanRenderer::renderEntityPart(const glm::vec3& position,
    const glm::vec3& offset, const glm::vec3& size, float,
    const glm::vec3& color, int textureIndex, const glm::mat4& viewProjection,
    SmoothLightSample light) {
    renderCompatibilityEntityCube(position + offset, size, color, textureIndex,
                                  0.0f, viewProjection, light);
}
void VulkanRenderer::renderParticles(
    const std::vector<ParticleRenderData>& particles,
    const glm::mat4& viewProjection,const glm::vec3& cameraRight,
    const glm::vec3& cameraUp,float intensity) {
    if(!m_impl||!m_impl->frameBegun)
        throw std::logic_error("Vulkan particles require an active frame");
    if(particles.size()>ParticleSystem::MAX_PARTICLES)
        throw std::invalid_argument("Vulkan particle count exceeds capacity");
    m_impl->submittedParticles=particles;
    if(particles.empty())return;
    m_impl->particleViewProjection=viewProjection;
    m_impl->particleCameraRight=cameraRight;
    m_impl->particleCameraUp=cameraUp;
    m_impl->particleIntensity=std::clamp(intensity,0.0f,1.0f);
    m_impl->particleTime=static_cast<float>(
        RuntimeClock::seconds(RuntimeClock{}.now()));
    m_impl->drawQueued=true;
}
void VulkanRenderer::renderClouds(const glm::dvec3& playerPosition,
                                  const glm::mat4& viewProjection,
                                  uint64_t worldSeed, float timeSeconds,
                                  int renderDistanceBlocks) {
    if (m_environment.skyStyle == RenderSkyStyle::Heaven) return;
    if (!m_impl || !m_impl->frameBegun)
        throw std::logic_error("Vulkan clouds require an active frame");
    if (!visualQualityConfig(m_visualQuality).voxelClouds) return;
    const CloudView view = cloudView(
        playerPosition, timeSeconds, renderDistanceBlocks);
    const bool rebuild = m_impl->cloudCacheRadius != view.radius ||
        m_impl->cloudCacheCenterX != view.centerX ||
        m_impl->cloudCacheCenterZ != view.centerZ ||
        m_impl->cloudCacheSeed != worldSeed;
    if (rebuild) {
        m_impl->cloudInstances = buildCloudInstances(
            worldSeed, view.centerX, view.centerZ, view.radius);
        m_impl->cloudCacheRadius = view.radius;
        m_impl->cloudCacheCenterX = view.centerX;
        m_impl->cloudCacheCenterZ = view.centerZ;
        m_impl->cloudCacheSeed = worldSeed;
        if (++m_impl->cloudRevision == 0) ++m_impl->cloudRevision;
    }
    if (m_impl->cloudInstances.empty()) return;
    m_impl->cloudOrigin = view.origin;
    m_impl->cloudColor = cloudColorForEnvironment(m_environment);
    m_impl->cloudViewProjection = viewProjection;
    m_impl->cloudsQueued = true;
    m_impl->drawQueued = true;
}

void VulkanRenderer::queueUiBatch(const std::vector<UiMeshVertex>& vertices,
                                  const std::vector<uint32_t>& indices,
                                  RenderMaterialHandle material,
                                  const glm::mat4& projection) {
    if (!m_impl || !m_impl->frameBegun || vertices.empty() || indices.empty() ||
        m_impl->materials.find(material.value) == m_impl->materials.end())
        return;
    Impl::UiSubmission batch;
    batch.vertices = vertices;
    batch.indices = indices;
    batch.material = material;
    batch.projection = projection;
    m_impl->submittedUi.push_back(std::move(batch));
    m_impl->drawQueued = true;
}
