#pragma once

#include "renderer/backend/vulkan/VulkanRenderer.h"

#include "core/AssetStore.h"
#include "core/RuntimeClock.h"
#include "core/Window.h"
#include "debug/Log.h"
#include "model/ModelRenderer.h"
#include "model/ModelRenderLogic.h"
#include "renderer/BlockAtlasData.h"
#include "renderer/CloudRenderData.h"
#include "renderer/Shader.h"
#include "renderer/backend/vulkan/VulkanHelpers.h"
#include "renderer/backend/vulkan/VulkanIndexRebase.h"
#include "renderer/backend/vulkan/VulkanPipelineFactory.h"
#include "renderer/backend/vulkan/VulkanResources.h"
#include "Config.h"
#include "world/Block.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

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

namespace {

// Pure Vulkan helpers live in VulkanHelpers; expose them unqualified here.
using vkhelp::require;
using vkhelp::QueueFamilies;
using vkhelp::findQueueFamilies;
using vkhelp::supportsDeviceExtension;
#if defined(__APPLE__)
using vkhelp::supportsInstanceExtension;
#endif
using vkhelp::supportsSwapchain;
using vkhelp::chooseSurfaceFormat;
using vkhelp::isSrgbFormat;
using vkhelp::choosePresentMode;
using vkhelp::chooseCompositeAlpha;
using vkhelp::chooseSurfaceTransform;
using vkhelp::chooseExtent;
using vkhelp::findDepthFormat;
using vkhelp::loadRgbaTexture;

// Push-constant and uniform-buffer layouts live with the pipeline factory.
using vkp::FrameUniforms;
using vkp::ChunkEnvironmentUniforms;
using vkp::ShadowConstants;
using vkp::ParticleUniforms;
using vkp::SkyUniforms;
using vkp::CloudUniforms;
using vkp::WireUniforms;
using vkp::UiConstants;
using vkp::PostConstants;
using vkp::ModelUniforms;

static_assert(sizeof(ParticleRenderData) == 32);
static_assert(offsetof(ParticleRenderData, position) == 0);
static_assert(offsetof(ParticleRenderData, kind) == 12);
static_assert(offsetof(ParticleRenderData, phase) == 16);
static_assert(offsetof(ParticleRenderData, texture) == 20);
static_assert(offsetof(ParticleRenderData, size) == 24);
static_assert(offsetof(ParticleRenderData, rotation) == 28);
static_assert(sizeof(CloudInstance) == 28);
static_assert(offsetof(CloudInstance, width) == 12);
static_assert(offsetof(CloudInstance, height) == 20);
static_assert(offsetof(CloudInstance, visibleFaces) == 24);

} // namespace

struct VulkanRenderer::Impl : vkp::VulkanDeviceContext {
    static constexpr size_t FRAMES_IN_FLIGHT = 2;

    struct Buffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    struct GpuMesh {
        Buffer vertex;
        Buffer index;
        uint32_t arenaPage = std::numeric_limits<uint32_t>::max();
        VkDeviceSize vertexOffset = 0;
        VkDeviceSize indexOffset = 0;
        VkDeviceSize vertexBytes = 0;
        VkDeviceSize indexBytes = 0;
        uint32_t indexCount = 0;
        MeshVertexLayout layout = MeshVertexLayout::PositionUv;
    };
    struct GpuTexture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };
    struct GpuMaterial {
        MaterialDesc desc{};
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    struct UiSubmission {
        std::vector<UiMeshVertex> vertices;
        std::vector<uint32_t> indices;
        RenderMaterialHandle material{};
        glm::mat4 projection{1.0f};
    };
    struct UiFrameBuffers {
        Buffer vertex;
        Buffer index;
        size_t vertexCapacity = 0;
        size_t indexCapacity = 0;
    };
    struct ParticleFrameBuffer {
        Buffer instance;
        size_t capacity = 0;
    };
    struct SkyFrameBuffer {
        Buffer uniform;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    struct ChunkFrameBuffer {
        Buffer uniform;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    struct CloudFrameBuffer {
        Buffer instance;
        size_t capacity = 0;
        uint64_t revision = 0;
    };
    struct ModelPrimitive {
        Buffer vertex;
        Buffer index;
        uint32_t indexCount = 0;
        int material = -1;
        int skin = -1;
        int node = -1;
    };
    struct ModelResource {
        std::shared_ptr<const model::ModelAsset> asset;
        std::vector<ModelPrimitive> primitives;
        std::vector<RenderTextureHandle> textures;
        std::vector<RenderMaterialHandle> textureMaterials;
    };
    struct ModelFrameBuffer {
        Buffer uniform;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        size_t capacity = 0;
    };
    struct ModelPassSubmission {
        model::ModelDraw draw;
        glm::mat4 viewProjection{1.0f};
        RenderEnvironment environment;
        glm::vec3 cameraPosition{0.0f};
        float fogStart = 0.0f;
        float fogEnd = 1.0f;
    };
    struct PendingBufferUpload {
        VkBuffer destination = VK_NULL_HANDLE;
        VkDeviceSize destinationOffset = 0;
        std::vector<uint8_t> bytes;
    };
    struct PreparedBufferCopy {
        VkBuffer destination = VK_NULL_HANDLE;
        VkDeviceSize sourceOffset = 0;
        VkDeviceSize destinationOffset = 0;
        VkDeviceSize size = 0;
    };
    struct PendingImageUpload {
        VkImage destination = VK_NULL_HANDLE;
        uint32_t mipLevels = 1;
        std::vector<uint8_t> bytes;
        std::vector<VkBufferImageCopy> regions;
    };
    struct PreparedImageCopy {
        VkImage destination = VK_NULL_HANDLE;
        uint32_t mipLevels = 1;
        std::vector<VkBufferImageCopy> regions;
    };
    struct UploadFrameBuffer {
        Buffer staging;
        VkDeviceSize capacity = 0;
    };
    struct FreeRange {
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };
    struct ChunkArenaPage {
        Buffer vertex;
        Buffer index;
        VkDeviceSize vertexCapacity = 0;
        VkDeviceSize indexCapacity = 0;
        std::vector<FreeRange> freeVertices;
        std::vector<FreeRange> freeIndices;
    };

    explicit Impl(Window& owner, std::filesystem::path root)
        : window(owner), assetRoot(std::move(root)) {
        createInstance();
        surface = reinterpret_cast<VkSurfaceKHR>(
            window.createVulkanSurface(reinterpret_cast<void*>(instance)));
        pickPhysicalDevice();
        createDevice();
        LOG_INFO("Vulkan device initialized");
        createAllocator();
        LOG_INFO("Vulkan memory allocator initialized");
        createCommandPool();
        LOG_INFO("Vulkan command pool initialized");
        createDescriptorLayout();
        createDescriptorPool();
        descriptors.attach(device);
        LOG_INFO("Vulkan descriptor resources initialized");
        {
            const vkp::VulkanPipelineFactory pipelineFactory(
                device, assetRoot / "shaders" / "vulkan");
            vkp::ShadowPipelineOutputs shadowOutputs;
            pipelineFactory.createShadowSet(
                {physicalDevice, descriptors.descriptorSetLayout}, shadowOutputs);
            shadow.attach(device, allocator);
            shadow.shadowFormat = shadowOutputs.shadowFormat;
            shadow.shadowRenderPass = shadowOutputs.shadowRenderPass;
            shadow.shadowPipelineLayout = shadowOutputs.shadowPipelineLayout;
            shadow.shadowPipeline = shadowOutputs.shadowPipeline;
        }
        createShadowImage(ShadowQuality::Low);
        createSkyResources();
        LOG_INFO("Vulkan frame resources initialized");
        frameSync = vkp::VulkanFrameSync::create(device);
        LOG_INFO("Vulkan synchronization initialized");
        createSwapchain();
        LOG_INFO("VulkanRenderer initialized with VMA");
    }

    ~Impl() { cleanup(); }

    Window& window;
    std::filesystem::path assetRoot;
    bool presentationSuspended = false;
    VkSampleCountFlagBits maxSampleCount = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits requestedSampleCount = VK_SAMPLE_COUNT_2_BIT;
    ShadowCascades shadowCascades{};
    ShadowCascades shadowBaseCascades{};
    std::vector<ShadowChunkSubmission> submittedShadowChunks;
    bool shadowUpdateQueued = false;
    RuntimeClock::Tick lastShadowUpdate = 0;
    glm::dvec3 lastShadowWorldOrigin{0.0};
    glm::vec3 lastShadowDirection{0.0f};
    VkDescriptorSet shadowAtlasSet = VK_NULL_HANDLE;
    uint32_t submittedShadowAtlasTiles = 1;
    std::unordered_map<uint32_t, GpuMesh> meshes;
    std::unordered_map<uint32_t, GpuTexture> textures;
    std::unordered_map<uint32_t, GpuMaterial> materials;
    uint32_t nextMeshHandle = 1;
    uint32_t nextTextureHandle = 1;
    uint32_t nextMaterialHandle = 1;
    // RAII resource groups, declared after the plain state so their
    // destructors run before the VulkanDeviceContext base releases the
    // device/allocator; cleanup() releases the dynamic resources first.
    vkp::VulkanDescriptorResources descriptors;
    vkp::VulkanFrameSync frameSync;
    vkp::VulkanShadowResources shadow;
    vkp::VulkanSwapchainBundle swapchain;
    size_t currentFrame = 0;
    bool swapchainDirty = false;
    bool firstFrameLogged = false;
    FrameData submittedFrame{};
    std::vector<DrawCommand> submittedDraws;
    std::vector<DrawCommand> submittedViewModels;
    bool queueViewModel = false;
    std::vector<UiSubmission> submittedUi;
    std::array<UiFrameBuffers, FRAMES_IN_FLIGHT> uiBuffers{};
    std::array<ParticleFrameBuffer, FRAMES_IN_FLIGHT> particleBuffers{};
    std::array<SkyFrameBuffer, FRAMES_IN_FLIGHT> skyBuffers{};
    std::array<ChunkFrameBuffer, FRAMES_IN_FLIGHT> chunkBuffers{};
    std::array<CloudFrameBuffer, FRAMES_IN_FLIGHT> cloudBuffers{};
    std::array<ModelFrameBuffer, FRAMES_IN_FLIGHT> modelBuffers{};
    std::array<UploadFrameBuffer, FRAMES_IN_FLIGHT> uploadBuffers{};
    std::vector<ChunkArenaPage> chunkArenaPages;
    std::vector<ParticleRenderData> submittedParticles;
    SkyUniforms submittedSky{};
    ChunkEnvironmentUniforms submittedChunkEnvironment{};
    bool skyQueued = false;
    std::vector<CloudInstance> cloudInstances;
    glm::vec3 cloudOrigin{0.0f};
    glm::vec3 cloudColor{1.0f};
    glm::mat4 cloudViewProjection{1.0f};
    uint64_t cloudRevision = 0;
    uint64_t cloudCacheSeed = 0;
    int cloudCacheCenterX = 0;
    int cloudCacheCenterZ = 0;
    int cloudCacheRadius = -1;
    bool cloudsQueued = false;
    glm::mat4 wireModelViewProjection{1.0f};
    bool wireQueued = false;
    glm::mat4 particleViewProjection{1.0f};
    glm::vec3 particleCameraRight{1.0f,0.0f,0.0f};
    glm::vec3 particleCameraUp{0.0f,1.0f,0.0f};
    float particleIntensity = 0.0f;
    float particleTime = 0.0f;
    RenderMaterialHandle particleMaterial{};
    RenderMaterialHandle modelFallbackMaterial{};
    std::vector<ModelResource> models;
    std::vector<ModelPassSubmission> submittedModelOpaque;
    std::vector<ModelPassSubmission> submittedModelBlend;
    VkDeviceSize modelUniformStride = 0;
    std::vector<PendingBufferUpload> pendingBufferUploads;
    std::vector<PreparedBufferCopy> preparedBufferCopies;
    std::vector<PendingImageUpload> pendingImageUploads;
    std::vector<PreparedImageCopy> preparedImageCopies;
    std::array<std::vector<GpuMesh>, FRAMES_IN_FLIGHT> retiredMeshes{};
    bool frameBegun = false;
    bool drawQueued = false;
    VisualQuality visualQuality = VisualQuality::Medium;
    PostProcessState postProcess{};
    bool sceneFinished = false;
    RendererPerformanceStats performance{};

    void createInstance() {
        std::vector<std::string> extensionStorage =
            window.requiredVulkanInstanceExtensions();
        bool enumeratePortability = false;
#if defined(__APPLE__) && TARGET_OS_SIMULATOR
        const bool configureSimulator = supportsInstanceExtension(
            VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        if (configureSimulator &&
            std::find(extensionStorage.begin(), extensionStorage.end(),
                      VK_EXT_LAYER_SETTINGS_EXTENSION_NAME) ==
                extensionStorage.end())
            extensionStorage.emplace_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
#endif
#if defined(__APPLE__)
        enumeratePortability = supportsInstanceExtension(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        if (enumeratePortability &&
            std::find(extensionStorage.begin(), extensionStorage.end(),
                      VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) ==
                extensionStorage.end())
            extensionStorage.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
        std::vector<const char*> extensions;
        extensions.reserve(extensionStorage.size());
        for (const std::string& extension : extensionStorage)
            extensions.push_back(extension.c_str());
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "MinecraftC Vulkan Basic Renderer";
        application.applicationVersion = VK_MAKE_VERSION(
            MINECRAFTC_VERSION_MAJOR,
            MINECRAFTC_VERSION_MINOR,
            MINECRAFTC_VERSION_PATCH);
        application.pEngineName = "MinecraftC";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if defined(__APPLE__) && TARGET_OS_SIMULATOR
        const VkBool32 disableArgumentBuffers = VK_FALSE;
        const VkLayerSettingEXT simulatorSetting{
            "MoltenVK",
            "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS",
            VK_LAYER_SETTING_TYPE_BOOL32_EXT,
            1,
            &disableArgumentBuffers};
        const VkLayerSettingsCreateInfoEXT simulatorSettings{
            VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            nullptr,
            1,
            &simulatorSetting};
        if (configureSimulator) info.pNext = &simulatorSettings;
#endif
#if defined(__APPLE__)
        if (enumeratePortability)
            info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#else
        (void)enumeratePortability;
#endif
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        require(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
    }

    void pickPhysicalDevice() {
        uint32_t count = 0;
        require(vkEnumeratePhysicalDevices(instance, &count, nullptr),
                "vkEnumeratePhysicalDevices");
        if (count == 0) throw std::runtime_error("No Vulkan physical device is available");
        std::vector<VkPhysicalDevice> devices(count);
        require(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
                "vkEnumeratePhysicalDevices");
        for (VkPhysicalDevice candidate : devices) {
            const QueueFamilies queues = findQueueFamilies(candidate, surface);
            if (!queues.complete() || !supportsSwapchain(candidate)) continue;
            uint32_t formatCount = 0;
            uint32_t modeCount = 0;
            require(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface,
                        &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
            require(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface,
                        &modeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
            if (formatCount == 0 || modeCount == 0) continue;
            physicalDevice = candidate;
            graphicsFamily = *queues.graphics;
            presentFamily = *queues.present;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            const VkSampleCountFlags counts =
                properties.limits.framebufferColorSampleCounts &
                properties.limits.framebufferDepthSampleCounts;
            maxSampleCount = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT :
                (counts & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT :
                VK_SAMPLE_COUNT_1_BIT;
            swapchain.sampleCount = maxSampleCount >= requestedSampleCount
                ? requestedSampleCount : maxSampleCount;
            LOG_INFO("Vulkan device: " << properties.deviceName);
            LOG_INFO("Vulkan MSAA: " << static_cast<uint32_t>(swapchain.sampleCount) << "x");
            return;
        }
        throw std::runtime_error("No Vulkan device supports graphics and presentation");
    }

    void configureVisualQuality(VisualQuality quality) {
        visualQuality = quality;
        const int samples = visualQualityConfig(quality).sceneSamples;
        requestedSampleCount = samples >= 4 ? VK_SAMPLE_COUNT_4_BIT :
            samples >= 2 ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
        const VkSampleCountFlagBits effective = maxSampleCount >= requestedSampleCount
            ? requestedSampleCount : maxSampleCount;
        if (effective == swapchain.sampleCount) return;
        swapchain.sampleCount = effective;
        swapchainDirty = true;
        LOG_INFO("Vulkan visual quality selected " << samples
                 << "x scene MSAA; effective "
                 << static_cast<uint32_t>(swapchain.sampleCount) << "x");
    }

    void createDevice() {
        const std::set<uint32_t> uniqueFamilies{graphicsFamily, presentFamily};
        constexpr float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queue{};
            queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            queues.push_back(queue);
        }
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#if defined(__APPLE__)
        constexpr const char* portabilitySubset = "VK_KHR_portability_subset";
        if (supportsDeviceExtension(
                physicalDevice, portabilitySubset))
            extensions.push_back(portabilitySubset);
#endif
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
        info.pQueueCreateInfos = queues.data();
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        require(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
    }

    void createAllocator() {
        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo info{};
        info.instance = instance;
        info.physicalDevice = physicalDevice;
        info.device = device;
        info.vulkanApiVersion = VK_API_VERSION_1_0;
        info.pVulkanFunctions = &functions;
        require(vmaCreateAllocator(&info, &allocator), "vmaCreateAllocator");
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        info.queueFamilyIndex = graphicsFamily;
        require(vkCreateCommandPool(device, &info, nullptr, &commandPool),
                "vkCreateCommandPool");
    }

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags = 0) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = memoryUsage;
        allocationInfo.flags = flags;
        VmaAllocationInfo resultInfo{};
        Buffer result;
        require(vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
                                &result.handle, &result.allocation, &resultInfo),
                "vmaCreateBuffer");
        result.mapped = resultInfo.pMappedData;
        return result;
    }

    void destroyBuffer(Buffer& buffer) {
        if (buffer.handle) {
            pendingBufferUploads.erase(std::remove_if(
                pendingBufferUploads.begin(), pendingBufferUploads.end(),
                [&](const PendingBufferUpload& upload) {
                    return upload.destination == buffer.handle;
                }), pendingBufferUploads.end());
        }
        if (buffer.handle) vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
        buffer = {};
    }

    template<typename T>
    Buffer uploadDeviceBuffer(const std::vector<T>& data, VkBufferUsageFlags usage) {
        const VkDeviceSize size = data.size() * sizeof(T);
        Buffer target = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        PendingBufferUpload upload;
        upload.destination = target.handle;
        upload.bytes.resize(static_cast<size_t>(size));
        std::memcpy(upload.bytes.data(), data.data(), static_cast<size_t>(size));
        pendingBufferUploads.push_back(std::move(upload));
        return target;
    }

    GpuMesh createGeometry(const MeshData& data) {
        GpuMesh result;
        result.vertex = data.layout == MeshVertexLayout::Chunk
            ? uploadDeviceBuffer(data.chunkVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            : uploadDeviceBuffer(data.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        try {
            result.index = uploadDeviceBuffer(
            data.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        } catch (...) {
            destroyBuffer(result.vertex);
            throw;
        }
        result.indexCount = static_cast<uint32_t>(data.indices.size());
        result.layout = data.layout;
        return result;
    }

    static std::optional<VkDeviceSize> allocateRange(
        std::vector<FreeRange>& ranges, VkDeviceSize size) {
        for (size_t index = 0; index < ranges.size(); ++index) {
            if (ranges[index].size < size) continue;
            const VkDeviceSize offset = ranges[index].offset;
            ranges[index].offset += size;
            ranges[index].size -= size;
            if (ranges[index].size == 0) ranges.erase(ranges.begin() + index);
            return offset;
        }
        return std::nullopt;
    }

    static void releaseRange(std::vector<FreeRange>& ranges,
                             VkDeviceSize offset, VkDeviceSize size) {
        if (size == 0) return;
        ranges.push_back({offset, size});
        std::sort(ranges.begin(), ranges.end(),
            [](const FreeRange& a, const FreeRange& b) {
                return a.offset < b.offset;
            });
        size_t output = 0;
        for (const FreeRange& range : ranges) {
            if (output > 0 &&
                ranges[output - 1].offset + ranges[output - 1].size == range.offset) {
                ranges[output - 1].size += range.size;
            } else {
                ranges[output++] = range;
            }
        }
        ranges.resize(output);
    }

    uint32_t createChunkArenaPage(VkDeviceSize vertexBytes,
                                  VkDeviceSize indexBytes) {
        constexpr VkDeviceSize INITIAL_VERTEX_BYTES = 8u * 1024u * 1024u;
        constexpr VkDeviceSize INITIAL_INDEX_BYTES = 2u * 1024u * 1024u;
        ChunkArenaPage page;
        page.vertexCapacity = std::max(INITIAL_VERTEX_BYTES, vertexBytes);
        page.indexCapacity = std::max(INITIAL_INDEX_BYTES, indexBytes);
        page.vertex = createBuffer(page.vertexCapacity,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        try {
            page.index = createBuffer(page.indexCapacity,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        } catch (...) {
            destroyBuffer(page.vertex);
            throw;
        }
        page.freeVertices.push_back({0, page.vertexCapacity});
        page.freeIndices.push_back({0, page.indexCapacity});
        LOG_INFO("Vulkan Chunk arena page: " << page.vertexCapacity
                 << " vertex bytes, " << page.indexCapacity << " index bytes");
        chunkArenaPages.push_back(std::move(page));
        return static_cast<uint32_t>(chunkArenaPages.size() - 1);
    }

    GpuMesh createChunkGeometry(const ChunkMesh& mesh) {
        GpuMesh result;
        result.vertexBytes = mesh.vertices.size() * sizeof(MeshVertex);
        result.indexBytes = mesh.indices.size() * sizeof(uint32_t);
        std::optional<VkDeviceSize> vertexOffset;
        std::optional<VkDeviceSize> indexOffset;
        for (uint32_t pageIndex = 0; pageIndex < chunkArenaPages.size(); ++pageIndex) {
            ChunkArenaPage& page = chunkArenaPages[pageIndex];
            vertexOffset = allocateRange(page.freeVertices, result.vertexBytes);
            if (!vertexOffset) continue;
            indexOffset = allocateRange(page.freeIndices, result.indexBytes);
            if (indexOffset) {
                result.arenaPage = pageIndex;
                break;
            }
            releaseRange(page.freeVertices, *vertexOffset, result.vertexBytes);
            vertexOffset.reset();
        }
        if (result.arenaPage == std::numeric_limits<uint32_t>::max()) {
            result.arenaPage = createChunkArenaPage(
                result.vertexBytes, result.indexBytes);
            ChunkArenaPage& page = chunkArenaPages[result.arenaPage];
            vertexOffset = allocateRange(page.freeVertices, result.vertexBytes);
            indexOffset = allocateRange(page.freeIndices, result.indexBytes);
        }
        if (!vertexOffset || !indexOffset)
            throw std::runtime_error("Vulkan Chunk arena allocation failed");
        result.vertexOffset = *vertexOffset;
        result.indexOffset = *indexOffset;
        ChunkArenaPage& page = chunkArenaPages[result.arenaPage];
        try {
            PendingBufferUpload vertices;
            vertices.destination = page.vertex.handle;
            vertices.destinationOffset = result.vertexOffset;
            vertices.bytes.resize(static_cast<size_t>(result.vertexBytes));
            std::memcpy(vertices.bytes.data(), mesh.vertices.data(), vertices.bytes.size());
            pendingBufferUploads.push_back(std::move(vertices));
            PendingBufferUpload indices;
            indices.destination = page.index.handle;
            indices.destinationOffset = result.indexOffset;
            indices.bytes.resize(static_cast<size_t>(result.indexBytes));
            if (result.vertexOffset % sizeof(MeshVertex) != 0)
                throw std::runtime_error("Vulkan Chunk vertex arena is misaligned");
            const VkDeviceSize vertexBase64 = result.vertexOffset / sizeof(MeshVertex);
            if (vertexBase64 > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("Vulkan Chunk vertex base exceeds uint32_t");
            const uint32_t vertexBase = static_cast<uint32_t>(vertexBase64);
            for (size_t index = 0; index < mesh.indices.size(); ++index) {
                const uint32_t rebased = rebaseVulkanIndex(
                    mesh.indices[index], vertexBase);
                std::memcpy(indices.bytes.data() + index * sizeof(uint32_t),
                            &rebased, sizeof(rebased));
            }
            pendingBufferUploads.push_back(std::move(indices));
        } catch (...) {
            destroyGpuMesh(result);
            throw;
        }
        result.indexCount = static_cast<uint32_t>(mesh.indices.size());
        result.layout = MeshVertexLayout::Chunk;
        return result;
    }

    void destroyGpuMesh(GpuMesh& mesh) {
        if (mesh.arenaPage != std::numeric_limits<uint32_t>::max()) {
            ChunkArenaPage& page = chunkArenaPages.at(mesh.arenaPage);
            pendingBufferUploads.erase(std::remove_if(
                pendingBufferUploads.begin(), pendingBufferUploads.end(),
                [&](const PendingBufferUpload& upload) {
                    return (upload.destination == page.vertex.handle &&
                            upload.destinationOffset == mesh.vertexOffset) ||
                           (upload.destination == page.index.handle &&
                            upload.destinationOffset == mesh.indexOffset);
                }), pendingBufferUploads.end());
            releaseRange(page.freeVertices, mesh.vertexOffset, mesh.vertexBytes);
            releaseRange(page.freeIndices, mesh.indexOffset, mesh.indexBytes);
        } else {
            destroyBuffer(mesh.index);
            destroyBuffer(mesh.vertex);
        }
        mesh = {};
    }

    GpuTexture createTextureResource(const TextureData& data,
                                     const TextureSamplerDesc& samplerDesc) {
        GpuTexture result;
        VkDeviceSize byteCount = data.pixels.size();
        for (const auto& mip : data.mipLevels) byteCount += mip.pixels.size();
        try {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {data.width, data.height, 1};
            imageInfo.mipLevels = static_cast<uint32_t>(data.mipLevels.size() + 1);
            imageInfo.arrayLayers = 1;
            const VkFormat textureFormat = data.format == TextureFormat::Rgba8Srgb
                ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.format = textureFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo allocationInfo{};
            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            require(vmaCreateImage(allocator, &imageInfo, &allocationInfo,
                                   &result.image, &result.allocation, nullptr),
                    "vmaCreateImage");
            PendingImageUpload upload;
            upload.destination = result.image;
            upload.mipLevels = imageInfo.mipLevels;
            upload.bytes.resize(static_cast<size_t>(byteCount));
            std::memcpy(upload.bytes.data(), data.pixels.data(), data.pixels.size());
            size_t mappedOffset = data.pixels.size();
            for (const auto& mip : data.mipLevels) {
                std::memcpy(upload.bytes.data() + mappedOffset,
                            mip.pixels.data(), mip.pixels.size());
                mappedOffset += mip.pixels.size();
            }
            upload.regions.resize(imageInfo.mipLevels);
            VkDeviceSize offset = 0;
            for (uint32_t level = 0; level < imageInfo.mipLevels; ++level) {
                const uint32_t width = level == 0 ? data.width
                    : data.mipLevels[level - 1].width;
                const uint32_t height = level == 0 ? data.height
                    : data.mipLevels[level - 1].height;
                auto& copy = upload.regions[level];
                copy.bufferOffset = offset;
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.mipLevel = level;
                copy.imageSubresource.layerCount = 1;
                copy.imageExtent = {width, height, 1};
                offset += static_cast<VkDeviceSize>(width) * height * 4u;
            }
            pendingImageUploads.push_back(std::move(upload));
        } catch (...) {
            if (result.image)
                vmaDestroyImage(allocator, result.image, result.allocation);
            throw;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = result.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = data.format == TextureFormat::Rgba8Srgb
            ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = static_cast<uint32_t>(data.mipLevels.size() + 1);
        viewInfo.subresourceRange.layerCount = 1;
        require(vkCreateImageView(device, &viewInfo, nullptr, &result.view),
                "vkCreateImageView");

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = samplerDesc.magFilter == TextureFilter::Nearest
            ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        samplerInfo.minFilter = samplerDesc.minFilter == TextureFilter::Linear
            ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = samplerDesc.minFilter == TextureFilter::NearestMipmapLinear
            ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = samplerDesc.addressU == TextureAddressMode::Repeat
            ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = samplerDesc.addressV == TextureAddressMode::Repeat
            ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = static_cast<float>(data.mipLevels.size());
        require(vkCreateSampler(device, &samplerInfo, nullptr, &result.sampler),
                "vkCreateSampler");
        LOG_INFO("Vulkan texture queued (" << data.width << "x"
                 << data.height << (data.format == TextureFormat::Rgba8Srgb
                    ? " sRGB, " : " linear, ") << data.mipLevels.size() + 1
                 << " mip levels)");
        return result;
    }

    void createDescriptorLayout() {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        for (uint32_t binding = 0; binding < bindings.size(); ++binding) {
            bindings[binding].binding = binding;
            bindings[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[binding].descriptorCount = 1;
            bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = bindings.size();
        info.pBindings = bindings.data();
        require(vkCreateDescriptorSetLayout(device, &info, nullptr, &descriptors.descriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        VkDescriptorSetLayoutBinding skyBinding{};
        skyBinding.binding = 0;
        skyBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyBinding.descriptorCount = 1;
        skyBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        info.bindingCount = 1;
        info.pBindings = &skyBinding;
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &descriptors.skyDescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        std::array<VkDescriptorSetLayoutBinding,2> chunkBindings{};
        chunkBindings[0]=skyBinding;
        chunkBindings[0].stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
        chunkBindings[1].binding=1;
        chunkBindings[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        chunkBindings[1].descriptorCount=1;
        chunkBindings[1].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        info.bindingCount = chunkBindings.size();
        info.pBindings = chunkBindings.data();
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &descriptors.chunkDescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        VkDescriptorSetLayoutBinding modelBinding{};
        modelBinding.binding = 0;
        modelBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        modelBinding.descriptorCount = 1;
        modelBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT;
        info.bindingCount = 1;
        info.pBindings = &modelBinding;
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &descriptors.modelUniformDescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        VkDescriptorSetLayoutBinding postBinding{};
        postBinding.binding = 0;
        postBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postBinding.descriptorCount = 1;
        postBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        info.bindingCount = 1;
        info.pBindings = &postBinding;
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &descriptors.postDescriptorSetLayout),
                "vkCreateDescriptorSetLayout(post)");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        const VkDeviceSize alignment = std::max<VkDeviceSize>(
            1, properties.limits.minUniformBufferOffsetAlignment);
        modelUniformStride = (sizeof(ModelUniforms) + alignment - 1) & ~(alignment - 1);
    }

    void createDescriptorPool() {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12288},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
             static_cast<uint32_t>(FRAMES_IN_FLIGHT * 2)},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
             static_cast<uint32_t>(FRAMES_IN_FLIGHT)}}};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 4096 + static_cast<uint32_t>(FRAMES_IN_FLIGHT * 3);
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptors.descriptorPool),
                "vkCreateDescriptorPool");
    }

    void createShadowImage(ShadowQuality quality) {
        const ShadowConfig config=shadowConfig(quality);
        const int columns=config.cascadeCount==1?1:2;
        const int rows=(config.cascadeCount+columns-1)/columns;
        shadow.destroyImage();shadow.shadowQuality=quality;
        VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType=VK_IMAGE_TYPE_2D;image.format=shadow.shadowFormat;
        image.extent={static_cast<uint32_t>(config.resolution*columns),
                      static_cast<uint32_t>(config.resolution*rows),1};
        image.mipLevels=1;image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|
            VK_IMAGE_USAGE_SAMPLED_BIT;image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        require(vmaCreateImage(allocator,&image,&allocation,&shadow.shadowImage,&shadow.shadowAllocation,nullptr),"vmaCreateImage(shadow)");
        VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=shadow.shadowImage;
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=shadow.shadowFormat;
        view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;view.subresourceRange.levelCount=1;
        view.subresourceRange.layerCount=1;
        require(vkCreateImageView(device,&view,nullptr,&shadow.shadowImageView),"vkCreateImageView(shadow)");
        VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter=VK_FILTER_NEAREST;sampler.minFilter=VK_FILTER_NEAREST;
        sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.maxLod=0.0f;
        require(vkCreateSampler(device,&sampler,nullptr,&shadow.shadowSampler),"vkCreateSampler(shadow)");
        VkFramebufferCreateInfo framebuffer{};framebuffer.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer.renderPass=shadow.shadowRenderPass;framebuffer.attachmentCount=1;
        framebuffer.pAttachments=&shadow.shadowImageView;framebuffer.width=image.extent.width;
        framebuffer.height=image.extent.height;framebuffer.layers=1;
        require(vkCreateFramebuffer(device,&framebuffer,nullptr,&shadow.shadowFramebuffer),"vkCreateFramebuffer(shadow)");
        VkDescriptorImageInfo imageInfo{shadow.shadowSampler,shadow.shadowImageView,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        for(auto& frame:chunkBuffers) if(frame.descriptorSet){
            VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=frame.descriptorSet;
            write.dstBinding=1;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo=&imageInfo;vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        }
    }

    void createSkyResources() {
        for (SkyFrameBuffer& frame : skyBuffers) {
            frame.uniform = createBuffer(
                sizeof(SkyUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptors.descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &descriptors.skyDescriptorSetLayout;
            require(vkAllocateDescriptorSets(device, &allocate, &frame.descriptorSet),
                    "vkAllocateDescriptorSets");
            VkDescriptorBufferInfo bufferInfo{
                frame.uniform.handle, 0, sizeof(SkyUniforms)};
            VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet=frame.descriptorSet;write.dstBinding=0;write.descriptorCount=1;
            write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&bufferInfo;
            vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        }
        for (ChunkFrameBuffer& frame : chunkBuffers) {
            frame.uniform = createBuffer(
                sizeof(ChunkEnvironmentUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptors.descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &descriptors.chunkDescriptorSetLayout;
            require(vkAllocateDescriptorSets(device, &allocate, &frame.descriptorSet),
                    "vkAllocateDescriptorSets");
            VkDescriptorBufferInfo bufferInfo{
                frame.uniform.handle, 0, sizeof(ChunkEnvironmentUniforms)};
            VkDescriptorImageInfo imageInfo{shadow.shadowSampler,shadow.shadowImageView,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            std::array<VkWriteDescriptorSet,2> writes{};
            writes[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[0].dstSet=frame.descriptorSet;
            writes[0].dstBinding=0;writes[0].descriptorCount=1;
            writes[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[0].pBufferInfo=&bufferInfo;
            writes[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[1].dstSet=frame.descriptorSet;
            writes[1].dstBinding=1;writes[1].descriptorCount=1;
            writes[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[1].pImageInfo=&imageInfo;
            vkUpdateDescriptorSets(device,writes.size(),writes.data(),0,nullptr);
        }
    }

    VkDescriptorSet createMaterialDescriptor(const GpuTexture& texture,
                                             const GpuTexture& normal,
                                             const GpuTexture& properties) {
        VkDescriptorSetAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool = descriptors.descriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &descriptors.descriptorSetLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        require(vkAllocateDescriptorSets(device, &allocate, &set),
                "vkAllocateDescriptorSets");
        const std::array<const GpuTexture*, 3> textures{
            &texture, &normal, &properties};
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        std::array<VkWriteDescriptorSet, 3> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            imageInfos[binding].sampler = textures[binding]->sampler;
            imageInfos[binding].imageView = textures[binding]->view;
            imageInfos[binding].imageLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = set;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[binding].pImageInfo = &imageInfos[binding];
        }
        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
        return set;
    }

    void createSwapchain() {
        vkp::VulkanSwapchainBundle::CreateParams params;
        params.device = device;
        params.allocator = allocator;
        params.commandPool = commandPool;
        params.descriptorPool = descriptors.descriptorPool;
        params.physicalDevice = physicalDevice;
        params.surface = surface;
        params.graphicsFamily = graphicsFamily;
        params.presentFamily = presentFamily;
        params.windowWidth = window.width();
        params.windowHeight = window.height();
        params.synchronizePresentation = window.synchronizePresentation();
        params.blockLayout = descriptors.descriptorSetLayout;
        params.chunkFrameLayout = descriptors.chunkDescriptorSetLayout;
        params.skyLayout = descriptors.skyDescriptorSetLayout;
        params.modelLayout = descriptors.modelUniformDescriptorSetLayout;
        params.postLayout = descriptors.postDescriptorSetLayout;
        params.requestedSampleCount = requestedSampleCount;
        params.maxSampleCount = maxSampleCount;
        params.shaderRoot = assetRoot / "shaders" / "vulkan";
        swapchain = vkp::VulkanSwapchainBundle::create(params);
        swapchainDirty = false;
    }


    void prepareUiBuffers() {
        size_t vertexCount = 0, indexCount = 0;
        for (const auto& batch : submittedUi) {
            vertexCount += batch.vertices.size();
            indexCount += batch.indices.size();
        }
        if (vertexCount == 0 || indexCount == 0) return;
        UiFrameBuffers& buffers = uiBuffers[currentFrame];
        auto grow = [&](Buffer& buffer, size_t& capacity, size_t required,
                        size_t elementSize, VkBufferUsageFlags usage) {
            if (capacity >= required) return;
            destroyBuffer(buffer);
            capacity = std::max(required, capacity ? capacity * 2 : size_t{4096});
            buffer = createBuffer(capacity * elementSize, usage,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
        };
        grow(buffers.vertex, buffers.vertexCapacity, vertexCount,
             sizeof(UiMeshVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        grow(buffers.index, buffers.indexCapacity, indexCount,
             sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        auto* vertices = static_cast<UiMeshVertex*>(buffers.vertex.mapped);
        auto* indices = static_cast<uint32_t*>(buffers.index.mapped);
        size_t vertexOffset = 0, indexOffset = 0;
        for (const auto& batch : submittedUi) {
            std::copy(batch.vertices.begin(), batch.vertices.end(),
                      vertices + vertexOffset);
            if (vertexOffset > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("Vulkan UI vertex base exceeds uint32_t");
            const uint32_t vertexBase = static_cast<uint32_t>(vertexOffset);
            for (size_t index = 0; index < batch.indices.size(); ++index)
                indices[indexOffset + index] = rebaseVulkanIndex(
                    batch.indices[index], vertexBase);
            vertexOffset += batch.vertices.size();
            indexOffset += batch.indices.size();
        }
        require(vmaFlushAllocation(allocator,buffers.vertex.allocation,0,
                    vertexCount*sizeof(UiMeshVertex)),"vmaFlushAllocation");
        require(vmaFlushAllocation(allocator,buffers.index.allocation,0,
                    indexCount*sizeof(uint32_t)),"vmaFlushAllocation");
    }

    void prepareBufferUploads() {
        preparedBufferCopies.clear();
        preparedImageCopies.clear();
        if (pendingBufferUploads.empty() && pendingImageUploads.empty()) return;
        VkDeviceSize required = 0;
        for (const PendingBufferUpload& upload : pendingBufferUploads) {
            required = (required + 15u) & ~VkDeviceSize{15u};
            required += upload.bytes.size();
        }
        for (const PendingImageUpload& upload : pendingImageUploads) {
            required = (required + 15u) & ~VkDeviceSize{15u};
            required += upload.bytes.size();
        }
        UploadFrameBuffer& frame = uploadBuffers[currentFrame];
        if (frame.capacity < required) {
            destroyBuffer(frame.staging);
            frame.capacity = std::max(required,
                frame.capacity ? frame.capacity * 2 : VkDeviceSize{4u * 1024u * 1024u});
            frame.staging = createBuffer(frame.capacity,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            LOG_INFO("Vulkan upload staging capacity: " << frame.capacity << " bytes");
        }
        VkDeviceSize offset = 0;
        for (const PendingBufferUpload& upload : pendingBufferUploads) {
            offset = (offset + 15u) & ~VkDeviceSize{15u};
            std::memcpy(static_cast<uint8_t*>(frame.staging.mapped) + offset,
                        upload.bytes.data(), upload.bytes.size());
            preparedBufferCopies.push_back({upload.destination, offset,
                upload.destinationOffset,
                static_cast<VkDeviceSize>(upload.bytes.size())});
            offset += upload.bytes.size();
        }
        for (const PendingImageUpload& upload : pendingImageUploads) {
            offset = (offset + 15u) & ~VkDeviceSize{15u};
            std::memcpy(static_cast<uint8_t*>(frame.staging.mapped) + offset,
                        upload.bytes.data(), upload.bytes.size());
            PreparedImageCopy prepared;
            prepared.destination = upload.destination;
            prepared.mipLevels = upload.mipLevels;
            prepared.regions = upload.regions;
            for (VkBufferImageCopy& region : prepared.regions)
                region.bufferOffset += offset;
            preparedImageCopies.push_back(std::move(prepared));
            offset += upload.bytes.size();
        }
        require(vmaFlushAllocation(allocator, frame.staging.allocation, 0, offset),
                "vmaFlushAllocation");
        performance.uploadBytes = static_cast<uint64_t>(offset);
        pendingBufferUploads.clear();
        pendingImageUploads.clear();
    }

    void prepareParticleBuffer() {
        if (submittedParticles.empty()) return;
        ParticleFrameBuffer& frame=particleBuffers[currentFrame];
        if(frame.capacity<submittedParticles.size()){
            destroyBuffer(frame.instance);
            const size_t doubled=frame.capacity?frame.capacity*2:size_t{256};
            frame.capacity=std::min(ParticleSystem::MAX_PARTICLES,
                std::max(submittedParticles.size(),doubled));
            frame.instance=createBuffer(
                frame.capacity*sizeof(ParticleRenderData),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }
        const VkDeviceSize bytes=
            submittedParticles.size()*sizeof(ParticleRenderData);
        std::memcpy(frame.instance.mapped,submittedParticles.data(),bytes);
        require(vmaFlushAllocation(allocator,frame.instance.allocation,0,bytes),
                "vmaFlushAllocation");
    }

    template<typename Callback>
    void forEachModelPrimitive(const std::vector<ModelPassSubmission>& submissions,
                               bool blended, Callback&& callback) {
        for (const ModelPassSubmission& submission : submissions) {
            if (submission.draw.model == 0 || submission.draw.model > models.size()) continue;
            ModelResource& resource = models[submission.draw.model - 1];
            for (ModelPrimitive& primitive : resource.primitives) {
                const model::Material fallback;
                const model::Material& material = primitive.material >= 0 &&
                    static_cast<size_t>(primitive.material) < resource.asset->materials.size()
                    ? resource.asset->materials[static_cast<size_t>(primitive.material)]
                    : fallback;
                if ((model::modelPass(material.alphaMode) == model::ModelPass::Blend) != blended)
                    continue;
                callback(submission, resource, primitive, material);
            }
        }
    }

    void prepareModelBuffer() {
        // Opaque draws have no ordering requirement. Group equal model
        // resources so their primitive buffers and texture descriptors remain
        // hot while recording; blended submissions retain strict depth order.
        std::stable_sort(submittedModelOpaque.begin(), submittedModelOpaque.end(),
            [](const ModelPassSubmission& a, const ModelPassSubmission& b) {
                return a.draw.model < b.draw.model;
            });
        size_t count = 0;
        forEachModelPrimitive(submittedModelOpaque, false,
            [&](const auto&, auto&, auto&, const auto&) { ++count; });
        forEachModelPrimitive(submittedModelBlend, true,
            [&](const auto&, auto&, auto&, const auto&) { ++count; });
        if (count == 0) return;
        ModelFrameBuffer& frame = modelBuffers[currentFrame];
        if (frame.capacity < count) {
            if (frame.descriptorSet) {
                require(vkFreeDescriptorSets(device, descriptors.descriptorPool, 1,
                    &frame.descriptorSet), "vkFreeDescriptorSets");
                frame.descriptorSet = VK_NULL_HANDLE;
            }
            destroyBuffer(frame.uniform);
            frame.capacity = std::max(count, frame.capacity ? frame.capacity * 2 : size_t{64});
            frame.uniform = createBuffer(modelUniformStride * frame.capacity,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptors.descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &descriptors.modelUniformDescriptorSetLayout;
            require(vkAllocateDescriptorSets(device, &allocate, &frame.descriptorSet),
                    "vkAllocateDescriptorSets");
            const VkDescriptorBufferInfo bufferInfo{
                frame.uniform.handle, 0, sizeof(ModelUniforms)};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        size_t index = 0;
        auto writeUniform = [&](const ModelPassSubmission& submission,
            ModelResource& resource, ModelPrimitive& primitive,
            const model::Material& material) {
            ModelUniforms uniforms;
            uniforms.joints.fill(glm::mat4(1));
            uniforms.viewProjection = clipSpaceCorrection(GraphicsApi::Vulkan) *
                                      submission.viewProjection;
            uniforms.model = submission.draw.transform;
            uniforms.node = glm::mat4(1);
            if (submission.draw.instance && primitive.node >= 0 &&
                static_cast<size_t>(primitive.node) <
                    submission.draw.instance->pose.global.size() && primitive.skin < 0)
                uniforms.node = submission.draw.instance->pose.global[
                    static_cast<size_t>(primitive.node)];
            if (submission.draw.instance && primitive.skin >= 0 &&
                static_cast<size_t>(primitive.skin) <
                    submission.draw.instance->jointPalettes.size()) {
                const auto& palette = submission.draw.instance->jointPalettes[
                    static_cast<size_t>(primitive.skin)];
                std::copy_n(palette.begin(), std::min(palette.size(), model::MAX_JOINTS),
                            uniforms.joints.begin());
            }
            const bool textured = material.image >= 0 &&
                static_cast<size_t>(material.image) < resource.textureMaterials.size();
            uniforms.baseColor = material.baseColor * submission.draw.tint;
            uniforms.params = {material.alphaMode == model::AlphaMode::Mask
                ? material.alphaCutoff : 0.0f, textured ? 1.0f : 0.0f,
                0.0f, 0.0f};
            uniforms.cameraFogStart = glm::vec4(
                submission.cameraPosition, submission.fogStart);
            uniforms.fogColorEnd = glm::vec4(
                submission.environment.fogColor, submission.fogEnd);
            uniforms.lightDirection = glm::vec4(
                submission.environment.lightDirection, 0.0f);
            uniforms.directColor = glm::vec4(
                submission.environment.directColor * submission.environment.directIntensity,
                0.0f);
            uniforms.ambientColor = glm::vec4(
                submission.environment.ambientColor * submission.environment.ambientIntensity,
                0.0f);
            std::memcpy(static_cast<uint8_t*>(frame.uniform.mapped) +
                        index * modelUniformStride, &uniforms, sizeof(uniforms));
            ++index;
        };
        forEachModelPrimitive(submittedModelOpaque, false, writeUniform);
        forEachModelPrimitive(submittedModelBlend, true, writeUniform);
        require(vmaFlushAllocation(allocator, frame.uniform.allocation, 0,
                                   count * modelUniformStride),
                "vmaFlushAllocation");
    }

    void prepareSkyBuffer() {
        if (!skyQueued) return;
        SkyFrameBuffer& frame = skyBuffers[currentFrame];
        std::memcpy(frame.uniform.mapped, &submittedSky, sizeof(submittedSky));
        require(vmaFlushAllocation(allocator, frame.uniform.allocation, 0,
                                   sizeof(submittedSky)),
                "vmaFlushAllocation");
    }

    void prepareChunkBuffer() {
        if (submittedDraws.empty()) return;
        ChunkFrameBuffer& frame = chunkBuffers[currentFrame];
        std::memcpy(frame.uniform.mapped, &submittedChunkEnvironment,
                    sizeof(submittedChunkEnvironment));
        require(vmaFlushAllocation(allocator, frame.uniform.allocation, 0,
                                   sizeof(submittedChunkEnvironment)),
                "vmaFlushAllocation");
    }

    void prepareCloudBuffer() {
        if (!cloudsQueued || cloudInstances.empty()) return;
        CloudFrameBuffer& frame = cloudBuffers[currentFrame];
        if (frame.capacity < cloudInstances.size()) {
            destroyBuffer(frame.instance);
            frame.capacity = std::min(MAX_CLOUD_INSTANCES,
                std::max(cloudInstances.size(), frame.capacity
                    ? frame.capacity * 2 : size_t{256}));
            frame.instance = createBuffer(
                frame.capacity * sizeof(CloudInstance),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            frame.revision = 0;
        }
        if (frame.revision == cloudRevision) return;
        const VkDeviceSize bytes = cloudInstances.size() * sizeof(CloudInstance);
        std::memcpy(frame.instance.mapped, cloudInstances.data(), bytes);
        require(vmaFlushAllocation(allocator, frame.instance.allocation, 0, bytes),
                "vmaFlushAllocation");
        frame.revision = cloudRevision;
    }

    void recordCommandBuffer(uint32_t imageIndex) {
        VkCommandBuffer command = swapchain.commandBuffers[imageIndex];
        require(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
        recordUploads(command);
        recordShadowPass(command);
        recordScenePass(command, imageIndex);
        // The post and UI draws share one present render pass; the pass
        // bracket spans recordPostPass and recordUiPass.
        recordPostPass(command, imageIndex);
        recordUiPass(command);
        require(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    }

    void recordUploads(VkCommandBuffer command) {
        if (!preparedBufferCopies.empty() || !preparedImageCopies.empty()) {
            const VkBuffer staging = uploadBuffers[currentFrame].staging.handle;
            std::vector<VkImageMemoryBarrier> imageBarriers;
            imageBarriers.reserve(preparedImageCopies.size());
            for (const PreparedImageCopy& copy : preparedImageCopies) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = copy.destination;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = copy.mipLevels;
                barrier.subresourceRange.layerCount = 1;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageBarriers.push_back(barrier);
            }
            if (!imageBarriers.empty())
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                    static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
            for (const PreparedBufferCopy& copy : preparedBufferCopies) {
                const VkBufferCopy region{copy.sourceOffset,
                                          copy.destinationOffset, copy.size};
                vkCmdCopyBuffer(command, staging, copy.destination, 1, &region);
            }
            for (const PreparedImageCopy& copy : preparedImageCopies)
                vkCmdCopyBufferToImage(command, staging, copy.destination,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    static_cast<uint32_t>(copy.regions.size()), copy.regions.data());
            if (!preparedBufferCopies.empty()) {
                VkMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                                        VK_ACCESS_INDEX_READ_BIT |
                                        VK_ACCESS_UNIFORM_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 1, &barrier, 0, nullptr, 0, nullptr);
            }
            if (!imageBarriers.empty()) {
                for (VkImageMemoryBarrier& barrier : imageBarriers) {
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                }
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                    0, nullptr, static_cast<uint32_t>(imageBarriers.size()),
                    imageBarriers.data());
            }
        }
    }

    void recordShadowPass(VkCommandBuffer command) {
        if (shadowUpdateQueued && shadowCascades.count > 0 &&
            !submittedShadowChunks.empty() && shadowAtlasSet) {
            const ShadowConfig config=shadowConfig(shadow.shadowQuality);
            const int columns=config.cascadeCount==1?1:2;
            const int rows=(config.cascadeCount+columns-1)/columns;
            VkClearValue shadowClear{};shadowClear.depthStencil={1.0f,0};
            VkRenderPassBeginInfo shadowPass{};shadowPass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            shadowPass.renderPass=shadow.shadowRenderPass;shadowPass.framebuffer=shadow.shadowFramebuffer;
            shadowPass.renderArea.extent={static_cast<uint32_t>(config.resolution*columns),
                                          static_cast<uint32_t>(config.resolution*rows)};
            shadowPass.clearValueCount=1;shadowPass.pClearValues=&shadowClear;
            vkCmdBeginRenderPass(command,&shadowPass,VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,shadow.shadowPipeline);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                shadow.shadowPipelineLayout,0,1,&shadowAtlasSet,0,nullptr);
            const VkDeviceSize zero=0;
            for(int cascade=0;cascade<shadowCascades.count;++cascade){
                const VkViewport shadowViewport{static_cast<float>((cascade%columns)*config.resolution),
                    static_cast<float>((cascade/columns)*config.resolution),
                    static_cast<float>(config.resolution),static_cast<float>(config.resolution),0.0f,1.0f};
                const VkRect2D shadowScissor{{(cascade%columns)*config.resolution,
                                               (cascade/columns)*config.resolution},
                    {static_cast<uint32_t>(config.resolution),static_cast<uint32_t>(config.resolution)}};
                vkCmdSetViewport(command,0,1,&shadowViewport);vkCmdSetScissor(command,0,1,&shadowScissor);
                for(const ShadowChunkSubmission& submission:submittedShadowChunks){
                    if(!submission.mesh||!submission.mesh->gpuReady||
                       submission.mesh->shadowCasterIndexCount==0)continue;
                    if(!shadowIntersectsAabb(shadowCascades.lightViewProjection[cascade],
                        submission.aabbMin,submission.aabbMax,true))continue;
                    const auto meshIt=meshes.find(submission.mesh->renderHandle.value);
                    if(meshIt==meshes.end())continue;
                    const GpuMesh& mesh=meshIt->second;
                    const bool arena=mesh.arenaPage!=std::numeric_limits<uint32_t>::max();
                    const VkBuffer vertex=arena?chunkArenaPages[mesh.arenaPage].vertex.handle:mesh.vertex.handle;
                    const VkBuffer index=arena?chunkArenaPages[mesh.arenaPage].index.handle:mesh.index.handle;
                    vkCmdBindVertexBuffers(command,0,1,&vertex,&zero);
                    vkCmdBindIndexBuffer(command,index,0,VK_INDEX_TYPE_UINT32);
                    ShadowConstants constants;
                    constants.lightMvp=shadowCascades.lightViewProjection[cascade]*submission.model;
                    constants.atlasParams.x=static_cast<float>(submittedShadowAtlasTiles);
                    vkCmdPushConstants(command,shadow.shadowPipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                        0,sizeof(constants),&constants);
                    uint32_t first=static_cast<uint32_t>(submission.mesh->shadowCasterIndexOffset);
                    if(arena)first+=static_cast<uint32_t>(mesh.indexOffset/sizeof(uint32_t));
                    vkCmdDrawIndexed(command,static_cast<uint32_t>(submission.mesh->shadowCasterIndexCount),
                                     1,first,0,0);
                    ++performance.drawCalls;
                }
            }
            vkCmdEndRenderPass(command);
        }
    }

    void recordScenePass(VkCommandBuffer command, uint32_t imageIndex) {
        std::array<VkClearValue, 3> clear{};
        clear[0].color.float32[0] = submittedFrame.clearColor.r;
        clear[0].color.float32[1] = submittedFrame.clearColor.g;
        clear[0].color.float32[2] = submittedFrame.clearColor.b;
        clear[0].color.float32[3] = submittedFrame.clearColor.a;
        clear[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo render{};
        render.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render.renderPass = swapchain.renderPass;
        render.framebuffer = swapchain.framebuffers[imageIndex];
        render.renderArea.extent = swapchain.swapchainExtent;
        render.clearValueCount = swapchain.sampleCount != VK_SAMPLE_COUNT_1_BIT ? 3u : 2u;
        render.pClearValues = clear.data();
        vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
        VkPipeline boundPipeline = VK_NULL_HANDLE;
        const VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(swapchain.swapchainExtent.width),
            static_cast<float>(swapchain.swapchainExtent.height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, swapchain.swapchainExtent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        const VkDeviceSize offset = 0;
        if (skyQueued) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              swapchain.skyPipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                swapchain.skyPipelineLayout, 0, 1,
                &skyBuffers[currentFrame].descriptorSet, 0, nullptr);
            vkCmdDraw(command, 3, 1, 0, 0);
            ++performance.drawCalls;
            ++performance.pipelineBinds;
            ++performance.descriptorBinds;
        }
        if (cloudsQueued && !cloudInstances.empty()) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              swapchain.cloudPipeline);
            const VkBuffer buffer = cloudBuffers[currentFrame].instance.handle;
            vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
            const CloudUniforms constants{
                clipSpaceCorrection(GraphicsApi::Vulkan) * cloudViewProjection,
                glm::vec4(cloudOrigin, 0.0f), glm::vec4(cloudColor, 0.0f),
                glm::vec4(glm::normalize(submittedFrame.lightDirection),
                          postProcess.environment.rainIntensity)};
            vkCmdPushConstants(command, swapchain.cloudPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(constants), &constants);
            vkCmdDraw(command, 36,
                      static_cast<uint32_t>(cloudInstances.size()), 0, 0);
            ++performance.drawCalls;
            ++performance.pipelineBinds;
            ++performance.vertexBufferBinds;
        }
        VkDescriptorSet boundMaterialSet = VK_NULL_HANDLE;
        VkDescriptorSet boundChunkSet = VK_NULL_HANDLE;
        VkBuffer boundVertexBuffer = VK_NULL_HANDLE;
        VkBuffer boundIndexBuffer = VK_NULL_HANDLE;
        const auto drawBasicSubmissions = [&](const std::vector<DrawCommand>& draws) {
        for (const DrawCommand& draw : draws) {
            const auto meshIt = meshes.find(draw.mesh.value);
            const auto materialIt = materials.find(draw.material.value);
            if (meshIt == meshes.end() || materialIt == materials.end()) continue;
            const GpuMesh& mesh = meshIt->second;
            const GpuMaterial& material = materialIt->second;
            VkPipeline requestedPipeline = swapchain.pipeline;
            if (material.desc.pipeline == MaterialPipeline::ChunkTranslucent)
                requestedPipeline = swapchain.translucentPipeline;
            else if (material.desc.pipeline == MaterialPipeline::UnlitTextured) {
                if (material.desc.depthTest)
                    requestedPipeline = material.desc.backfaceCull
                        ? swapchain.basicPipeline : swapchain.basicNoCullPipeline;
                else
                    requestedPipeline = material.desc.backfaceCull
                        ? swapchain.basicNoDepthPipeline : swapchain.basicNoDepthNoCullPipeline;
            }
            if (requestedPipeline != boundPipeline) {
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  requestedPipeline);
                boundPipeline = requestedPipeline;
                ++performance.pipelineBinds;
            }
            const bool arenaMesh =
                mesh.arenaPage != std::numeric_limits<uint32_t>::max();
            const VkBuffer vertexBuffer = arenaMesh
                ? chunkArenaPages[mesh.arenaPage].vertex.handle : mesh.vertex.handle;
            const VkBuffer indexBuffer = arenaMesh
                ? chunkArenaPages[mesh.arenaPage].index.handle : mesh.index.handle;
            if (boundVertexBuffer != vertexBuffer) {
                vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer, &offset);
                boundVertexBuffer = vertexBuffer;
                ++performance.vertexBufferBinds;
            }
            if (boundIndexBuffer != indexBuffer) {
                vkCmdBindIndexBuffer(command, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                boundIndexBuffer = indexBuffer;
                ++performance.vertexBufferBinds;
            }
            if (boundMaterialSet != material.descriptorSet) {
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        swapchain.pipelineLayout, 0, 1,
                                        &material.descriptorSet, 0, nullptr);
                boundMaterialSet = material.descriptorSet;
                ++performance.descriptorBinds;
            }
            if (material.desc.pipeline != MaterialPipeline::UnlitTextured &&
                boundChunkSet != chunkBuffers[currentFrame].descriptorSet) {
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    swapchain.pipelineLayout, 1, 1, &chunkBuffers[currentFrame].descriptorSet,
                    0, nullptr);
                boundChunkSet = chunkBuffers[currentFrame].descriptorSet;
                ++performance.descriptorBinds;
            }
            const uint32_t count = draw.indexCount ? draw.indexCount : mesh.indexCount;
            const glm::mat4 drawViewProjection = draw.useCustomViewProjection
                ? draw.viewProjection : submittedFrame.projection * submittedFrame.view;
            const FrameUniforms constants{
                clipSpaceCorrection(GraphicsApi::Vulkan) * drawViewProjection * draw.model,
                {static_cast<float>(material.desc.atlasTilesPerSide),
                 material.desc.smoothLighting ? 1.0f : 0.0f,
                 material.desc.alphaCutoff, 0.0f},
                glm::vec4(glm::vec3(draw.model[3]), 0.0f), draw.tint};
            vkCmdPushConstants(command, swapchain.pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(constants), &constants);
            const uint32_t firstIndex = arenaMesh
                ? static_cast<uint32_t>(mesh.indexOffset / sizeof(uint32_t)) +
                    draw.firstIndex
                : draw.firstIndex;
            vkCmdDrawIndexed(command, count, 1, firstIndex, 0, 0);
            ++performance.drawCalls;
        }
        };
        drawBasicSubmissions(submittedDraws);
        uint32_t modelUniformIndex = 0;
        VkPipeline boundModelPipeline = VK_NULL_HANDLE;
        VkDescriptorSet boundModelMaterial = VK_NULL_HANDLE;
        VkBuffer boundModelVertex = VK_NULL_HANDLE;
        VkBuffer boundModelIndex = VK_NULL_HANDLE;
        const auto drawModels = [&](std::vector<ModelPassSubmission>& submissions,
                                    bool blended) {
            forEachModelPrimitive(submissions, blended,
                [&](const ModelPassSubmission&, ModelResource& resource,
                    ModelPrimitive& primitive, const model::Material& material) {
                    const VkPipeline selected = blended
                        ? (material.doubleSided ? swapchain.modelBlendDoubleSidedPipeline
                                                : swapchain.modelBlendPipeline)
                        : (material.doubleSided ? swapchain.modelOpaqueDoubleSidedPipeline
                                                : swapchain.modelOpaquePipeline);
                    if (boundModelPipeline != selected) {
                        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          selected);
                        boundModelPipeline = selected;
                        ++performance.pipelineBinds;
                    }
                    if (boundModelVertex != primitive.vertex.handle) {
                        vkCmdBindVertexBuffers(command, 0, 1,
                                               &primitive.vertex.handle, &offset);
                        boundModelVertex = primitive.vertex.handle;
                        ++performance.vertexBufferBinds;
                    }
                    if (boundModelIndex != primitive.index.handle) {
                        vkCmdBindIndexBuffer(command, primitive.index.handle, 0,
                                             VK_INDEX_TYPE_UINT32);
                        boundModelIndex = primitive.index.handle;
                        ++performance.vertexBufferBinds;
                    }
                    RenderMaterialHandle textureMaterial = modelFallbackMaterial;
                    if (material.image >= 0 && static_cast<size_t>(material.image) <
                        resource.textureMaterials.size())
                        textureMaterial = resource.textureMaterials[
                            static_cast<size_t>(material.image)];
                    const auto descriptor = materials.find(textureMaterial.value);
                    if (descriptor == materials.end()) {
                        ++modelUniformIndex;
                        return;
                    }
                    if (boundModelMaterial != descriptor->second.descriptorSet) {
                        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            swapchain.modelPipelineLayout, 0, 1,
                            &descriptor->second.descriptorSet,
                            0, nullptr);
                        boundModelMaterial = descriptor->second.descriptorSet;
                        ++performance.descriptorBinds;
                    }
                    const VkDeviceSize byteOffset =
                        modelUniformIndex++ * modelUniformStride;
                    if (byteOffset > std::numeric_limits<uint32_t>::max())
                        throw std::runtime_error("Vulkan model uniform offset overflow");
                    const uint32_t dynamicOffset = static_cast<uint32_t>(byteOffset);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        swapchain.modelPipelineLayout, 1, 1,
                        &modelBuffers[currentFrame].descriptorSet,
                        1, &dynamicOffset);
                    ++performance.descriptorBinds;
                    vkCmdDrawIndexed(command, primitive.indexCount, 1, 0, 0, 0);
                    ++performance.drawCalls;
                });
        };
        drawModels(submittedModelOpaque, false);
        drawModels(submittedModelBlend, true);
        if(!submittedParticles.empty()){
            const auto material=materials.find(particleMaterial.value);
            if(material!=materials.end()){
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  swapchain.particlePipeline);
                const VkBuffer buffer=particleBuffers[currentFrame].instance.handle;
                vkCmdBindVertexBuffers(command,0,1,&buffer,&offset);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                    swapchain.particlePipelineLayout,0,1,&material->second.descriptorSet,
                    0,nullptr);
                ParticleUniforms constants;
                constants.viewProjection=clipSpaceCorrection(GraphicsApi::Vulkan)*
                    particleViewProjection;
                constants.cameraRightTime=glm::vec4(particleCameraRight,particleTime);
                constants.cameraUpIntensity=glm::vec4(particleCameraUp,
                                                       particleIntensity);
                constants.atlasParams.x=static_cast<float>(
                    material->second.desc.atlasTilesPerSide);
                constants.atlasParams.y=0.0f;
                vkCmdPushConstants(command,swapchain.particlePipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,sizeof(constants),&constants);
                vkCmdDraw(command,6,static_cast<uint32_t>(submittedParticles.size()),
                          0,0);
            }
        }
        if (wireQueued) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              swapchain.wirePipeline);
            const WireUniforms constants{wireModelViewProjection,
                {0.0f, 0.0f, 0.0f, 0.0f}};
            vkCmdPushConstants(command, swapchain.wirePipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(constants), &constants);
            vkCmdDraw(command, 24, 1, 0, 0);
        }
        if (!submittedViewModels.empty()) {
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            attachment.clearValue.depthStencil = {1.0f, 0};
            VkClearRect rect{};
            rect.rect.extent = swapchain.swapchainExtent;
            rect.layerCount = 1;
            vkCmdClearAttachments(command, 1, &attachment, 1, &rect);
            boundPipeline = VK_NULL_HANDLE;
            boundMaterialSet = VK_NULL_HANDLE;
            boundChunkSet = VK_NULL_HANDLE;
            boundVertexBuffer = VK_NULL_HANDLE;
            boundIndexBuffer = VK_NULL_HANDLE;
            drawBasicSubmissions(submittedViewModels);
        }
        vkCmdEndRenderPass(command);
    }

    void recordPostPass(VkCommandBuffer command, uint32_t imageIndex) {
        VkClearValue presentClear{};
        presentClear.color.float32[3] = 1.0f;
        VkRenderPassBeginInfo present{};
        present.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        present.renderPass = swapchain.presentRenderPass;
        present.framebuffer = swapchain.presentFramebuffers[imageIndex];
        present.renderArea.extent = swapchain.swapchainExtent;
        present.clearValueCount = 1;
        present.pClearValues = &presentClear;
        vkCmdBeginRenderPass(command, &present, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(swapchain.swapchainExtent.width),
            static_cast<float>(swapchain.swapchainExtent.height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, swapchain.swapchainExtent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.postPipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            swapchain.postPipelineLayout, 0, 1, &swapchain.postDescriptorSets[imageIndex], 0, nullptr);
        const VisualQualityConfig visual = visualQualityConfig(visualQuality);
        PostConstants postConstants;
        postConstants.exposureBloom = {
            std::clamp(postProcess.exposure, 0.75f, 1.65f),
            visual.bloomLevels > 0 ? 0.07f + visual.bloomLevels * 0.012f : 0.0f,
            visual.bloomLevels > 0 ? 1.15f + visual.bloomLevels * 0.18f : 1.0f,
            visual.bloomLevels <= 0 ? 0.0f :
                visual.bloomLevels <= 3 ? 4.0f :
                visual.bloomLevels <= 5 ? 8.0f : 12.0f};
        postConstants.effects = {
            std::clamp(postProcess.underwater, 0.0f, 1.0f),
            std::clamp(postProcess.hurt, 0.0f, 1.0f),
            swapchain.framebufferSrgb ? 0.0f : 1.0f,
            static_cast<float>(static_cast<int>(visualQuality))};
        postConstants.texelTime = {
            1.0f / std::max(1u, swapchain.swapchainExtent.width),
            1.0f / std::max(1u, swapchain.swapchainExtent.height),
            static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now())), 0.0f};
        postConstants.environment = {
            postProcess.environment.rainIntensity,
            postProcess.environment.thunderIntensity,
            postProcess.environment.lightningFlash,
            postProcess.environment.daylight};
        vkCmdPushConstants(command, swapchain.postPipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(postConstants), &postConstants);
        vkCmdDraw(command, 3, 1, 0, 0);
        ++performance.drawCalls;
        ++performance.pipelineBinds;
        ++performance.descriptorBinds;
    }

    void recordUiPass(VkCommandBuffer command) {
        if (!submittedUi.empty()) {
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,swapchain.uiPipeline);
            const UiFrameBuffers& buffers=uiBuffers[currentFrame];
            const VkDeviceSize uiOffset=0;
            vkCmdBindVertexBuffers(command,0,1,&buffers.vertex.handle,&uiOffset);
            vkCmdBindIndexBuffer(command,buffers.index.handle,0,VK_INDEX_TYPE_UINT32);
            uint32_t firstIndex=0;
            for(const UiSubmission& batch:submittedUi){
                const auto material=materials.find(batch.material.value);
                if(material==materials.end())continue;
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                    swapchain.pipelineLayout,0,1,&material->second.descriptorSet,0,nullptr);
                const UiConstants constants{batch.projection,
                    {swapchain.framebufferSrgb ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f}};
                vkCmdPushConstants(command,swapchain.pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,sizeof(constants),&constants);
                vkCmdDrawIndexed(command,static_cast<uint32_t>(batch.indices.size()),
                    1,firstIndex,0,0);
                firstIndex+=static_cast<uint32_t>(batch.indices.size());
            }
        }
        vkCmdEndRenderPass(command);
    }

    void drawFrame() {
        if (window.isMinimized() || window.width() <= 0 || window.height() <= 0)
            return;
        if (presentationSuspended) resumePresentation();
        if (presentationSuspended || !swapchain.handle) return;
        if (swapchainDirty) recreateSwapchain();
        performance = {};
        RuntimeClock clock;
        auto mark = clock.now();
        require(vkWaitForFences(device, 1, &frameSync.fences[currentFrame], VK_TRUE,
                                UINT64_MAX), "vkWaitForFences");
        performance.cpuWaitMs += RuntimeClock::seconds(
            RuntimeClock::elapsed(mark, clock.now())) * 1000.0;
        for (GpuMesh& mesh : retiredMeshes[currentFrame]) {
            destroyGpuMesh(mesh);
        }
        retiredMeshes[currentFrame].clear();
        uint32_t imageIndex = 0;
        const VkResult acquire = vkAcquireNextImageKHR(
            device, swapchain.handle, UINT64_MAX, frameSync.imageAvailable[currentFrame],
            VK_NULL_HANDLE, &imageIndex);
        if (acquire == VK_ERROR_SURFACE_LOST_KHR) {
            suspendPresentation();
            return;
        }
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
            require(acquire, "vkAcquireNextImageKHR");
        mark = clock.now();
        if (swapchain.imagesInFlight[imageIndex] != VK_NULL_HANDLE)
            require(vkWaitForFences(device, 1, &swapchain.imagesInFlight[imageIndex], VK_TRUE,
                                    UINT64_MAX), "vkWaitForFences");
        performance.cpuWaitMs += RuntimeClock::seconds(
            RuntimeClock::elapsed(mark, clock.now())) * 1000.0;
        swapchain.imagesInFlight[imageIndex] = frameSync.fences[currentFrame];
        mark = clock.now();
        prepareSkyBuffer();
        prepareChunkBuffer();
        prepareCloudBuffer();
        prepareParticleBuffer();
        prepareModelBuffer();
        prepareUiBuffers();
        prepareBufferUploads();
        performance.cpuPrepareMs = RuntimeClock::seconds(
            RuntimeClock::elapsed(mark, clock.now())) * 1000.0;
        mark = clock.now();
        recordCommandBuffer(imageIndex);
        performance.cpuRecordMs = RuntimeClock::seconds(
            RuntimeClock::elapsed(mark, clock.now())) * 1000.0;
        mark = clock.now();
        require(vkResetFences(device, 1, &frameSync.fences[currentFrame]), "vkResetFences");
        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frameSync.imageAvailable[currentFrame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &swapchain.commandBuffers[imageIndex];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &frameSync.renderFinished[currentFrame];
        require(vkQueueSubmit(graphicsQueue, 1, &submit, frameSync.fences[currentFrame]),
                "vkQueueSubmit");
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &frameSync.renderFinished[currentFrame];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain.handle;
        present.pImageIndices = &imageIndex;
        const VkResult result = vkQueuePresentKHR(presentQueue, &present);
        performance.cpuSubmitMs = RuntimeClock::seconds(
            RuntimeClock::elapsed(mark, clock.now())) * 1000.0;
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            suspendPresentation();
        } else if (result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR ||
            acquire == VK_SUBOPTIMAL_KHR) {
            swapchainDirty = true;
        } else if (result != VK_SUCCESS) {
            require(result, "vkQueuePresentKHR");
        }
        if (!firstFrameLogged) {
            LOG_INFO("Vulkan first frame presented");
            firstFrameLogged = true;
        }
        currentFrame = (currentFrame + 1) % FRAMES_IN_FLIGHT;
    }

    void recreateSwapchain() {
        if (presentationSuspended || !surface) return;
        if (window.isMinimized() || window.width() <= 0 || window.height() <= 0)
            return;
        require(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        // Destroy the previous bundle before creating a replacement: some
        // drivers reject a second swapchain on the same surface while the
        // old one is still alive.
        swapchain = vkp::VulkanSwapchainBundle{};
        createSwapchain();
    }

    void suspendPresentation() {
        if (presentationSuspended) return;
        if (device) require(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        swapchain = vkp::VulkanSwapchainBundle{};
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
        presentationSuspended = true;
        swapchainDirty = false;
        LOG_INFO("Vulkan presentation suspended");
    }

    void resumePresentation() {
        if (!presentationSuspended) return;
        if (window.isMinimized() || window.width() <= 0 || window.height() <= 0)
            return;
        VkSurfaceKHR replacement = reinterpret_cast<VkSurfaceKHR>(
            window.createVulkanSurface(reinterpret_cast<void*>(instance)));
        VkBool32 supported = VK_FALSE;
        const VkResult supportResult = vkGetPhysicalDeviceSurfaceSupportKHR(
            physicalDevice, presentFamily, replacement, &supported);
        if (supportResult != VK_SUCCESS || !supported) {
            vkDestroySurfaceKHR(instance, replacement, nullptr);
            require(supportResult, "vkGetPhysicalDeviceSurfaceSupportKHR");
            throw std::runtime_error(
                "Vulkan presentation queue does not support the restored surface");
        }
        surface = replacement;
        try {
            createSwapchain();
            presentationSuspended = false;
            LOG_INFO("Vulkan presentation resumed");
        } catch (...) {
            if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
            throw;
        }
    }

    void cleanup() {
        if (device) vkDeviceWaitIdle(device);
        if (allocator) {
            for (auto& [id, mesh] : meshes) {
                (void)id;
                destroyGpuMesh(mesh);
            }
            meshes.clear();
            for (auto& retired : retiredMeshes) {
                for (GpuMesh& mesh : retired) {
                    destroyGpuMesh(mesh);
                }
                retired.clear();
            }
            for (ChunkArenaPage& page : chunkArenaPages) {
                destroyBuffer(page.index);
                destroyBuffer(page.vertex);
            }
            chunkArenaPages.clear();
            for (auto& [id, texture] : textures) {
                (void)id;
                if (texture.sampler) vkDestroySampler(device, texture.sampler, nullptr);
                if (texture.view) vkDestroyImageView(device, texture.view, nullptr);
                if (texture.image)
                    vmaDestroyImage(allocator, texture.image, texture.allocation);
            }
            textures.clear();
            pendingImageUploads.clear();
            for (auto& buffers : uiBuffers) {
                destroyBuffer(buffers.index);
                destroyBuffer(buffers.vertex);
            }
            for (auto& buffer : particleBuffers)
                destroyBuffer(buffer.instance);
            for (auto& buffer : skyBuffers)
                destroyBuffer(buffer.uniform);
            for (auto& buffer : chunkBuffers)
                destroyBuffer(buffer.uniform);
            for (auto& buffer : cloudBuffers)
                destroyBuffer(buffer.instance);
            for (auto& buffer : modelBuffers)
                destroyBuffer(buffer.uniform);
            for (auto& buffer : uploadBuffers)
                destroyBuffer(buffer.staging);
        }
        materials.clear();
    }

};
