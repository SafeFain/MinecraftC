#pragma once

#include "model/ModelAsset.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

// Pipeline and render-pass creation for the Vulkan backend. Creation is
// grouped by lifecycle:
//   - createSwapchainSet: render passes and every pipeline that references
//     them; they are recreated together whenever the swapchain is rebuilt.
//   - createShadowSet: the shadow render pass/pipeline, created once with the
//     device and independent of the swapchain.
// The factory owns no Vulkan handles; outputs are stored by the caller.

namespace vkp {

// ── Push-constant and uniform-buffer layouts (shader ABI) ───────────────

struct FrameUniforms {
    glm::mat4 modelViewProjection{1.0f};
    glm::vec4 atlasAndLighting{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 chunkOrigin{0.0f};
    glm::vec4 tint{1.0f};
};

struct ChunkEnvironmentUniforms {
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 lightDirection{0.0f};
    glm::vec4 directColorIntensity{0.0f};
    glm::vec4 ambientColorIntensity{0.0f};
    glm::vec4 fogColorDistance{0.0f};
    glm::vec4 materialParams{0.0f};
    glm::vec4 weatherParams{0.0f};
    std::array<glm::mat4, 4> shadowMatrices{};
    glm::vec4 shadowSplits{0.0f};
    glm::vec4 shadowOptions{0.0f};
};

struct ShadowConstants {
    glm::mat4 lightMvp{1.0f};
    glm::vec4 atlasParams{1.0f};
};

struct ParticleUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraRightTime{0.0f};
    glm::vec4 cameraUpIntensity{0.0f};
    glm::vec4 atlasParams{1.0f};
};

struct SkyUniforms {
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 sunDirection{0.0f};
    glm::vec4 moonDirection{0.0f};
    glm::vec4 zenithColor{0.0f};
    glm::vec4 horizonColor{0.0f};
    glm::vec4 weather{0.0f};
    glm::vec4 options{0.0f};
};

struct CloudUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 origin{0.0f};
    glm::vec4 color{1.0f};
    glm::vec4 lighting{0.0f, 1.0f, 0.0f, 0.0f};
};

struct WireUniforms {
    glm::mat4 modelViewProjection{1.0f};
    glm::vec4 options{0.0f};
};

struct UiConstants {
    glm::mat4 projection{1.0f};
    glm::vec4 options{0.0f};
};

struct PostConstants {
    glm::vec4 exposureBloom{1.0f, 0.0f, 1.0f, 0.0f};
    glm::vec4 effects{0.0f};
    glm::vec4 texelTime{0.0f};
    glm::vec4 environment{0.0f};
};

struct ModelUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 model{1.0f};
    glm::mat4 node{1.0f};
    std::array<glm::mat4, model::MAX_JOINTS> joints{};
    glm::vec4 baseColor{1.0f};
    glm::vec4 params{0.0f};
    glm::vec4 cameraFogStart{0.0f};
    glm::vec4 fogColorEnd{0.0f};
    glm::vec4 lightDirection{0.0f};
    glm::vec4 directColor{0.0f};
    glm::vec4 ambientColor{0.0f};
    glm::vec4 options{0.0f};
};

static_assert(sizeof(SkyUniforms) == 176);
static_assert(offsetof(SkyUniforms, weather) == 144);
static_assert(offsetof(SkyUniforms, options) == 160);
static_assert(sizeof(CloudUniforms) == 112);
static_assert(sizeof(FrameUniforms) == 112);
static_assert(offsetof(FrameUniforms, chunkOrigin) == 80);
static_assert(sizeof(ChunkEnvironmentUniforms) == 400);
static_assert(offsetof(ChunkEnvironmentUniforms, materialParams) == 80);
static_assert(offsetof(ChunkEnvironmentUniforms, weatherParams) == 96);
static_assert(sizeof(WireUniforms) == 80);
static_assert(sizeof(UiConstants) == 80);
static_assert(sizeof(PostConstants) == 64);
static_assert(sizeof(ModelUniforms) == 4416);

// ── Factory inputs/outputs ──────────────────────────────────────────────

struct SwapchainPipelineInputs {
    VkDescriptorSetLayout chunkSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout chunkFrameLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout modelLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout postLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkFormat sceneFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    // Live scene image views used to bind the post-pass descriptors; the
    // caller rebuilds them before invoking createSwapchainSet.
    const std::vector<VkImageView>* sceneImageViews = nullptr;
};

struct SwapchainPipelineOutputs {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass presentRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline translucentPipeline = VK_NULL_HANDLE;
    VkPipeline uiPipeline = VK_NULL_HANDLE;
    VkPipeline postPipeline = VK_NULL_HANDLE;
    VkPipelineLayout postPipelineLayout = VK_NULL_HANDLE;
    VkSampler postSampler = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> postDescriptorSets;
    VkPipeline basicPipeline = VK_NULL_HANDLE;
    VkPipeline basicNoCullPipeline = VK_NULL_HANDLE;
    VkPipeline basicNoDepthPipeline = VK_NULL_HANDLE;
    VkPipeline basicNoDepthNoCullPipeline = VK_NULL_HANDLE;
    VkPipelineLayout modelPipelineLayout = VK_NULL_HANDLE;
    VkPipeline modelOpaquePipeline = VK_NULL_HANDLE;
    VkPipeline modelOpaqueDoubleSidedPipeline = VK_NULL_HANDLE;
    VkPipeline modelBlendPipeline = VK_NULL_HANDLE;
    VkPipeline modelBlendDoubleSidedPipeline = VK_NULL_HANDLE;
    VkPipelineLayout wirePipelineLayout = VK_NULL_HANDLE;
    VkPipeline wirePipeline = VK_NULL_HANDLE;
    VkPipelineLayout particlePipelineLayout = VK_NULL_HANDLE;
    VkPipeline particlePipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout cloudPipelineLayout = VK_NULL_HANDLE;
    VkPipeline skyPipeline = VK_NULL_HANDLE;
    VkPipeline cloudPipeline = VK_NULL_HANDLE;
};

struct ShadowPipelineInputs {
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDescriptorSetLayout chunkSetLayout = VK_NULL_HANDLE;
};

struct ShadowPipelineOutputs {
    VkFormat shadowFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
};

class VulkanPipelineFactory {
public:
    VulkanPipelineFactory(VkDevice device, std::filesystem::path shaderRoot);

    // Render passes plus every pipeline that references them, in the exact
    // creation order the swapchain rebuild path expects.
    void createSwapchainSet(const SwapchainPipelineInputs& inputs,
                            SwapchainPipelineOutputs& outputs) const;
    void createShadowSet(const ShadowPipelineInputs& inputs,
                         ShadowPipelineOutputs& outputs) const;

private:
    VkShaderModule loadShader(const std::filesystem::path& path) const;
    void createSecondaryPipelines(const SwapchainPipelineInputs& inputs,
                                  SwapchainPipelineOutputs& outputs) const;

    VkDevice m_device = VK_NULL_HANDLE;
    std::filesystem::path m_shaderRoot;
};

}  // namespace vkp
