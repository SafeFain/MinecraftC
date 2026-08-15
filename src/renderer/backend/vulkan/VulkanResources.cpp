#include "renderer/backend/vulkan/VulkanResources.h"

#include "debug/Log.h"
#include "renderer/backend/vulkan/VulkanHelpers.h"
#include "renderer/backend/vulkan/VulkanPipelineFactory.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace vkp {

using vkhelp::require;
using vkhelp::chooseSurfaceFormat;
using vkhelp::chooseExtent;
using vkhelp::chooseSurfaceTransform;
using vkhelp::chooseCompositeAlpha;
using vkhelp::choosePresentMode;
using vkhelp::isSrgbFormat;
using vkhelp::findDepthFormat;

VulkanDeviceContext::~VulkanDeviceContext() {
    if (commandPool && device)
        vkDestroyCommandPool(device, commandPool, nullptr);
    if (allocator) vmaDestroyAllocator(allocator);
    if (device) vkDestroyDevice(device, nullptr);
    if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
}

VulkanDescriptorResources::~VulkanDescriptorResources() {
    if (!m_device) return;
    if (descriptorPool) vkDestroyDescriptorPool(m_device, descriptorPool, nullptr);
    if (postDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_device, postDescriptorSetLayout, nullptr);
    if (modelUniformDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_device, modelUniformDescriptorSetLayout, nullptr);
    if (chunkDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_device, chunkDescriptorSetLayout, nullptr);
    if (skyDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_device, skyDescriptorSetLayout, nullptr);
    if (descriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_device, descriptorSetLayout, nullptr);
}

VulkanFrameSync VulkanFrameSync::create(VkDevice device) {
    VulkanFrameSync result;
    result.m_device = device;
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        require(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                  &result.imageAvailable[i]),
                "vkCreateSemaphore");
        require(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                  &result.renderFinished[i]),
                "vkCreateSemaphore");
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        require(vkCreateFence(device, &fenceInfo, nullptr, &result.fences[i]),
                "vkCreateFence");
    }
    return result;
}

void VulkanFrameSync::destroy() {
    if (!m_device) return;
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (fences[i]) vkDestroyFence(m_device, fences[i], nullptr);
        if (renderFinished[i])
            vkDestroySemaphore(m_device, renderFinished[i], nullptr);
        if (imageAvailable[i])
            vkDestroySemaphore(m_device, imageAvailable[i], nullptr);
    }
    fences.fill(VK_NULL_HANDLE);
    renderFinished.fill(VK_NULL_HANDLE);
    imageAvailable.fill(VK_NULL_HANDLE);
}

VulkanFrameSync::~VulkanFrameSync() { destroy(); }

VulkanFrameSync::VulkanFrameSync(VulkanFrameSync&& other) noexcept
    : fences(other.fences),
      renderFinished(other.renderFinished),
      imageAvailable(other.imageAvailable),
      m_device(other.m_device) {
    other.fences.fill(VK_NULL_HANDLE);
    other.renderFinished.fill(VK_NULL_HANDLE);
    other.imageAvailable.fill(VK_NULL_HANDLE);
    other.m_device = VK_NULL_HANDLE;
}

VulkanFrameSync& VulkanFrameSync::operator=(VulkanFrameSync&& other) noexcept {
    if (this == &other) return *this;
    destroy();
    fences = other.fences;
    renderFinished = other.renderFinished;
    imageAvailable = other.imageAvailable;
    m_device = other.m_device;
    other.fences.fill(VK_NULL_HANDLE);
    other.renderFinished.fill(VK_NULL_HANDLE);
    other.imageAvailable.fill(VK_NULL_HANDLE);
    other.m_device = VK_NULL_HANDLE;
    return *this;
}

void VulkanShadowResources::attach(VkDevice device, VmaAllocator allocator) {
    m_device = device;
    m_allocator = allocator;
}

void VulkanShadowResources::destroyImage() {
    if (!m_device) return;
    if (shadowFramebuffer) vkDestroyFramebuffer(m_device, shadowFramebuffer, nullptr);
    shadowFramebuffer = VK_NULL_HANDLE;
    if (shadowSampler) vkDestroySampler(m_device, shadowSampler, nullptr);
    shadowSampler = VK_NULL_HANDLE;
    if (shadowImageView) vkDestroyImageView(m_device, shadowImageView, nullptr);
    shadowImageView = VK_NULL_HANDLE;
    if (shadowImage && m_allocator)
        vmaDestroyImage(m_allocator, shadowImage, shadowAllocation);
    shadowImage = VK_NULL_HANDLE;
    shadowAllocation = VK_NULL_HANDLE;
}

void VulkanShadowResources::destroy() {
    if (!m_device) return;
    if (shadowPipeline) vkDestroyPipeline(m_device, shadowPipeline, nullptr);
    shadowPipeline = VK_NULL_HANDLE;
    if (shadowPipelineLayout)
        vkDestroyPipelineLayout(m_device, shadowPipelineLayout, nullptr);
    shadowPipelineLayout = VK_NULL_HANDLE;
    if (shadowRenderPass) vkDestroyRenderPass(m_device, shadowRenderPass, nullptr);
    shadowRenderPass = VK_NULL_HANDLE;
    destroyImage();
}

VulkanSwapchainBundle VulkanSwapchainBundle::create(const CreateParams& params) {
    VulkanSwapchainBundle result;
    result.m_device = params.device;
    result.m_allocator = params.allocator;
    result.m_commandPool = params.commandPool;
    result.m_descriptorPool = params.descriptorPool;

    VkSurfaceCapabilitiesKHR capabilities{};
    require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(params.physicalDevice,
                params.surface, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    uint32_t formatCount = 0;
    uint32_t modeCount = 0;
    require(vkGetPhysicalDeviceSurfaceFormatsKHR(params.physicalDevice,
                params.surface, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    require(vkGetPhysicalDeviceSurfaceFormatsKHR(params.physicalDevice,
                params.surface, &formatCount, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    require(vkGetPhysicalDeviceSurfacePresentModesKHR(params.physicalDevice,
                params.surface, &modeCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> modes(modeCount);
    require(vkGetPhysicalDeviceSurfacePresentModesKHR(params.physicalDevice,
                params.surface, &modeCount, modes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (formats.empty() || modes.empty())
        throw std::runtime_error("Vulkan surface has no swapchain configuration");
    const VkSurfaceFormatKHR format = chooseSurfaceFormat(formats);
    const VkExtent2D extent = chooseExtent(
        capabilities, params.windowWidth, params.windowHeight);
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0)
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = params.surface;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const uint32_t queueFamilies[]{params.graphicsFamily, params.presentFamily};
    if (params.graphicsFamily != params.presentFamily) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queueFamilies;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = chooseSurfaceTransform(capabilities);
    info.compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
    info.presentMode = choosePresentMode(modes, params.synchronizePresentation);
    if (!params.synchronizePresentation &&
        info.presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR)
        LOG_WARN("Vulkan benchmark requested unsynchronized presentation, but "
                 "VK_PRESENT_MODE_IMMEDIATE_KHR is unavailable");
    info.clipped = VK_TRUE;
    require(vkCreateSwapchainKHR(params.device, &info, nullptr, &result.handle),
            "vkCreateSwapchainKHR");
    result.swapchainFormat = format.format;
    result.framebufferSrgb = isSrgbFormat(result.swapchainFormat);
    result.swapchainExtent = extent;
    require(vkGetSwapchainImagesKHR(params.device, result.handle, &imageCount,
                                    nullptr),
            "vkGetSwapchainImagesKHR");
    result.images.resize(imageCount);
    require(vkGetSwapchainImagesKHR(params.device, result.handle, &imageCount,
                                    result.images.data()),
            "vkGetSwapchainImagesKHR");

    // ── Image views ─────────────────────────────────────────────────────
    result.imageViews.resize(result.images.size());
    for (size_t i = 0; i < result.images.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = result.swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        require(vkCreateImageView(params.device, &viewInfo, nullptr,
                                  &result.imageViews[i]),
                "vkCreateImageView");
    }

    // ── Scene format and sample count ───────────────────────────────────
    const std::array<VkFormat, 3> sceneCandidates{
        VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT,
        result.swapchainFormat};
    for (VkFormat candidate : sceneCandidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(params.physicalDevice, candidate,
                                            &properties);
        const VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) != required) continue;
        result.sceneFormat = candidate;
        break;
    }
    VkSampleCountFlagBits desired = params.maxSampleCount >=
            params.requestedSampleCount
        ? params.requestedSampleCount : params.maxSampleCount;
    while (desired > VK_SAMPLE_COUNT_1_BIT) {
        VkImageFormatProperties imageProperties{};
        const VkResult supported = vkGetPhysicalDeviceImageFormatProperties(
            params.physicalDevice, result.sceneFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            0, &imageProperties);
        if (supported == VK_SUCCESS &&
            (imageProperties.sampleCounts & desired) != 0)
            break;
        desired = desired == VK_SAMPLE_COUNT_4_BIT
            ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
    }
    result.sampleCount = desired;
    LOG_INFO("Vulkan scene target: format "
             << static_cast<int>(result.sceneFormat) << ", "
             << static_cast<uint32_t>(result.sampleCount) << "x MSAA");

    // ── Scene, color (MSAA), and depth resources ────────────────────────
    result.sceneImages.assign(result.images.size(), VK_NULL_HANDLE);
    result.sceneAllocations.assign(result.images.size(), VK_NULL_HANDLE);
    result.sceneImageViews.assign(result.images.size(), VK_NULL_HANDLE);
    VkImageCreateInfo sceneImageInfo{};
    sceneImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    sceneImageInfo.imageType = VK_IMAGE_TYPE_2D;
    sceneImageInfo.extent = {result.swapchainExtent.width,
                             result.swapchainExtent.height, 1};
    sceneImageInfo.mipLevels = 1;
    sceneImageInfo.arrayLayers = 1;
    sceneImageInfo.format = result.sceneFormat;
    sceneImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    sceneImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sceneImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT;
    sceneImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    sceneImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    for (size_t i = 0; i < result.images.size(); ++i) {
        require(vmaCreateImage(params.allocator, &sceneImageInfo,
                               &allocationInfo, &result.sceneImages[i],
                               &result.sceneAllocations[i], nullptr),
                "vmaCreateImage(scene)");
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = result.sceneImages[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = result.sceneFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        require(vkCreateImageView(params.device, &view, nullptr,
                                  &result.sceneImageViews[i]),
                "vkCreateImageView(scene)");
    }
    result.depthFormat = findDepthFormat(params.physicalDevice);
    result.depthImages.assign(result.images.size(), VK_NULL_HANDLE);
    result.depthAllocations.assign(result.images.size(), VK_NULL_HANDLE);
    result.depthImageViews.assign(result.images.size(), VK_NULL_HANDLE);
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent = {result.swapchainExtent.width,
                             result.swapchainExtent.height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = result.depthFormat;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = result.sampleCount;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    for (size_t i = 0; i < result.images.size(); ++i) {
        require(vmaCreateImage(params.allocator, &depthImageInfo,
                               &allocationInfo, &result.depthImages[i],
                               &result.depthAllocations[i], nullptr),
                "vmaCreateImage");
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = result.depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        require(vkCreateImageView(params.device, &viewInfo, nullptr,
                                  &result.depthImageViews[i]),
                "vkCreateImageView");
    }
    if (result.sampleCount != VK_SAMPLE_COUNT_1_BIT) {
        result.colorImages.assign(result.images.size(), VK_NULL_HANDLE);
        result.colorAllocations.assign(result.images.size(), VK_NULL_HANDLE);
        result.colorImageViews.assign(result.images.size(), VK_NULL_HANDLE);
        VkImageCreateInfo colorImageInfo{};
        colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
        colorImageInfo.extent = {result.swapchainExtent.width,
                                 result.swapchainExtent.height, 1};
        colorImageInfo.mipLevels = 1;
        colorImageInfo.arrayLayers = 1;
        colorImageInfo.format = result.sceneFormat;
        colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorImageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        colorImageInfo.samples = result.sampleCount;
        colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        for (size_t i = 0; i < result.images.size(); ++i) {
            require(vmaCreateImage(params.allocator, &colorImageInfo,
                                   &allocationInfo, &result.colorImages[i],
                                   &result.colorAllocations[i], nullptr),
                    "vmaCreateImage");
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = result.colorImages[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = result.sceneFormat;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            require(vkCreateImageView(params.device, &view, nullptr,
                                      &result.colorImageViews[i]),
                    "vkCreateImageView");
        }
    }

    // ── Render passes and pipelines ─────────────────────────────────────
    const VulkanPipelineFactory pipelineFactory(params.device, params.shaderRoot);
    SwapchainPipelineInputs pipelineInputs;
    pipelineInputs.chunkSetLayout = params.blockLayout;
    pipelineInputs.chunkFrameLayout = params.chunkFrameLayout;
    pipelineInputs.skyLayout = params.skyLayout;
    pipelineInputs.modelLayout = params.modelLayout;
    pipelineInputs.postLayout = params.postLayout;
    pipelineInputs.descriptorPool = params.descriptorPool;
    pipelineInputs.sceneFormat = result.sceneFormat;
    pipelineInputs.depthFormat = result.depthFormat;
    pipelineInputs.swapchainFormat = result.swapchainFormat;
    pipelineInputs.sampleCount = result.sampleCount;
    pipelineInputs.sceneImageViews = &result.sceneImageViews;
    SwapchainPipelineOutputs pipelineOutputs;
    pipelineFactory.createSwapchainSet(pipelineInputs, pipelineOutputs);
    result.renderPass = pipelineOutputs.renderPass;
    result.presentRenderPass = pipelineOutputs.presentRenderPass;
    result.pipelineLayout = pipelineOutputs.pipelineLayout;
    result.pipeline = pipelineOutputs.pipeline;
    result.translucentPipeline = pipelineOutputs.translucentPipeline;
    result.uiPipeline = pipelineOutputs.uiPipeline;
    result.postPipeline = pipelineOutputs.postPipeline;
    result.postPipelineLayout = pipelineOutputs.postPipelineLayout;
    result.postSampler = pipelineOutputs.postSampler;
    result.postDescriptorSets = std::move(pipelineOutputs.postDescriptorSets);
    result.basicPipeline = pipelineOutputs.basicPipeline;
    result.basicNoCullPipeline = pipelineOutputs.basicNoCullPipeline;
    result.basicNoDepthPipeline = pipelineOutputs.basicNoDepthPipeline;
    result.basicNoDepthNoCullPipeline =
        pipelineOutputs.basicNoDepthNoCullPipeline;
    result.modelPipelineLayout = pipelineOutputs.modelPipelineLayout;
    result.modelOpaquePipeline = pipelineOutputs.modelOpaquePipeline;
    result.modelOpaqueDoubleSidedPipeline =
        pipelineOutputs.modelOpaqueDoubleSidedPipeline;
    result.modelBlendPipeline = pipelineOutputs.modelBlendPipeline;
    result.modelBlendDoubleSidedPipeline =
        pipelineOutputs.modelBlendDoubleSidedPipeline;
    result.wirePipelineLayout = pipelineOutputs.wirePipelineLayout;
    result.wirePipeline = pipelineOutputs.wirePipeline;
    result.particlePipelineLayout = pipelineOutputs.particlePipelineLayout;
    result.particlePipeline = pipelineOutputs.particlePipeline;
    result.skyPipelineLayout = pipelineOutputs.skyPipelineLayout;
    result.cloudPipelineLayout = pipelineOutputs.cloudPipelineLayout;
    result.skyPipeline = pipelineOutputs.skyPipeline;
    result.cloudPipeline = pipelineOutputs.cloudPipeline;

    // ── Framebuffers and command buffers ────────────────────────────────
    result.framebuffers.resize(result.imageViews.size());
    result.presentFramebuffers.resize(result.imageViews.size());
    for (size_t i = 0; i < result.imageViews.size(); ++i) {
        const bool multisampled =
            result.sampleCount != VK_SAMPLE_COUNT_1_BIT;
        const std::array<VkImageView, 3> attachments{
            multisampled ? result.colorImageViews[i] : result.sceneImageViews[i],
            result.depthImageViews[i], result.sceneImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = result.renderPass;
        framebufferInfo.attachmentCount = multisampled ? 3u : 2u;
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = result.swapchainExtent.width;
        framebufferInfo.height = result.swapchainExtent.height;
        framebufferInfo.layers = 1;
        require(vkCreateFramebuffer(params.device, &framebufferInfo, nullptr,
                                    &result.framebuffers[i]),
                "vkCreateFramebuffer");

        VkFramebufferCreateInfo presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        presentInfo.renderPass = result.presentRenderPass;
        presentInfo.attachmentCount = 1;
        presentInfo.pAttachments = &result.imageViews[i];
        presentInfo.width = result.swapchainExtent.width;
        presentInfo.height = result.swapchainExtent.height;
        presentInfo.layers = 1;
        require(vkCreateFramebuffer(params.device, &presentInfo, nullptr,
                                    &result.presentFramebuffers[i]),
                "vkCreateFramebuffer(present)");
    }
    result.commandBuffers.resize(result.images.size());
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = params.commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = result.commandBuffers.size();
    require(vkAllocateCommandBuffers(params.device, &allocateInfo,
                                     result.commandBuffers.data()),
            "vkAllocateCommandBuffers");
    result.imagesInFlight.assign(result.images.size(), VK_NULL_HANDLE);

    LOG_INFO("Vulkan swapchain: " << extent.width << "x" << extent.height
             << ", " << imageCount << " images, format "
             << static_cast<int>(result.swapchainFormat) << ", "
             << (result.framebufferSrgb ? "hardware sRGB"
                                        : "shader gamma fallback")
             << ", surface transform "
             << static_cast<uint32_t>(info.preTransform));
    return result;
}

void VulkanSwapchainBundle::destroy() {
    if (!m_device) return;
    if (!commandBuffers.empty() && m_commandPool)
        vkFreeCommandBuffers(m_device, m_commandPool, commandBuffers.size(),
                             commandBuffers.data());
    commandBuffers.clear();
    if (!postDescriptorSets.empty() && m_descriptorPool) {
        require(vkFreeDescriptorSets(
                    m_device, m_descriptorPool,
                    static_cast<uint32_t>(postDescriptorSets.size()),
                    postDescriptorSets.data()),
                "vkFreeDescriptorSets(post)");
    }
    postDescriptorSets.clear();
    for (VkFramebuffer framebuffer : presentFramebuffers)
        if (framebuffer) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    presentFramebuffers.clear();
    for (VkFramebuffer framebuffer : framebuffers)
        if (framebuffer) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    framebuffers.clear();
    if (postPipeline) vkDestroyPipeline(m_device, postPipeline, nullptr);
    postPipeline = VK_NULL_HANDLE;
    if (postPipelineLayout)
        vkDestroyPipelineLayout(m_device, postPipelineLayout, nullptr);
    postPipelineLayout = VK_NULL_HANDLE;
    if (postSampler) vkDestroySampler(m_device, postSampler, nullptr);
    postSampler = VK_NULL_HANDLE;
    if (pipeline) vkDestroyPipeline(m_device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
    if (translucentPipeline)
        vkDestroyPipeline(m_device, translucentPipeline, nullptr);
    translucentPipeline = VK_NULL_HANDLE;
    for (VkPipeline* basic : {&basicPipeline, &basicNoCullPipeline,
            &basicNoDepthPipeline, &basicNoDepthNoCullPipeline}) {
        if (*basic) vkDestroyPipeline(m_device, *basic, nullptr);
        *basic = VK_NULL_HANDLE;
    }
    if (uiPipeline) vkDestroyPipeline(m_device, uiPipeline, nullptr);
    uiPipeline = VK_NULL_HANDLE;
    if (skyPipeline) vkDestroyPipeline(m_device, skyPipeline, nullptr);
    skyPipeline = VK_NULL_HANDLE;
    if (cloudPipeline) vkDestroyPipeline(m_device, cloudPipeline, nullptr);
    cloudPipeline = VK_NULL_HANDLE;
    if (wirePipeline) vkDestroyPipeline(m_device, wirePipeline, nullptr);
    wirePipeline = VK_NULL_HANDLE;
    for (VkPipeline* modelPipeline : {&modelOpaquePipeline,
            &modelOpaqueDoubleSidedPipeline, &modelBlendPipeline,
            &modelBlendDoubleSidedPipeline}) {
        if (*modelPipeline) vkDestroyPipeline(m_device, *modelPipeline, nullptr);
        *modelPipeline = VK_NULL_HANDLE;
    }
    if (particlePipeline) vkDestroyPipeline(m_device, particlePipeline, nullptr);
    particlePipeline = VK_NULL_HANDLE;
    if (particlePipelineLayout)
        vkDestroyPipelineLayout(m_device, particlePipelineLayout, nullptr);
    particlePipelineLayout = VK_NULL_HANDLE;
    if (skyPipelineLayout)
        vkDestroyPipelineLayout(m_device, skyPipelineLayout, nullptr);
    skyPipelineLayout = VK_NULL_HANDLE;
    if (cloudPipelineLayout)
        vkDestroyPipelineLayout(m_device, cloudPipelineLayout, nullptr);
    cloudPipelineLayout = VK_NULL_HANDLE;
    if (wirePipelineLayout)
        vkDestroyPipelineLayout(m_device, wirePipelineLayout, nullptr);
    wirePipelineLayout = VK_NULL_HANDLE;
    if (modelPipelineLayout)
        vkDestroyPipelineLayout(m_device, modelPipelineLayout, nullptr);
    modelPipelineLayout = VK_NULL_HANDLE;
    if (pipelineLayout) vkDestroyPipelineLayout(m_device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
    if (presentRenderPass)
        vkDestroyRenderPass(m_device, presentRenderPass, nullptr);
    presentRenderPass = VK_NULL_HANDLE;
    if (renderPass) vkDestroyRenderPass(m_device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
    for (VkImageView view : depthImageViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    depthImageViews.clear();
    if (m_allocator) {
        for (size_t i = 0; i < depthImages.size(); ++i) {
            if (depthImages[i])
                vmaDestroyImage(m_allocator, depthImages[i], depthAllocations[i]);
        }
    }
    depthImages.clear();
    depthAllocations.clear();
    for (VkImageView view : colorImageViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    colorImageViews.clear();
    if (m_allocator) {
        for (size_t i = 0; i < colorImages.size(); ++i) {
            if (colorImages[i])
                vmaDestroyImage(m_allocator, colorImages[i], colorAllocations[i]);
        }
    }
    colorImages.clear();
    colorAllocations.clear();
    for (VkImageView view : sceneImageViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    sceneImageViews.clear();
    if (m_allocator) {
        for (size_t i = 0; i < sceneImages.size(); ++i) {
            if (sceneImages[i])
                vmaDestroyImage(m_allocator, sceneImages[i], sceneAllocations[i]);
        }
    }
    sceneImages.clear();
    sceneAllocations.clear();
    for (VkImageView view : imageViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    imageViews.clear();
    images.clear();
    imagesInFlight.clear();
    if (handle) vkDestroySwapchainKHR(m_device, handle, nullptr);
    handle = VK_NULL_HANDLE;
}

VulkanSwapchainBundle::VulkanSwapchainBundle(
    VulkanSwapchainBundle&& other) noexcept
    : handle(other.handle),
      swapchainFormat(other.swapchainFormat),
      sceneFormat(other.sceneFormat),
      depthFormat(other.depthFormat),
      framebufferSrgb(other.framebufferSrgb),
      swapchainExtent(other.swapchainExtent),
      sampleCount(other.sampleCount),
      images(std::move(other.images)),
      imageViews(std::move(other.imageViews)),
      colorImages(std::move(other.colorImages)),
      colorAllocations(std::move(other.colorAllocations)),
      colorImageViews(std::move(other.colorImageViews)),
      sceneImages(std::move(other.sceneImages)),
      sceneAllocations(std::move(other.sceneAllocations)),
      sceneImageViews(std::move(other.sceneImageViews)),
      depthImages(std::move(other.depthImages)),
      depthAllocations(std::move(other.depthAllocations)),
      depthImageViews(std::move(other.depthImageViews)),
      renderPass(other.renderPass),
      presentRenderPass(other.presentRenderPass),
      pipelineLayout(other.pipelineLayout),
      pipeline(other.pipeline),
      translucentPipeline(other.translucentPipeline),
      uiPipeline(other.uiPipeline),
      postPipeline(other.postPipeline),
      postPipelineLayout(other.postPipelineLayout),
      postSampler(other.postSampler),
      postDescriptorSets(std::move(other.postDescriptorSets)),
      basicPipeline(other.basicPipeline),
      basicNoCullPipeline(other.basicNoCullPipeline),
      basicNoDepthPipeline(other.basicNoDepthPipeline),
      basicNoDepthNoCullPipeline(other.basicNoDepthNoCullPipeline),
      modelPipelineLayout(other.modelPipelineLayout),
      modelOpaquePipeline(other.modelOpaquePipeline),
      modelOpaqueDoubleSidedPipeline(other.modelOpaqueDoubleSidedPipeline),
      modelBlendPipeline(other.modelBlendPipeline),
      modelBlendDoubleSidedPipeline(other.modelBlendDoubleSidedPipeline),
      wirePipelineLayout(other.wirePipelineLayout),
      wirePipeline(other.wirePipeline),
      particlePipelineLayout(other.particlePipelineLayout),
      particlePipeline(other.particlePipeline),
      skyPipelineLayout(other.skyPipelineLayout),
      cloudPipelineLayout(other.cloudPipelineLayout),
      skyPipeline(other.skyPipeline),
      cloudPipeline(other.cloudPipeline),
      framebuffers(std::move(other.framebuffers)),
      presentFramebuffers(std::move(other.presentFramebuffers)),
      commandBuffers(std::move(other.commandBuffers)),
      imagesInFlight(std::move(other.imagesInFlight)),
      m_device(other.m_device),
      m_allocator(other.m_allocator),
      m_commandPool(other.m_commandPool),
      m_descriptorPool(other.m_descriptorPool) {
    other.handle = VK_NULL_HANDLE;
    other.m_device = VK_NULL_HANDLE;
    other.m_allocator = VK_NULL_HANDLE;
    other.m_commandPool = VK_NULL_HANDLE;
    other.m_descriptorPool = VK_NULL_HANDLE;
}

VulkanSwapchainBundle& VulkanSwapchainBundle::operator=(
    VulkanSwapchainBundle&& other) noexcept {
    if (this == &other) return *this;
    destroy();
    handle = other.handle;
    swapchainFormat = other.swapchainFormat;
    sceneFormat = other.sceneFormat;
    depthFormat = other.depthFormat;
    framebufferSrgb = other.framebufferSrgb;
    swapchainExtent = other.swapchainExtent;
    sampleCount = other.sampleCount;
    images = std::move(other.images);
    imageViews = std::move(other.imageViews);
    colorImages = std::move(other.colorImages);
    colorAllocations = std::move(other.colorAllocations);
    colorImageViews = std::move(other.colorImageViews);
    sceneImages = std::move(other.sceneImages);
    sceneAllocations = std::move(other.sceneAllocations);
    sceneImageViews = std::move(other.sceneImageViews);
    depthImages = std::move(other.depthImages);
    depthAllocations = std::move(other.depthAllocations);
    depthImageViews = std::move(other.depthImageViews);
    renderPass = other.renderPass;
    presentRenderPass = other.presentRenderPass;
    pipelineLayout = other.pipelineLayout;
    pipeline = other.pipeline;
    translucentPipeline = other.translucentPipeline;
    uiPipeline = other.uiPipeline;
    postPipeline = other.postPipeline;
    postPipelineLayout = other.postPipelineLayout;
    postSampler = other.postSampler;
    postDescriptorSets = std::move(other.postDescriptorSets);
    basicPipeline = other.basicPipeline;
    basicNoCullPipeline = other.basicNoCullPipeline;
    basicNoDepthPipeline = other.basicNoDepthPipeline;
    basicNoDepthNoCullPipeline = other.basicNoDepthNoCullPipeline;
    modelPipelineLayout = other.modelPipelineLayout;
    modelOpaquePipeline = other.modelOpaquePipeline;
    modelOpaqueDoubleSidedPipeline = other.modelOpaqueDoubleSidedPipeline;
    modelBlendPipeline = other.modelBlendPipeline;
    modelBlendDoubleSidedPipeline = other.modelBlendDoubleSidedPipeline;
    wirePipelineLayout = other.wirePipelineLayout;
    wirePipeline = other.wirePipeline;
    particlePipelineLayout = other.particlePipelineLayout;
    particlePipeline = other.particlePipeline;
    skyPipelineLayout = other.skyPipelineLayout;
    cloudPipelineLayout = other.cloudPipelineLayout;
    skyPipeline = other.skyPipeline;
    cloudPipeline = other.cloudPipeline;
    framebuffers = std::move(other.framebuffers);
    presentFramebuffers = std::move(other.presentFramebuffers);
    commandBuffers = std::move(other.commandBuffers);
    imagesInFlight = std::move(other.imagesInFlight);
    m_device = other.m_device;
    m_allocator = other.m_allocator;
    m_commandPool = other.m_commandPool;
    m_descriptorPool = other.m_descriptorPool;
    other.handle = VK_NULL_HANDLE;
    other.m_device = VK_NULL_HANDLE;
    other.m_allocator = VK_NULL_HANDLE;
    other.m_commandPool = VK_NULL_HANDLE;
    other.m_descriptorPool = VK_NULL_HANDLE;
    return *this;
}

}  // namespace vkp
