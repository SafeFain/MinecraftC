#pragma once

#include "renderer/Shadow.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// RAII ownership groups for the Vulkan backend. These are internal to the
// backend implementation; the public VulkanRenderer interface is untouched.
//
// Destruction order is carried by C++ member/base destruction order:
// VulkanRenderer::Impl derives from VulkanDeviceContext (destroyed last, so
// device/allocator/surface/instance outlive every resource group) and holds
// the resource groups as members declared after all plain fields, so they
// are destroyed first. The dynamic GPU resources (textures, meshes, per-frame
// buffers) are still released explicitly by Impl::cleanup before any group
// destructor runs.

namespace vkp {

class VulkanPipelineFactory;

// ── Context (base of Impl; destroyed last) ──────────────────────────────
// Instance, surface, physical/logical device, allocator, queues, and the
// command pool. Fields are plain (accessed unqualified from Impl); the
// destructor releases them in dependency order: pool -> allocator -> device
// -> surface -> instance.
struct VulkanDeviceContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    ~VulkanDeviceContext();
};

// ── Descriptor resources ────────────────────────────────────────────────
// The descriptor pool plus the five set layouts; created once with the
// device. Descriptor sets allocated from the pool are freed implicitly when
// the pool is destroyed.
struct VulkanDescriptorResources {
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout chunkDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout modelUniformDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout postDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout bloomDescriptorSetLayout = VK_NULL_HANDLE;

    void attach(VkDevice device) { m_device = device; }
    ~VulkanDescriptorResources();

private:
    VkDevice m_device = VK_NULL_HANDLE;
};

// ── Frame synchronization ───────────────────────────────────────────────
// Per-frame fences and acquire/render semaphores, created once with the
// device.
struct VulkanFrameSync {
    static constexpr size_t FRAMES_IN_FLIGHT = 2;
    std::array<VkFence, FRAMES_IN_FLIGHT> fences{};
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> renderFinished{};
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> imageAvailable{};

    static VulkanFrameSync create(VkDevice device);

    VulkanFrameSync() = default;
    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;
    VulkanFrameSync(VulkanFrameSync&& other) noexcept;
    VulkanFrameSync& operator=(VulkanFrameSync&& other) noexcept;
    ~VulkanFrameSync();

private:
    void destroy();
    VkDevice m_device = VK_NULL_HANDLE;
};

// ── Shadow resources ────────────────────────────────────────────────────
// Shadow pipeline/render pass (created once) and the shadow atlas
// image/view/sampler/framebuffer (rebuilt at runtime when the shadow quality
// changes via destroyImage()).
struct VulkanShadowResources {
    VkFormat shadowFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkImage shadowImage = VK_NULL_HANDLE;
    VmaAllocation shadowAllocation = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
    ShadowQuality shadowQuality = ShadowQuality::Low;

    void attach(VkDevice device, VmaAllocator allocator);
    void destroyImage();
    void destroy();
    ~VulkanShadowResources() { destroy(); }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

// ── Swapchain bundle ────────────────────────────────────────────────────
// Everything recreated when the swapchain is rebuilt: the swapchain itself,
// image views, MSAA scene/color/depth resources, render passes, every
// swapchain-dependent pipeline, framebuffers, command buffers, and the post
// descriptor sets. Rebuild is `bundle = VulkanSwapchainBundle::create(...)`
// (move assignment destroys the previous bundle first).
struct VulkanSwapchainBundle {
    static constexpr size_t MAX_BLOOM_LEVELS = 4;

    struct CreateParams {
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        uint32_t graphicsFamily = 0;
        uint32_t presentFamily = 0;
        int windowWidth = 1;
        int windowHeight = 1;
        bool synchronizePresentation = true;
        VkDescriptorSetLayout blockLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout chunkFrameLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout skyLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout modelLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout postLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout bloomLayout = VK_NULL_HANDLE;
        int bloomLevels = 0;
        VkSampleCountFlagBits requestedSampleCount = VK_SAMPLE_COUNT_1_BIT;
        VkSampleCountFlagBits maxSampleCount = VK_SAMPLE_COUNT_1_BIT;
        std::filesystem::path shaderRoot;
    };

    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkFormat sceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    bool framebufferSrgb = true;
    VkExtent2D swapchainExtent{};
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkImage> colorImages;
    std::vector<VmaAllocation> colorAllocations;
    std::vector<VkImageView> colorImageViews;
    std::vector<VkImage> sceneImages;
    std::vector<VmaAllocation> sceneAllocations;
    std::vector<VkImageView> sceneImageViews;
    std::vector<VkImage> depthImages;
    std::vector<VmaAllocation> depthAllocations;
    std::vector<VkImageView> depthImageViews;
    int bloomLevelCount = 0;
    std::array<VkExtent2D, MAX_BLOOM_LEVELS> bloomExtents{};
    std::array<std::vector<VkImage>, MAX_BLOOM_LEVELS> bloomImages;
    std::array<std::vector<VmaAllocation>, MAX_BLOOM_LEVELS> bloomAllocations;
    std::array<std::vector<VkImageView>, MAX_BLOOM_LEVELS> bloomImageViews;
    std::array<std::vector<VkFramebuffer>, MAX_BLOOM_LEVELS> bloomFramebuffers;
    std::array<std::vector<VkDescriptorSet>, MAX_BLOOM_LEVELS> bloomDescriptorSets;
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
    VkRenderPass bloomRenderPass = VK_NULL_HANDLE;
    VkPipelineLayout bloomPipelineLayout = VK_NULL_HANDLE;
    VkPipeline bloomPipeline = VK_NULL_HANDLE;
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
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkFramebuffer> presentFramebuffers;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkFence> imagesInFlight;

    static VulkanSwapchainBundle create(const CreateParams& params);

    VulkanSwapchainBundle() = default;
    VulkanSwapchainBundle(const VulkanSwapchainBundle&) = delete;
    VulkanSwapchainBundle& operator=(const VulkanSwapchainBundle&) = delete;
    VulkanSwapchainBundle(VulkanSwapchainBundle&& other) noexcept;
    VulkanSwapchainBundle& operator=(VulkanSwapchainBundle&& other) noexcept;
    void destroy();
    ~VulkanSwapchainBundle() { destroy(); }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};

}  // namespace vkp
