#include "renderer/backend/vulkan/VulkanPipelineFactory.h"

#include "core/AssetStore.h"
#include "renderer/CloudRenderData.h"
#include "renderer/ParticleSystem.h"
#include "renderer/RenderDevice.h"
#include "renderer/backend/vulkan/VulkanHelpers.h"
#include "world/ChunkMesh.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace vkp {

using vkhelp::require;

namespace {
const std::vector<VkImageView> g_emptyImageViews;

// The original implementation wrote handles directly into the renderer and
// relied on its cleanup path to release partially created resources when
// creation failed. The factory writes into output structs instead, so it
// must roll back its own partial creations on failure; on success the
// caller adopts the outputs and the guards are dismissed.
struct ShadowSetGuard {
    ShadowPipelineOutputs& out;
    VkDevice device;
    bool active = true;
    ~ShadowSetGuard() {
        if (!active) return;
        if (out.shadowPipeline)
            vkDestroyPipeline(device, out.shadowPipeline, nullptr);
        if (out.shadowPipelineLayout)
            vkDestroyPipelineLayout(device, out.shadowPipelineLayout, nullptr);
        if (out.shadowRenderPass)
            vkDestroyRenderPass(device, out.shadowRenderPass, nullptr);
    }
};

struct SwapchainSetGuard {
    SwapchainPipelineOutputs& out;
    VkDevice device;
    VkDescriptorPool pool;
    bool active = true;
    ~SwapchainSetGuard() {
        if (!active) return;
        for (auto& sets : out.bloomDescriptorSets)
            if (!sets.empty() && pool)
                vkFreeDescriptorSets(device, pool,
                    static_cast<uint32_t>(sets.size()), sets.data());
        if (!out.postDescriptorSets.empty() && pool)
            vkFreeDescriptorSets(device, pool,
                                 static_cast<uint32_t>(
                                     out.postDescriptorSets.size()),
                                 out.postDescriptorSets.data());
        if (out.postSampler) vkDestroySampler(device, out.postSampler, nullptr);
        if (out.postPipeline) vkDestroyPipeline(device, out.postPipeline, nullptr);
        if (out.postPipelineLayout)
            vkDestroyPipelineLayout(device, out.postPipelineLayout, nullptr);
        if (out.bloomPipeline)
            vkDestroyPipeline(device, out.bloomPipeline, nullptr);
        if (out.bloomPipelineLayout)
            vkDestroyPipelineLayout(device, out.bloomPipelineLayout, nullptr);
        for (VkPipeline pipeline : {out.basicPipeline, out.basicNoCullPipeline,
                 out.basicNoDepthPipeline, out.basicNoDepthNoCullPipeline})
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        for (VkPipeline pipeline : {out.modelOpaquePipeline,
                 out.modelOpaqueDoubleSidedPipeline, out.modelBlendPipeline,
                 out.modelBlendDoubleSidedPipeline})
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (out.modelPipelineLayout)
            vkDestroyPipelineLayout(device, out.modelPipelineLayout, nullptr);
        if (out.wirePipeline) vkDestroyPipeline(device, out.wirePipeline, nullptr);
        if (out.wirePipelineLayout)
            vkDestroyPipelineLayout(device, out.wirePipelineLayout, nullptr);
        if (out.particlePipeline)
            vkDestroyPipeline(device, out.particlePipeline, nullptr);
        if (out.particlePipelineLayout)
            vkDestroyPipelineLayout(device, out.particlePipelineLayout, nullptr);
        if (out.cloudPipeline) vkDestroyPipeline(device, out.cloudPipeline, nullptr);
        if (out.cloudPipelineLayout)
            vkDestroyPipelineLayout(device, out.cloudPipelineLayout, nullptr);
        if (out.skyPipeline) vkDestroyPipeline(device, out.skyPipeline, nullptr);
        if (out.skyPipelineLayout)
            vkDestroyPipelineLayout(device, out.skyPipelineLayout, nullptr);
        if (out.uiPipeline) vkDestroyPipeline(device, out.uiPipeline, nullptr);
        if (out.translucentPipeline)
            vkDestroyPipeline(device, out.translucentPipeline, nullptr);
        if (out.pipeline) vkDestroyPipeline(device, out.pipeline, nullptr);
        if (out.pipelineLayout)
            vkDestroyPipelineLayout(device, out.pipelineLayout, nullptr);
        if (out.presentRenderPass)
            vkDestroyRenderPass(device, out.presentRenderPass, nullptr);
        if (out.bloomRenderPass)
            vkDestroyRenderPass(device, out.bloomRenderPass, nullptr);
        if (out.renderPass) vkDestroyRenderPass(device, out.renderPass, nullptr);
    }
};
}  // namespace

VulkanPipelineFactory::VulkanPipelineFactory(VkDevice device,
                                             std::filesystem::path shaderRoot)
    : m_device(device), m_shaderRoot(std::move(shaderRoot)) {}

VkShaderModule VulkanPipelineFactory::loadShader(
    const std::filesystem::path& path) const {
    const std::vector<uint8_t> bytes = AssetStore::readPath(path);
    if (bytes.size() < sizeof(uint32_t) || bytes.size() % sizeof(uint32_t) != 0)
        throw std::runtime_error("Invalid SPIR-V shader: " + path.string());
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = bytes.size();
    info.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
    VkShaderModule module = VK_NULL_HANDLE;
    require(vkCreateShaderModule(m_device, &info, nullptr, &module),
            "vkCreateShaderModule");
    return module;
}

void VulkanPipelineFactory::createShadowSet(
    const ShadowPipelineInputs& inputs, ShadowPipelineOutputs& outputs) const {
    ShadowSetGuard guard{outputs, m_device};
    constexpr std::array<VkFormat, 3> candidates{
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
    for (VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(inputs.physicalDevice, candidate,
                                            &properties);
        const VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) == required) {
            outputs.shadowFormat = candidate;
            break;
        }
    }
    if (outputs.shadowFormat == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("No sampleable Vulkan shadow depth format");
    VkAttachmentDescription attachment{};
    attachment.format = outputs.shadowFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    const VkAttachmentReference depthRef{
        0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = dependencies.size();
    pass.pDependencies = dependencies.data();
    require(vkCreateRenderPass(m_device, &pass, nullptr,
                               &outputs.shadowRenderPass),
            "vkCreateRenderPass(shadow)");

    VkShaderModule vertex = loadShader(m_shaderRoot / "shadow.vert.spv");
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        fragment = loadShader(m_shaderRoot / "shadow.frag.spv");
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
        const VkVertexInputBindingDescription binding{
            0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 3> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, px)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, u)},
            {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(MeshVertex, face)}}};
        VkPipelineVertexInputStateCreateInfo input{};
        input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = attributes.size();
        input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 4.0f;
        raster.depthBiasSlopeFactor = 2.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        const std::array<VkDynamicState, 2> states{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = states.size();
        dynamic.pDynamicStates = states.data();
        VkPushConstantRange range{
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            sizeof(ShadowConstants)};
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &inputs.chunkSetLayout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        require(vkCreatePipelineLayout(m_device, &layout, nullptr,
                                       &outputs.shadowPipelineLayout),
                "vkCreatePipelineLayout(shadow)");
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = stages.size();
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &input;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depth;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = outputs.shadowPipelineLayout;
        pipelineInfo.renderPass = outputs.shadowRenderPass;
        require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                          &pipelineInfo, nullptr,
                                          &outputs.shadowPipeline),
                "vkCreateGraphicsPipelines(shadow)");
    } catch (...) {
        if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
        throw;
    }
    vkDestroyShaderModule(m_device, fragment, nullptr);
    vkDestroyShaderModule(m_device, vertex, nullptr);
    guard.active = false;
}

void VulkanPipelineFactory::createSwapchainSet(
    const SwapchainPipelineInputs& inputs,
    SwapchainPipelineOutputs& outputs) const {
    SwapchainSetGuard guard{outputs, m_device, inputs.descriptorPool};
    // ── Scene render pass (MSAA scene color + depth + resolve) ──────────
    std::array<VkAttachmentDescription, 3> attachments{};
    const bool multisampled = inputs.sampleCount != VK_SAMPLE_COUNT_1_BIT;
    attachments[0].format = inputs.sceneFormat;
    attachments[0].samples = inputs.sampleCount;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                           : VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = multisampled
        ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = inputs.depthFormat;
    attachments[1].samples = inputs.sampleCount;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference color{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference resolve{
        2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    attachments[2].format = inputs.sceneFormat;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    subpass.pResolveAttachments = multisampled ? &resolve : nullptr;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = dependencies[0].srcStageMask;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = multisampled ? 3u : 2u;
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = dependencies.size();
    info.pDependencies = dependencies.data();
    require(vkCreateRenderPass(m_device, &info, nullptr, &outputs.renderPass),
            "vkCreateRenderPass");

    // ── Present render pass (swapchain color) ───────────────────────────
    VkAttachmentDescription presentAttachment{};
    presentAttachment.format = inputs.swapchainFormat;
    presentAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    presentAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    presentAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    presentAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    presentAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    presentAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    presentAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    const VkAttachmentReference presentColor{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription presentSubpass{};
    presentSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    presentSubpass.colorAttachmentCount = 1;
    presentSubpass.pColorAttachments = &presentColor;
    std::array<VkSubpassDependency, 2> presentDependencies{};
    presentDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    presentDependencies[0].dstSubpass = 0;
    presentDependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    presentDependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    presentDependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    presentDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    presentDependencies[1].srcSubpass = 0;
    presentDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    presentDependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    presentDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    presentDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    presentInfo.attachmentCount = 1;
    presentInfo.pAttachments = &presentAttachment;
    presentInfo.subpassCount = 1;
    presentInfo.pSubpasses = &presentSubpass;
    presentInfo.dependencyCount = presentDependencies.size();
    presentInfo.pDependencies = presentDependencies.data();
    require(vkCreateRenderPass(m_device, &presentInfo, nullptr,
                               &outputs.presentRenderPass),
            "vkCreateRenderPass(present)");

    if (inputs.bloomLevels > 0) {
        VkAttachmentDescription bloomAttachment{};
        bloomAttachment.format = inputs.sceneFormat;
        bloomAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        bloomAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        bloomAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        bloomAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        bloomAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        bloomAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bloomAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkAttachmentReference bloomColor{
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription bloomSubpass{};
        bloomSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        bloomSubpass.colorAttachmentCount = 1;
        bloomSubpass.pColorAttachments = &bloomColor;
        std::array<VkSubpassDependency,2> bloomDependencies{};
        bloomDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        bloomDependencies[0].dstSubpass = 0;
        bloomDependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        bloomDependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        bloomDependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bloomDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bloomDependencies[1].srcSubpass = 0;
        bloomDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        bloomDependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        bloomDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        bloomDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bloomDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo bloomInfo{};
        bloomInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        bloomInfo.attachmentCount = 1;
        bloomInfo.pAttachments = &bloomAttachment;
        bloomInfo.subpassCount = 1;
        bloomInfo.pSubpasses = &bloomSubpass;
        bloomInfo.dependencyCount = bloomDependencies.size();
        bloomInfo.pDependencies = bloomDependencies.data();
        require(vkCreateRenderPass(m_device, &bloomInfo, nullptr,
                                   &outputs.bloomRenderPass),
                "vkCreateRenderPass(bloom)");
    }

    // ── Chunk and UI pipelines ──────────────────────────────────────────
    VkShaderModule vertex = loadShader(m_shaderRoot / "chunk.vert.spv");
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        fragment = loadShader(m_shaderRoot / "chunk.frag.spv");
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
        }};
        const VkVertexInputBindingDescription binding{
            0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 4> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, px)},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, ao)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, u)},
            {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(MeshVertex, face)},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = attributes.size();
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        // Keep Chunk culling disabled. World-space winding alone is not a
        // sufficient cross-driver proof of Vulkan front-facing behavior
        // after projection and viewport transforms; enabling it previously
        // removed exposed top and side faces on real gameplay cameras.
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = inputs.sampleCount;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        constexpr std::array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = dynamicStates.size();
        dynamic.pDynamicStates = dynamicStates.data();
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        const std::array<VkDescriptorSetLayout, 2> chunkLayouts{
            inputs.chunkSetLayout, inputs.chunkFrameLayout};
        layoutInfo.setLayoutCount = chunkLayouts.size();
        layoutInfo.pSetLayouts = chunkLayouts.data();
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                               VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(FrameUniforms);
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        require(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr,
                                       &outputs.pipelineLayout),
                "vkCreatePipelineLayout");
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = stages.size();
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = outputs.pipelineLayout;
        info.renderPass = outputs.renderPass;
        info.subpass = 0;
        require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info,
                                          nullptr, &outputs.pipeline),
                "vkCreateGraphicsPipelines");
        depth.depthWriteEnable = VK_FALSE;
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info,
                                          nullptr, &outputs.translucentPipeline),
                "vkCreateGraphicsPipelines");
    } catch (...) {
        if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
        throw;
    }
    vkDestroyShaderModule(m_device, fragment, nullptr);
    vkDestroyShaderModule(m_device, vertex, nullptr);

    vertex = loadShader(m_shaderRoot / "ui.vert.spv");
    fragment = VK_NULL_HANDLE;
    try {
        fragment = loadShader(m_shaderRoot / "ui.frag.spv");
        const std::array<VkPipelineShaderStageCreateInfo, 2> uiStages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
        const VkVertexInputBindingDescription uiBinding{
            0, sizeof(UiMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 3> uiAttributes{{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiMeshVertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiMeshVertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(UiMeshVertex, color)}}};
        VkPipelineVertexInputStateCreateInfo uiVertexInput{};
        uiVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        uiVertexInput.vertexBindingDescriptionCount = 1;
        uiVertexInput.pVertexBindingDescriptions = &uiBinding;
        uiVertexInput.vertexAttributeDescriptionCount = uiAttributes.size();
        uiVertexInput.pVertexAttributeDescriptions = uiAttributes.data();
        VkPipelineInputAssemblyStateCreateInfo uiAssembly{};
        uiAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        uiAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo uiViewport{};
        uiViewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        uiViewport.viewportCount = 1;
        uiViewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo uiRaster{};
        uiRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        uiRaster.polygonMode = VK_POLYGON_MODE_FILL;
        uiRaster.cullMode = VK_CULL_MODE_NONE;
        uiRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        uiRaster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo uiMultisample{};
        uiMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        uiMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo uiDepth{};
        uiDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        VkPipelineColorBlendAttachmentState uiBlendAttachment{};
        uiBlendAttachment.blendEnable = VK_TRUE;
        uiBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        uiBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        uiBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        uiBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        uiBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        uiBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        uiBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo uiBlend{};
        uiBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        uiBlend.attachmentCount = 1;
        uiBlend.pAttachments = &uiBlendAttachment;
        constexpr std::array<VkDynamicState, 2> uiDynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo uiDynamic{};
        uiDynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        uiDynamic.dynamicStateCount = uiDynamicStates.size();
        uiDynamic.pDynamicStates = uiDynamicStates.data();
        VkGraphicsPipelineCreateInfo uiInfo{};
        uiInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        uiInfo.stageCount = uiStages.size();
        uiInfo.pStages = uiStages.data();
        uiInfo.pVertexInputState = &uiVertexInput;
        uiInfo.pInputAssemblyState = &uiAssembly;
        uiInfo.pViewportState = &uiViewport;
        uiInfo.pRasterizationState = &uiRaster;
        uiInfo.pMultisampleState = &uiMultisample;
        uiInfo.pDepthStencilState = &uiDepth;
        uiInfo.pColorBlendState = &uiBlend;
        uiInfo.pDynamicState = &uiDynamic;
        uiInfo.layout = outputs.pipelineLayout;
        uiInfo.renderPass = outputs.presentRenderPass;
        require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &uiInfo,
                                          nullptr, &outputs.uiPipeline),
                "vkCreateGraphicsPipelines");
    } catch (...) {
        if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
        throw;
    }
    vkDestroyShaderModule(m_device, fragment, nullptr);
    vkDestroyShaderModule(m_device, vertex, nullptr);

    createSecondaryPipelines(inputs, outputs);

    if (inputs.bloomLevels > 0) {
        vertex = loadShader(m_shaderRoot / "post.vert.spv");
        fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(m_shaderRoot / "bloom.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo,2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState,2> dynamicStates{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = dynamicStates.size();
            dynamic.pDynamicStates = dynamicStates.data();
            VkPushConstantRange push{};
            push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            push.size = sizeof(BloomConstants);
            VkPipelineLayoutCreateInfo layout{};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount = 1;
            layout.pSetLayouts = &inputs.bloomLayout;
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges = &push;
            require(vkCreatePipelineLayout(m_device, &layout, nullptr,
                                           &outputs.bloomPipelineLayout),
                    "vkCreatePipelineLayout(bloom)");
            VkGraphicsPipelineCreateInfo pipeline{};
            pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline.stageCount = stages.size();
            pipeline.pStages = stages.data();
            pipeline.pVertexInputState = &vertexInput;
            pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewport;
            pipeline.pRasterizationState = &raster;
            pipeline.pMultisampleState = &multisample;
            pipeline.pDepthStencilState = &depth;
            pipeline.pColorBlendState = &blend;
            pipeline.pDynamicState = &dynamic;
            pipeline.layout = outputs.bloomPipelineLayout;
            pipeline.renderPass = outputs.bloomRenderPass;
            require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                               &pipeline, nullptr,
                                               &outputs.bloomPipeline),
                    "vkCreateGraphicsPipelines(bloom)");
        } catch (...) {
            if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
            vkDestroyShaderModule(m_device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
    }

    // ── Post pipeline and scene descriptors ─────────────────────────────
    vertex = loadShader(m_shaderRoot / "post.vert.spv");
    fragment = VK_NULL_HANDLE;
    try {
        fragment = loadShader(m_shaderRoot / "post.frag.spv");
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        constexpr std::array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = dynamicStates.size();
        dynamic.pDynamicStates = dynamicStates.data();
        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.size = sizeof(PostConstants);
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &inputs.postLayout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        require(vkCreatePipelineLayout(m_device, &layout, nullptr,
                                       &outputs.postPipelineLayout),
                "vkCreatePipelineLayout(post)");
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = stages.size();
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depth;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = outputs.postPipelineLayout;
        pipelineInfo.renderPass = outputs.presentRenderPass;
        require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                          &pipelineInfo, nullptr,
                                          &outputs.postPipeline),
                "vkCreateGraphicsPipelines(post)");
    } catch (...) {
        if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
        throw;
    }
    vkDestroyShaderModule(m_device, fragment, nullptr);
    vkDestroyShaderModule(m_device, vertex, nullptr);

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    require(vkCreateSampler(m_device, &sampler, nullptr, &outputs.postSampler),
            "vkCreateSampler(post)");
    const std::vector<VkImageView>& sceneImageViews =
        inputs.sceneImageViews ? *inputs.sceneImageViews : g_emptyImageViews;
    outputs.postDescriptorSets.resize(sceneImageViews.size());
    std::vector<VkDescriptorSetLayout> layouts(
        outputs.postDescriptorSets.size(), inputs.postLayout);
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = inputs.descriptorPool;
    allocate.descriptorSetCount = outputs.postDescriptorSets.size();
    allocate.pSetLayouts = layouts.data();
    require(vkAllocateDescriptorSets(m_device, &allocate,
                                     outputs.postDescriptorSets.data()),
            "vkAllocateDescriptorSets(post)");
    for (size_t i = 0; i < outputs.postDescriptorSets.size(); ++i) {
        std::array<VkDescriptorImageInfo,5> imageInfos{};
        std::array<VkWriteDescriptorSet,5> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            VkImageView view = sceneImageViews[i];
            if (binding > 0 && inputs.bloomImageViews &&
                static_cast<int>(binding) <= inputs.bloomLevels)
                view = (*inputs.bloomImageViews)[binding - 1][i];
            imageInfos[binding] = {outputs.postSampler, view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = outputs.postDescriptorSets[i];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[binding].pImageInfo = &imageInfos[binding];
        }
        vkUpdateDescriptorSets(m_device, writes.size(), writes.data(), 0, nullptr);
    }
    for (int level = 0; level < inputs.bloomLevels; ++level) {
        auto& sets = outputs.bloomDescriptorSets[static_cast<size_t>(level)];
        sets.resize(sceneImageViews.size());
        std::vector<VkDescriptorSetLayout> bloomLayouts(
            sets.size(), inputs.bloomLayout);
        allocate.descriptorSetCount = sets.size();
        allocate.pSetLayouts = bloomLayouts.data();
        require(vkAllocateDescriptorSets(m_device, &allocate, sets.data()),
                "vkAllocateDescriptorSets(bloom)");
        for (size_t i = 0; i < sets.size(); ++i) {
            const VkImageView source = level == 0
                ? sceneImageViews[i]
                : (*inputs.bloomImageViews)[static_cast<size_t>(level - 1)][i];
            const VkDescriptorImageInfo imageInfo{
                outputs.postSampler, source,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sets[i];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        }
    }
    guard.active = false;
}

void VulkanPipelineFactory::createSecondaryPipelines(
    const SwapchainPipelineInputs& inputs,
    SwapchainPipelineOutputs& outputs) const {
    // ── Particle pipeline ───────────────────────────────────────────────
    {
        VkShaderModule vertex = loadShader(m_shaderRoot / "weather.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(m_shaderRoot / "weather.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            const VkVertexInputBindingDescription binding{
                0, sizeof(ParticleRenderData), VK_VERTEX_INPUT_RATE_INSTANCE};
            const std::array<VkVertexInputAttributeDescription, 2> attributes{{
                {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(ParticleRenderData, position)},
                {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(ParticleRenderData, phase)}}};
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = attributes.size();
            vertexInput.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType =
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = inputs.sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_FALSE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState, 2> dynamicStates{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = dynamicStates.size();
            dynamic.pDynamicStates = dynamicStates.data();
            VkPushConstantRange range{};
            range.stageFlags =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            range.size = sizeof(ParticleUniforms);
            VkPipelineLayoutCreateInfo layout{};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount = 1;
            layout.pSetLayouts = &inputs.chunkSetLayout;
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges = &range;
            require(vkCreatePipelineLayout(m_device, &layout, nullptr,
                                           &outputs.particlePipelineLayout),
                    "vkCreatePipelineLayout");
            VkGraphicsPipelineCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = stages.size();
            info.pStages = stages.data();
            info.pVertexInputState = &vertexInput;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pDepthStencilState = &depth;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = outputs.particlePipelineLayout;
            info.renderPass = outputs.renderPass;
            require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                              &info, nullptr,
                                              &outputs.particlePipeline),
                    "vkCreateGraphicsPipelines");
        } catch (...) {
            if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
            vkDestroyShaderModule(m_device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
    }

    // ── Sky and cloud pipelines ─────────────────────────────────────────
    {
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = inputs.sampleCount;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        constexpr std::array<VkDynamicState, 2> dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = dynamicStates.size();
        dynamic.pDynamicStates = dynamicStates.data();

        VkPipelineLayoutCreateInfo skyLayoutInfo{};
        skyLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        skyLayoutInfo.setLayoutCount = 1;
        skyLayoutInfo.pSetLayouts = &inputs.skyLayout;
        require(vkCreatePipelineLayout(m_device, &skyLayoutInfo, nullptr,
                                       &outputs.skyPipelineLayout),
                "vkCreatePipelineLayout");
        VkPipelineVertexInputStateCreateInfo emptyVertexInput{};
        emptyVertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        const auto createGraphics = [&](const char* vertexName,
                                        const char* fragmentName,
                                        const VkPipelineVertexInputStateCreateInfo& input,
                                        VkPipelineLayout layout,
                                        VkPipeline* output) {
            VkShaderModule vertex = loadShader(m_shaderRoot / vertexName);
            VkShaderModule fragment = VK_NULL_HANDLE;
            try {
                fragment = loadShader(m_shaderRoot / fragmentName);
                const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
                VkGraphicsPipelineCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                info.stageCount = stages.size();
                info.pStages = stages.data();
                info.pVertexInputState = &input;
                info.pInputAssemblyState = &assembly;
                info.pViewportState = &viewport;
                info.pRasterizationState = &raster;
                info.pMultisampleState = &multisample;
                info.pDepthStencilState = &depth;
                info.pColorBlendState = &blend;
                info.pDynamicState = &dynamic;
                info.layout = layout;
                info.renderPass = outputs.renderPass;
                require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                                  &info, nullptr, output),
                        "vkCreateGraphicsPipelines");
            } catch (...) {
                if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
                vkDestroyShaderModule(m_device, vertex, nullptr);
                throw;
            }
            vkDestroyShaderModule(m_device, fragment, nullptr);
            vkDestroyShaderModule(m_device, vertex, nullptr);
        };
        createGraphics("sky.vert.spv", "sky.frag.spv", emptyVertexInput,
                       outputs.skyPipelineLayout, &outputs.skyPipeline);

        const VkVertexInputBindingDescription cloudBinding{
            0, sizeof(CloudInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
        const std::array<VkVertexInputAttributeDescription, 3> cloudAttributes{{
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CloudInstance, x)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(CloudInstance, depth)},
            {2, 0, VK_FORMAT_R32_UINT,
             offsetof(CloudInstance, visibleFaces)}}};
        VkPipelineVertexInputStateCreateInfo cloudVertexInput{};
        cloudVertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        cloudVertexInput.vertexBindingDescriptionCount = 1;
        cloudVertexInput.pVertexBindingDescriptions = &cloudBinding;
        cloudVertexInput.vertexAttributeDescriptionCount = cloudAttributes.size();
        cloudVertexInput.pVertexAttributeDescriptions = cloudAttributes.data();
        VkPushConstantRange cloudRange{};
        cloudRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT;
        cloudRange.size = sizeof(CloudUniforms);
        VkPipelineLayoutCreateInfo cloudLayoutInfo{};
        cloudLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        cloudLayoutInfo.pushConstantRangeCount = 1;
        cloudLayoutInfo.pPushConstantRanges = &cloudRange;
        require(vkCreatePipelineLayout(m_device, &cloudLayoutInfo, nullptr,
                                       &outputs.cloudPipelineLayout),
                "vkCreatePipelineLayout");
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        // The shared compatibility cube uses legacy per-face winding. Clouds
        // are opaque, so double-sided rasterization preserves the established
        // silhouette without making winding part of the shared data contract.
        raster.cullMode = VK_CULL_MODE_NONE;
        createGraphics("cloud.vert.spv", "cloud.frag.spv", cloudVertexInput,
                       outputs.cloudPipelineLayout, &outputs.cloudPipeline);
    }

    // ── Wireframe pipeline ──────────────────────────────────────────────
    {
        VkShaderModule vertex = loadShader(m_shaderRoot / "wireframe.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(m_shaderRoot / "wireframe.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType =
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = inputs.sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState, 2> dynamicStates{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = dynamicStates.size();
            dynamic.pDynamicStates = dynamicStates.data();
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            range.size = sizeof(WireUniforms);
            VkPipelineLayoutCreateInfo layout{};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges = &range;
            require(vkCreatePipelineLayout(m_device, &layout, nullptr,
                                           &outputs.wirePipelineLayout),
                    "vkCreatePipelineLayout");
            VkGraphicsPipelineCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = stages.size();
            info.pStages = stages.data();
            info.pVertexInputState = &vertexInput;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pDepthStencilState = &depth;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = outputs.wirePipelineLayout;
            info.renderPass = outputs.renderPass;
            require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                              &info, nullptr,
                                              &outputs.wirePipeline),
                    "vkCreateGraphicsPipelines");
        } catch (...) {
            if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
            vkDestroyShaderModule(m_device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
    }

    // ── Model pipelines ─────────────────────────────────────────────────
    {
        VkShaderModule vertex = loadShader(m_shaderRoot / "model.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(m_shaderRoot / "model.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            const VkVertexInputBindingDescription binding{
                0, sizeof(model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const std::array<VkVertexInputAttributeDescription, 5> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                 offsetof(model::Vertex, position)},
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                 offsetof(model::Vertex, normal)},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(model::Vertex, uv)},
                {3, 0, VK_FORMAT_R32G32B32A32_UINT,
                 offsetof(model::Vertex, joints)},
                {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(model::Vertex, weights)}}};
            VkPipelineVertexInputStateCreateInfo input{};
            input.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            input.vertexBindingDescriptionCount = 1;
            input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = attributes.size();
            input.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType =
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = inputs.sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState, 2> states{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = states.size();
            dynamic.pDynamicStates = states.data();
            const std::array<VkDescriptorSetLayout, 2> layouts{
                inputs.chunkSetLayout, inputs.modelLayout};
            VkPipelineLayoutCreateInfo layout{};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount = layouts.size();
            layout.pSetLayouts = layouts.data();
            require(vkCreatePipelineLayout(m_device, &layout, nullptr,
                                           &outputs.modelPipelineLayout),
                    "vkCreatePipelineLayout");
            VkGraphicsPipelineCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = stages.size();
            info.pStages = stages.data();
            info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pDepthStencilState = &depth;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = outputs.modelPipelineLayout;
            info.renderPass = outputs.renderPass;
            const auto create = [&](VkPipeline& output) {
                require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                                  &info, nullptr, &output),
                        "vkCreateGraphicsPipelines");
            };
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(outputs.modelOpaquePipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(outputs.modelOpaqueDoubleSidedPipeline);
            depth.depthWriteEnable = VK_FALSE;
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(outputs.modelBlendPipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(outputs.modelBlendDoubleSidedPipeline);
        } catch (...) {
            if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
            vkDestroyShaderModule(m_device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
    }

    // ── Basic pipelines ─────────────────────────────────────────────────
    {
        VkShaderModule vertex = loadShader(m_shaderRoot / "basic_cube.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(m_shaderRoot / "basic_cube.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            const VkVertexInputBindingDescription binding{
                0, sizeof(BasicMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const std::array<VkVertexInputAttributeDescription, 2> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                 offsetof(BasicMeshVertex, position)},
                {1, 0, VK_FORMAT_R32G32_SFLOAT,
                 offsetof(BasicMeshVertex, uv)}}};
            VkPipelineVertexInputStateCreateInfo input{};
            input.sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            input.vertexBindingDescriptionCount = 1;
            input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = attributes.size();
            input.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType =
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = inputs.sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState, 2> states{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType =
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = states.size();
            dynamic.pDynamicStates = states.data();
            VkGraphicsPipelineCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = stages.size();
            info.pStages = stages.data();
            info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pDepthStencilState = &depth;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = outputs.pipelineLayout;
            info.renderPass = outputs.renderPass;
            const auto create = [&](VkPipeline& output) {
                require(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                                  &info, nullptr, &output),
                        "vkCreateGraphicsPipelines");
            };
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(outputs.basicPipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(outputs.basicNoCullPipeline);
            depth.depthTestEnable = VK_FALSE;
            depth.depthWriteEnable = VK_FALSE;
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(outputs.basicNoDepthPipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(outputs.basicNoDepthNoCullPipeline);
        } catch (...) {
            if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
            vkDestroyShaderModule(m_device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(m_device, fragment, nullptr);
        vkDestroyShaderModule(m_device, vertex, nullptr);
    }
}

}  // namespace vkp
