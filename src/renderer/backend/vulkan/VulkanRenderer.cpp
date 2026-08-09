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
#include "Config.h"
#include "world/Block.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <stb_image.h>

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

void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed with Vulkan result " +
                                 std::to_string(result));
}

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    QueueFamilies result;
    for (uint32_t i = 0; i < count; ++i) {
        if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            result.graphics = i;
        VkBool32 present = VK_FALSE;
        require(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (present == VK_TRUE) result.present = i;
        if (result.complete()) break;
    }
    return result;
}

bool supportsSwapchain(VkPhysicalDevice device) {
    uint32_t count = 0;
    require(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
            "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    require(vkEnumerateDeviceExtensionProperties(
                device, nullptr, &count, extensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    return std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
        return std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    });
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    constexpr std::array<VkFormat, 4> preference{
        VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    for (VkFormat candidate : preference) {
        const auto found = std::find_if(formats.begin(), formats.end(),
            [&](const auto& format) {
                return format.format == candidate &&
                       format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
        if (found != formats.end()) return *found;
    }
    return formats.front();
}

bool isSrgbFormat(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB ||
           format == VK_FORMAT_R8G8B8A8_SRGB;
}

TextureData loadRgbaTexture(const std::filesystem::path& path) {
    const std::vector<uint8_t> encoded = AssetStore::readPath(path);
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(encoded.data(),
        static_cast<int>(encoded.size()), &width, &height, &channels, STBI_rgb_alpha);
    if (!decoded || width <= 0 || height <= 0) {
        const std::string reason = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        stbi_image_free(decoded);
        throw std::runtime_error("Could not decode texture " + path.string() + ": " + reason);
    }
    TextureData texture;
    texture.width = static_cast<uint32_t>(width);
    texture.height = static_cast<uint32_t>(height);
    texture.pixels.assign(decoded, decoded + static_cast<size_t>(width) * height * 4u);
    stbi_image_free(decoded);
    return texture;
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    return std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()
        ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (const auto choice : choices)
        if ((supported & choice) != 0) return choice;
    throw std::runtime_error("Vulkan surface exposes no composite alpha mode");
}

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
};

struct WireUniforms {
    glm::mat4 modelViewProjection{1.0f};
    glm::vec4 options{0.0f};
};

struct UiConstants {
    glm::mat4 projection{1.0f};
    glm::vec4 options{0.0f};
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
static_assert(sizeof(CloudUniforms) == 96);
static_assert(sizeof(FrameUniforms) == 112);
static_assert(offsetof(FrameUniforms, chunkOrigin) == 80);
static_assert(sizeof(ChunkEnvironmentUniforms) == 96);
static_assert(offsetof(ChunkEnvironmentUniforms, materialParams) == 80);
static_assert(sizeof(WireUniforms) == 80);
static_assert(sizeof(UiConstants) == 80);
static_assert(sizeof(ModelUniforms) == 4416);

static_assert(sizeof(ParticleRenderData) == 32);
static_assert(offsetof(ParticleRenderData, position) == 0);
static_assert(offsetof(ParticleRenderData, kind) == 12);
static_assert(offsetof(ParticleRenderData, phase) == 16);
static_assert(offsetof(ParticleRenderData, texture) == 20);
static_assert(offsetof(ParticleRenderData, size) == 24);
static_assert(offsetof(ParticleRenderData, rotation) == 28);
static_assert(sizeof(CloudInstance) == 24);
static_assert(offsetof(CloudInstance, width) == 12);
static_assert(offsetof(CloudInstance, height) == 20);

} // namespace

struct VulkanRenderer::Impl {
    static constexpr size_t FRAMES_IN_FLIGHT = 2;

    struct Buffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    struct GpuMesh {
        Buffer vertex;
        Buffer index;
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
        std::vector<uint8_t> bytes;
    };
    struct PreparedBufferCopy {
        VkBuffer destination = VK_NULL_HANDLE;
        VkDeviceSize sourceOffset = 0;
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

    explicit Impl(Window& owner, std::filesystem::path root)
        : window(owner), assetRoot(std::move(root)) {
        try {
            createInstance();
            surface = reinterpret_cast<VkSurfaceKHR>(
                window.createVulkanSurface(reinterpret_cast<void*>(instance)));
            pickPhysicalDevice();
            createDevice();
            createAllocator();
            createCommandPool();
            createDescriptorLayout();
            createDescriptorPool();
            createSkyResources();
            createSyncObjects();
            createSwapchain();
            LOG_INFO("VulkanRenderer initialized with VMA");
        } catch (...) {
            cleanup();
            throw;
        }
    }

    ~Impl() { cleanup(); }

    Window& window;
    std::filesystem::path assetRoot;
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
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    bool framebufferSrgb = true;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    std::vector<VkImage> colorImages;
    std::vector<VmaAllocation> colorAllocations;
    std::vector<VkImageView> colorImageViews;
    std::vector<VkImage> depthImages;
    std::vector<VmaAllocation> depthAllocations;
    std::vector<VkImageView> depthImageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout chunkDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline translucentPipeline = VK_NULL_HANDLE;
    VkPipeline basicPipeline = VK_NULL_HANDLE;
    VkPipeline basicNoCullPipeline = VK_NULL_HANDLE;
    VkPipeline basicNoDepthPipeline = VK_NULL_HANDLE;
    VkPipeline basicNoDepthNoCullPipeline = VK_NULL_HANDLE;
    VkPipeline uiPipeline = VK_NULL_HANDLE;
    VkPipeline particlePipeline = VK_NULL_HANDLE;
    VkPipelineLayout particlePipelineLayout = VK_NULL_HANDLE;
    VkPipeline skyPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyPipelineLayout = VK_NULL_HANDLE;
    VkPipeline cloudPipeline = VK_NULL_HANDLE;
    VkPipelineLayout cloudPipelineLayout = VK_NULL_HANDLE;
    VkPipeline wirePipeline = VK_NULL_HANDLE;
    VkPipelineLayout wirePipelineLayout = VK_NULL_HANDLE;
    VkPipeline modelOpaquePipeline = VK_NULL_HANDLE;
    VkPipeline modelOpaqueDoubleSidedPipeline = VK_NULL_HANDLE;
    VkPipeline modelBlendPipeline = VK_NULL_HANDLE;
    VkPipeline modelBlendDoubleSidedPipeline = VK_NULL_HANDLE;
    VkPipelineLayout modelPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout modelUniformDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, GpuMesh> meshes;
    std::unordered_map<uint32_t, GpuTexture> textures;
    std::unordered_map<uint32_t, GpuMaterial> materials;
    uint32_t nextMeshHandle = 1;
    uint32_t nextTextureHandle = 1;
    uint32_t nextMaterialHandle = 1;
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> imageAvailable{};
    std::array<VkSemaphore, FRAMES_IN_FLIGHT> renderFinished{};
    std::array<VkFence, FRAMES_IN_FLIGHT> frameFences{};
    std::vector<VkFence> imagesInFlight;
    size_t currentFrame = 0;
    bool swapchainDirty = false;
    bool firstFrameLogged = false;
    FrameData submittedFrame{};
    std::vector<DrawCommand> submittedDraws;
    std::vector<UiSubmission> submittedUi;
    std::array<UiFrameBuffers, FRAMES_IN_FLIGHT> uiBuffers{};
    std::array<ParticleFrameBuffer, FRAMES_IN_FLIGHT> particleBuffers{};
    std::array<SkyFrameBuffer, FRAMES_IN_FLIGHT> skyBuffers{};
    std::array<ChunkFrameBuffer, FRAMES_IN_FLIGHT> chunkBuffers{};
    std::array<CloudFrameBuffer, FRAMES_IN_FLIGHT> cloudBuffers{};
    std::array<ModelFrameBuffer, FRAMES_IN_FLIGHT> modelBuffers{};
    std::array<UploadFrameBuffer, FRAMES_IN_FLIGHT> uploadBuffers{};
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

    void createInstance() {
        const std::vector<std::string> extensionStorage =
            window.requiredVulkanInstanceExtensions();
        std::vector<const char*> extensions;
        extensions.reserve(extensionStorage.size());
        for (const std::string& extension : extensionStorage)
            extensions.push_back(extension.c_str());
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "MinecraftC Vulkan Basic Renderer";
        application.applicationVersion = VK_MAKE_VERSION(1, 1, 5);
        application.pEngineName = "MinecraftC";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
            sampleCount = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT :
                (counts & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT :
                VK_SAMPLE_COUNT_1_BIT;
            LOG_INFO("Vulkan device: " << properties.deviceName);
            LOG_INFO("Vulkan MSAA: " << static_cast<uint32_t>(sampleCount) << "x");
            return;
        }
        throw std::runtime_error("No Vulkan device supports graphics and presentation");
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
        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
        info.pQueueCreateInfos = queues.data();
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        require(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
    }

    void createAllocator() {
        VmaAllocatorCreateInfo info{};
        info.instance = instance;
        info.physicalDevice = physicalDevice;
        info.device = device;
        info.vulkanApiVersion = VK_API_VERSION_1_0;
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
            imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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
                 << data.height << " sRGB, " << data.mipLevels.size() + 1
                 << " mip levels)");
        return result;
    }

    void createDescriptorLayout() {
        std::array<VkDescriptorSetLayoutBinding, 1> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = bindings.size();
        info.pBindings = bindings.data();
        require(vkCreateDescriptorSetLayout(device, &info, nullptr, &descriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        VkDescriptorSetLayoutBinding skyBinding{};
        skyBinding.binding = 0;
        skyBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyBinding.descriptorCount = 1;
        skyBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        info.bindingCount = 1;
        info.pBindings = &skyBinding;
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &skyDescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        skyBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT;
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &chunkDescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        VkDescriptorSetLayoutBinding modelBinding{};
        modelBinding.binding = 0;
        modelBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        modelBinding.descriptorCount = 1;
        modelBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT;
        info.pBindings = &modelBinding;
        require(vkCreateDescriptorSetLayout(
                    device, &info, nullptr, &modelUniformDescriptorSetLayout),
                "vkCreateDescriptorSetLayout");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        const VkDeviceSize alignment = std::max<VkDeviceSize>(
            1, properties.limits.minUniformBufferOffsetAlignment);
        modelUniformStride = (sizeof(ModelUniforms) + alignment - 1) & ~(alignment - 1);
    }

    void createDescriptorPool() {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
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
        require(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
                "vkCreateDescriptorPool");
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
            allocate.descriptorPool = descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &skyDescriptorSetLayout;
            require(vkAllocateDescriptorSets(device, &allocate, &frame.descriptorSet),
                    "vkAllocateDescriptorSets");
            VkDescriptorBufferInfo bufferInfo{
                frame.uniform.handle, 0, sizeof(SkyUniforms)};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        for (ChunkFrameBuffer& frame : chunkBuffers) {
            frame.uniform = createBuffer(
                sizeof(ChunkEnvironmentUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &chunkDescriptorSetLayout;
            require(vkAllocateDescriptorSets(device, &allocate, &frame.descriptorSet),
                    "vkAllocateDescriptorSets");
            VkDescriptorBufferInfo bufferInfo{
                frame.uniform.handle, 0, sizeof(ChunkEnvironmentUniforms)};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    VkDescriptorSet createMaterialDescriptor(const GpuTexture& texture) {
        VkDescriptorSetAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool = descriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &descriptorSetLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        require(vkAllocateDescriptorSets(device, &allocate, &set),
                "vkAllocateDescriptorSets");
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = texture.sampler;
        imageInfo.imageView = texture.view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return set;
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            require(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable[i]),
                    "vkCreateSemaphore");
            require(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished[i]),
                    "vkCreateSemaphore");
            require(vkCreateFence(device, &fenceInfo, nullptr, &frameFences[i]),
                    "vkCreateFence");
        }
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return capabilities.currentExtent;
        return {
            std::clamp(static_cast<uint32_t>(std::max(1, window.width())),
                       capabilities.minImageExtent.width,
                       capabilities.maxImageExtent.width),
            std::clamp(static_cast<uint32_t>(std::max(1, window.height())),
                       capabilities.minImageExtent.height,
                       capabilities.maxImageExtent.height)};
    }

    VkFormat findDepthFormat() const {
        constexpr std::array<VkFormat, 3> candidates{
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT};
        for (VkFormat format : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
            if ((properties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
                return format;
        }
        throw std::runtime_error("No supported Vulkan depth format");
    }

    void createSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                    &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        uint32_t formatCount = 0;
        uint32_t modeCount = 0;
        require(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                    &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        require(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                    &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        require(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                    &modeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        std::vector<VkPresentModeKHR> modes(modeCount);
        require(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                    &modeCount, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (formats.empty() || modes.empty())
            throw std::runtime_error("Vulkan surface has no swapchain configuration");
        const VkSurfaceFormatKHR format = chooseSurfaceFormat(formats);
        const VkExtent2D extent = chooseExtent(capabilities);
        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0)
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const uint32_t queueFamilies[]{graphicsFamily, presentFamily};
        if (graphicsFamily != presentFamily) {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queueFamilies;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
        info.presentMode = choosePresentMode(modes);
        info.clipped = VK_TRUE;
        require(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain),
                "vkCreateSwapchainKHR");
        swapchainFormat = format.format;
        framebufferSrgb = isSrgbFormat(swapchainFormat);
        swapchainExtent = extent;
        require(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR");
        images.resize(imageCount);
        require(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()),
                "vkGetSwapchainImagesKHR");
        createImageViews();
        createColorResources();
        createDepthResources();
        createRenderPass();
        createPipeline();
        createFramebuffers();
        allocateCommandBuffers();
        imagesInFlight.assign(images.size(), VK_NULL_HANDLE);
        swapchainDirty = false;
        LOG_INFO("Vulkan swapchain: " << extent.width << "x" << extent.height
                 << ", " << imageCount << " images, format "
                 << static_cast<int>(swapchainFormat) << ", "
                 << (framebufferSrgb ? "hardware sRGB" : "shader gamma fallback"));
    }

    void createImageViews() {
        imageViews.resize(images.size());
        for (size_t i = 0; i < images.size(); ++i) {
            VkImageViewCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = images[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = swapchainFormat;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.layerCount = 1;
            require(vkCreateImageView(device, &info, nullptr, &imageViews[i]),
                    "vkCreateImageView");
        }
    }

    void createDepthResources() {
        depthFormat = findDepthFormat();
        depthImages.assign(images.size(), VK_NULL_HANDLE);
        depthAllocations.assign(images.size(), VK_NULL_HANDLE);
        depthImageViews.assign(images.size(), VK_NULL_HANDLE);
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = sampleCount;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        for (size_t i = 0; i < images.size(); ++i) {
            require(vmaCreateImage(allocator, &imageInfo, &allocationInfo,
                                   &depthImages[i], &depthAllocations[i], nullptr),
                    "vmaCreateImage");
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depthImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            require(vkCreateImageView(device, &viewInfo, nullptr, &depthImageViews[i]),
                    "vkCreateImageView");
        }
    }

    void createColorResources() {
        if (sampleCount == VK_SAMPLE_COUNT_1_BIT) return;
        colorImages.assign(images.size(), VK_NULL_HANDLE);
        colorAllocations.assign(images.size(), VK_NULL_HANDLE);
        colorImageViews.assign(images.size(), VK_NULL_HANDLE);
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = swapchainFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.samples = sampleCount;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        for (size_t i = 0; i < images.size(); ++i) {
            require(vmaCreateImage(allocator, &imageInfo, &allocationInfo,
                                   &colorImages[i], &colorAllocations[i], nullptr),
                    "vmaCreateImage");
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = colorImages[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = swapchainFormat;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            require(vkCreateImageView(device, &view, nullptr, &colorImageViews[i]),
                    "vkCreateImageView");
        }
    }

    void createRenderPass() {
        std::array<VkAttachmentDescription, 3> attachments{};
        const bool multisampled = sampleCount != VK_SAMPLE_COUNT_1_BIT;
        attachments[0].format = swapchainFormat;
        attachments[0].samples = sampleCount;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                               : VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = multisampled
            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].format = depthFormat;
        attachments[1].samples = sampleCount;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        attachments[2].format = swapchainFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depth;
        subpass.pResolveAttachments = multisampled ? &resolve : nullptr;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = dependency.srcStageMask;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = multisampled ? 3u : 2u;
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        require(vkCreateRenderPass(device, &info, nullptr, &renderPass),
                "vkCreateRenderPass");
    }

    VkShaderModule loadShader(const std::filesystem::path& path) const {
        const std::vector<uint8_t> bytes = AssetStore::readPath(path);
        if (bytes.size() < sizeof(uint32_t) || bytes.size() % sizeof(uint32_t) != 0)
            throw std::runtime_error("Invalid SPIR-V shader: " + path.string());
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = bytes.size();
        info.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
        VkShaderModule module = VK_NULL_HANDLE;
        require(vkCreateShaderModule(device, &info, nullptr, &module),
                "vkCreateShaderModule");
        return module;
    }

    void createPipeline() {
        const auto shaderRoot = assetRoot / "shaders" / "vulkan";
        VkShaderModule vertex = loadShader(shaderRoot / "chunk.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(shaderRoot / "chunk.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
            }};
            const VkVertexInputBindingDescription binding{0, sizeof(MeshVertex),
                                                          VK_VERTEX_INPUT_RATE_VERTEX};
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
            // Keep production Chunk geometry double-sided. Its CPU triangles
            // are outward CCW, but the complete gameplay projection/viewport
            // path does not yet preserve one reliable framebuffer winding for
            // every generated face; back-face culling drops exposed top faces.
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = sampleCount;
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
                descriptorSetLayout, chunkDescriptorSetLayout};
            layoutInfo.setLayoutCount = chunkLayouts.size();
            layoutInfo.pSetLayouts = chunkLayouts.data();
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(FrameUniforms);
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            require(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout),
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
            info.layout = pipelineLayout;
            info.renderPass = renderPass;
            info.subpass = 0;
            require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info,
                                              nullptr, &pipeline),
                    "vkCreateGraphicsPipelines");
            depth.depthWriteEnable = VK_FALSE;
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info,
                                              nullptr, &translucentPipeline),
                    "vkCreateGraphicsPipelines");
        } catch (...) {
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);

        vertex = loadShader(shaderRoot / "ui.vert.spv");
        fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(shaderRoot / "ui.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> uiStages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            const VkVertexInputBindingDescription uiBinding{
                0, sizeof(UiMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const std::array<VkVertexInputAttributeDescription, 3> uiAttributes{{
                {0,0,VK_FORMAT_R32G32_SFLOAT,offsetof(UiMeshVertex,position)},
                {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(UiMeshVertex,uv)},
                {2,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(UiMeshVertex,color)}}};
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
            uiViewport.viewportCount = 1; uiViewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo uiRaster{};
            uiRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            uiRaster.polygonMode = VK_POLYGON_MODE_FILL;
            uiRaster.cullMode = VK_CULL_MODE_NONE;
            uiRaster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            uiRaster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo uiMultisample{};
            uiMultisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            uiMultisample.rasterizationSamples = sampleCount;
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
            uiBlend.attachmentCount = 1; uiBlend.pAttachments = &uiBlendAttachment;
            constexpr std::array<VkDynamicState,2> uiDynamicStates{
                VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo uiDynamic{};
            uiDynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            uiDynamic.dynamicStateCount = uiDynamicStates.size();
            uiDynamic.pDynamicStates = uiDynamicStates.data();
            VkGraphicsPipelineCreateInfo uiInfo{};
            uiInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            uiInfo.stageCount = uiStages.size(); uiInfo.pStages = uiStages.data();
            uiInfo.pVertexInputState = &uiVertexInput;
            uiInfo.pInputAssemblyState = &uiAssembly;
            uiInfo.pViewportState = &uiViewport;
            uiInfo.pRasterizationState = &uiRaster;
            uiInfo.pMultisampleState = &uiMultisample;
            uiInfo.pDepthStencilState = &uiDepth;
            uiInfo.pColorBlendState = &uiBlend;
            uiInfo.pDynamicState = &uiDynamic;
            uiInfo.layout = pipelineLayout; uiInfo.renderPass = renderPass;
            require(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&uiInfo,
                                               nullptr,&uiPipeline),
                    "vkCreateGraphicsPipelines");
        } catch (...) {
            if(fragment)vkDestroyShaderModule(device,fragment,nullptr);
            vkDestroyShaderModule(device,vertex,nullptr);
            throw;
        }
        vkDestroyShaderModule(device,fragment,nullptr);
        vkDestroyShaderModule(device,vertex,nullptr);
        createParticlePipeline(shaderRoot);
        createSkyCloudPipelines(shaderRoot);
        createWirePipeline(shaderRoot);
        createModelPipelines(shaderRoot);
        createBasicPipelines(shaderRoot);
    }

    void createBasicPipelines(const std::filesystem::path& shaderRoot) {
        VkShaderModule vertex = loadShader(shaderRoot / "basic_cube.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(shaderRoot / "basic_cube.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            const VkVertexInputBindingDescription binding{
                0, sizeof(BasicMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const std::array<VkVertexInputAttributeDescription, 2> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BasicMeshVertex, position)},
                {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(BasicMeshVertex, uv)}}};
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
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState, 2> states{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
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
            info.layout = pipelineLayout;
            info.renderPass = renderPass;
            const auto create = [&](VkPipeline& output) {
                require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                    &info, nullptr, &output), "vkCreateGraphicsPipelines");
            };
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(basicPipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(basicNoCullPipeline);
            depth.depthTestEnable = VK_FALSE;
            depth.depthWriteEnable = VK_FALSE;
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(basicNoDepthPipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(basicNoDepthNoCullPipeline);
        } catch (...) {
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
    }

    void createModelPipelines(const std::filesystem::path& shaderRoot) {
        VkShaderModule vertex = loadShader(shaderRoot / "model.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(shaderRoot / "model.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            const VkVertexInputBindingDescription binding{
                0, sizeof(model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const std::array<VkVertexInputAttributeDescription, 5> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(model::Vertex, position)},
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(model::Vertex, normal)},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(model::Vertex, uv)},
                {3, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(model::Vertex, joints)},
                {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(model::Vertex, weights)}}};
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
            raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            constexpr std::array<VkDynamicState, 2> states{
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = states.size();
            dynamic.pDynamicStates = states.data();
            const std::array<VkDescriptorSetLayout, 2> layouts{
                descriptorSetLayout, modelUniformDescriptorSetLayout};
            VkPipelineLayoutCreateInfo layout{};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount = layouts.size();
            layout.pSetLayouts = layouts.data();
            require(vkCreatePipelineLayout(device, &layout, nullptr,
                                            &modelPipelineLayout),
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
            info.layout = modelPipelineLayout;
            info.renderPass = renderPass;
            const auto create = [&](VkPipeline& output) {
                require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                    &info, nullptr, &output), "vkCreateGraphicsPipelines");
            };
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(modelOpaquePipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(modelOpaqueDoubleSidedPipeline);
            depth.depthWriteEnable = VK_FALSE;
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            create(modelBlendPipeline);
            raster.cullMode = VK_CULL_MODE_NONE;
            create(modelBlendDoubleSidedPipeline);
        } catch (...) {
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
    }

    void createWirePipeline(const std::filesystem::path& shaderRoot) {
        VkShaderModule vertex = loadShader(shaderRoot / "wireframe.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(shaderRoot / "wireframe.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}}};
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
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
            multisample.rasterizationSamples = sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
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
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            range.size = sizeof(WireUniforms);
            VkPipelineLayoutCreateInfo layout{};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges = &range;
            require(vkCreatePipelineLayout(device, &layout, nullptr,
                                           &wirePipelineLayout),
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
            info.layout = wirePipelineLayout;
            info.renderPass = renderPass;
            require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info,
                                               nullptr, &wirePipeline),
                    "vkCreateGraphicsPipelines");
        } catch (...) {
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
    }

    void createParticlePipeline(const std::filesystem::path& shaderRoot) {
        VkShaderModule vertex = loadShader(shaderRoot / "weather.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = loadShader(shaderRoot / "weather.frag.spv");
            const std::array<VkPipelineShaderStageCreateInfo,2> stages{{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,
                 VK_SHADER_STAGE_VERTEX_BIT,vertex,"main",nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,
                 VK_SHADER_STAGE_FRAGMENT_BIT,fragment,"main",nullptr}}};
            const VkVertexInputBindingDescription binding{
                0,sizeof(ParticleRenderData),VK_VERTEX_INPUT_RATE_INSTANCE};
            const std::array<VkVertexInputAttributeDescription,2> attributes{{
                {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(ParticleRenderData,position)},
                {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,
                 offsetof(ParticleRenderData,phase)}}};
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount=1;
            vertexInput.pVertexBindingDescriptions=&binding;
            vertexInput.vertexAttributeDescriptionCount=attributes.size();
            vertexInput.pVertexAttributeDescriptions=attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount=1;viewport.scissorCount=1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode=VK_POLYGON_MODE_FILL;
            raster.cullMode=VK_CULL_MODE_NONE;
            raster.frontFace=VK_FRONT_FACE_CLOCKWISE;
            raster.lineWidth=1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples=sampleCount;
            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable=VK_TRUE;
            depth.depthWriteEnable=VK_FALSE;
            depth.depthCompareOp=VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.blendEnable=VK_TRUE;
            attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp=VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp=VK_BLEND_OP_ADD;
            attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|
                VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|
                VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount=1;blend.pAttachments=&attachment;
            constexpr std::array<VkDynamicState,2> dynamicStates{
                VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount=dynamicStates.size();
            dynamic.pDynamicStates=dynamicStates.data();
            VkPushConstantRange range{};
            range.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
            range.size=sizeof(ParticleUniforms);
            VkPipelineLayoutCreateInfo layout{};
            layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount=1;layout.pSetLayouts=&descriptorSetLayout;
            layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
            require(vkCreatePipelineLayout(device,&layout,nullptr,
                                             &particlePipelineLayout),
                    "vkCreatePipelineLayout");
            VkGraphicsPipelineCreateInfo info{};
            info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount=stages.size();info.pStages=stages.data();
            info.pVertexInputState=&vertexInput;
            info.pInputAssemblyState=&assembly;
            info.pViewportState=&viewport;
            info.pRasterizationState=&raster;
            info.pMultisampleState=&multisample;
            info.pDepthStencilState=&depth;
            info.pColorBlendState=&blend;
            info.pDynamicState=&dynamic;
            info.layout=particlePipelineLayout;
            info.renderPass=renderPass;
            require(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,
                                               nullptr,&particlePipeline),
                    "vkCreateGraphicsPipelines");
        } catch (...) {
            if(fragment)vkDestroyShaderModule(device,fragment,nullptr);
            vkDestroyShaderModule(device,vertex,nullptr);
            throw;
        }
        vkDestroyShaderModule(device,fragment,nullptr);
        vkDestroyShaderModule(device,vertex,nullptr);
    }

    void createSkyCloudPipelines(const std::filesystem::path& shaderRoot) {
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
        multisample.rasterizationSamples = sampleCount;
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
        skyLayoutInfo.pSetLayouts = &skyDescriptorSetLayout;
        require(vkCreatePipelineLayout(device, &skyLayoutInfo, nullptr,
                                       &skyPipelineLayout),
                "vkCreatePipelineLayout");
        VkPipelineVertexInputStateCreateInfo emptyVertexInput{};
        emptyVertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        const auto createGraphics = [&](const char* vertexName,
                                        const char* fragmentName,
                                        const VkPipelineVertexInputStateCreateInfo& input,
                                        VkPipelineLayout layout,
                                        VkPipeline* output) {
            VkShaderModule vertex = loadShader(shaderRoot / vertexName);
            VkShaderModule fragment = VK_NULL_HANDLE;
            try {
                fragment = loadShader(shaderRoot / fragmentName);
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
                info.renderPass = renderPass;
                require(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                   &info, nullptr, output),
                        "vkCreateGraphicsPipelines");
            } catch (...) {
                if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
                vkDestroyShaderModule(device, vertex, nullptr);
                throw;
            }
            vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
        };
        createGraphics("sky.vert.spv", "sky.frag.spv", emptyVertexInput,
                       skyPipelineLayout, &skyPipeline);

        const VkVertexInputBindingDescription cloudBinding{
            0, sizeof(CloudInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
        const std::array<VkVertexInputAttributeDescription, 2> cloudAttributes{{
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CloudInstance, x)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(CloudInstance, depth)}}};
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
        require(vkCreatePipelineLayout(device, &cloudLayoutInfo, nullptr,
                                       &cloudPipelineLayout),
                "vkCreatePipelineLayout");
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        // The shared compatibility cube uses legacy per-face winding. Clouds
        // are opaque, so double-sided rasterization preserves the OpenGL
        // silhouette without making winding part of the shared data contract.
        raster.cullMode = VK_CULL_MODE_NONE;
        createGraphics("cloud.vert.spv", "cloud.frag.spv", cloudVertexInput,
                       cloudPipelineLayout, &cloudPipeline);
    }

    void createFramebuffers() {
        framebuffers.resize(imageViews.size());
        for (size_t i = 0; i < imageViews.size(); ++i) {
            const bool multisampled = sampleCount != VK_SAMPLE_COUNT_1_BIT;
            const std::array<VkImageView, 3> attachments{
                multisampled ? colorImageViews[i] : imageViews[i],
                depthImageViews[i], imageViews[i]};
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = renderPass;
            info.attachmentCount = multisampled ? 3u : 2u;
            info.pAttachments = attachments.data();
            info.width = swapchainExtent.width;
            info.height = swapchainExtent.height;
            info.layers = 1;
            require(vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]),
                    "vkCreateFramebuffer");
        }
    }

    void allocateCommandBuffers() {
        commandBuffers.resize(images.size());
        VkCommandBufferAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = commandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = commandBuffers.size();
        require(vkAllocateCommandBuffers(device, &info, commandBuffers.data()),
                "vkAllocateCommandBuffers");
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
            std::copy(batch.indices.begin(), batch.indices.end(),
                      indices + indexOffset);
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
        size_t count = 0;
        forEachModelPrimitive(submittedModelOpaque, false,
            [&](const auto&, auto&, auto&, const auto&) { ++count; });
        forEachModelPrimitive(submittedModelBlend, true,
            [&](const auto&, auto&, auto&, const auto&) { ++count; });
        if (count == 0) return;
        ModelFrameBuffer& frame = modelBuffers[currentFrame];
        if (frame.capacity < count) {
            if (frame.descriptorSet) {
                require(vkFreeDescriptorSets(device, descriptorPool, 1,
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
            allocate.descriptorPool = descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &modelUniformDescriptorSetLayout;
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
                framebufferSrgb ? 0.0f : 1.0f, 0.0f};
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
        VkCommandBuffer command = commandBuffers[imageIndex];
        require(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
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
                const VkBufferCopy region{copy.sourceOffset, 0, copy.size};
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
        std::array<VkClearValue, 3> clear{};
        clear[0].color.float32[0] = submittedFrame.clearColor.r;
        clear[0].color.float32[1] = submittedFrame.clearColor.g;
        clear[0].color.float32[2] = submittedFrame.clearColor.b;
        clear[0].color.float32[3] = submittedFrame.clearColor.a;
        clear[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo render{};
        render.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render.renderPass = renderPass;
        render.framebuffer = framebuffers[imageIndex];
        render.renderArea.extent = swapchainExtent;
        render.clearValueCount = sampleCount != VK_SAMPLE_COUNT_1_BIT ? 3u : 2u;
        render.pClearValues = clear.data();
        vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
        VkPipeline boundPipeline = VK_NULL_HANDLE;
        const VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(swapchainExtent.width),
            static_cast<float>(swapchainExtent.height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, swapchainExtent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        const VkDeviceSize offset = 0;
        if (skyQueued) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              skyPipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                skyPipelineLayout, 0, 1,
                &skyBuffers[currentFrame].descriptorSet, 0, nullptr);
            vkCmdDraw(command, 3, 1, 0, 0);
        }
        if (cloudsQueued && !cloudInstances.empty()) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              cloudPipeline);
            const VkBuffer buffer = cloudBuffers[currentFrame].instance.handle;
            vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
            const CloudUniforms constants{
                clipSpaceCorrection(GraphicsApi::Vulkan) * cloudViewProjection,
                glm::vec4(cloudOrigin, 0.0f), glm::vec4(cloudColor,
                    framebufferSrgb ? 0.0f : 1.0f)};
            vkCmdPushConstants(command, cloudPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(constants), &constants);
            vkCmdDraw(command, 36,
                      static_cast<uint32_t>(cloudInstances.size()), 0, 0);
        }
        for (const DrawCommand& draw : submittedDraws) {
            const auto meshIt = meshes.find(draw.mesh.value);
            const auto materialIt = materials.find(draw.material.value);
            if (meshIt == meshes.end() || materialIt == materials.end()) continue;
            const GpuMesh& mesh = meshIt->second;
            const GpuMaterial& material = materialIt->second;
            VkPipeline requestedPipeline = pipeline;
            if (material.desc.pipeline == MaterialPipeline::ChunkTranslucent)
                requestedPipeline = translucentPipeline;
            else if (material.desc.pipeline == MaterialPipeline::UnlitTextured) {
                if (material.desc.depthTest)
                    requestedPipeline = material.desc.backfaceCull
                        ? basicPipeline : basicNoCullPipeline;
                else
                    requestedPipeline = material.desc.backfaceCull
                        ? basicNoDepthPipeline : basicNoDepthNoCullPipeline;
            }
            if (requestedPipeline != boundPipeline) {
                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  requestedPipeline);
                boundPipeline = requestedPipeline;
            }
            vkCmdBindVertexBuffers(command, 0, 1, &mesh.vertex.handle, &offset);
            vkCmdBindIndexBuffer(command, mesh.index.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout, 0, 1,
                                    &material.descriptorSet, 0, nullptr);
            if (material.desc.pipeline != MaterialPipeline::UnlitTextured)
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout, 1, 1,
                                        &chunkBuffers[currentFrame].descriptorSet,
                                        0, nullptr);
            const FrameUniforms constants{
                clipSpaceCorrection(GraphicsApi::Vulkan) * submittedFrame.projection *
                    submittedFrame.view * draw.model,
                {static_cast<float>(material.desc.atlasTilesPerSide),
                 material.desc.smoothLighting ? 1.0f : 0.0f,
                 material.desc.alphaCutoff, framebufferSrgb ? 0.0f : 1.0f},
                glm::vec4(glm::vec3(draw.model[3]), 0.0f), draw.tint};
            vkCmdPushConstants(command, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(constants), &constants);
            const uint32_t count = draw.indexCount ? draw.indexCount : mesh.indexCount;
            vkCmdDrawIndexed(command, count, 1, draw.firstIndex, 0, 0);
        }
        uint32_t modelUniformIndex = 0;
        const auto drawModels = [&](std::vector<ModelPassSubmission>& submissions,
                                    bool blended) {
            forEachModelPrimitive(submissions, blended,
                [&](const ModelPassSubmission&, ModelResource& resource,
                    ModelPrimitive& primitive, const model::Material& material) {
                    const VkPipeline selected = blended
                        ? (material.doubleSided ? modelBlendDoubleSidedPipeline
                                                : modelBlendPipeline)
                        : (material.doubleSided ? modelOpaqueDoubleSidedPipeline
                                                : modelOpaquePipeline);
                    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, selected);
                    vkCmdBindVertexBuffers(command, 0, 1,
                                           &primitive.vertex.handle, &offset);
                    vkCmdBindIndexBuffer(command, primitive.index.handle, 0,
                                         VK_INDEX_TYPE_UINT32);
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
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        modelPipelineLayout, 0, 1, &descriptor->second.descriptorSet,
                        0, nullptr);
                    const VkDeviceSize byteOffset =
                        modelUniformIndex++ * modelUniformStride;
                    if (byteOffset > std::numeric_limits<uint32_t>::max())
                        throw std::runtime_error("Vulkan model uniform offset overflow");
                    const uint32_t dynamicOffset = static_cast<uint32_t>(byteOffset);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        modelPipelineLayout, 1, 1,
                        &modelBuffers[currentFrame].descriptorSet,
                        1, &dynamicOffset);
                    vkCmdDrawIndexed(command, primitive.indexCount, 1, 0, 0, 0);
                });
        };
        drawModels(submittedModelOpaque, false);
        drawModels(submittedModelBlend, true);
        if(!submittedParticles.empty()){
            const auto material=materials.find(particleMaterial.value);
            if(material!=materials.end()){
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  particlePipeline);
                const VkBuffer buffer=particleBuffers[currentFrame].instance.handle;
                vkCmdBindVertexBuffers(command,0,1,&buffer,&offset);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                    particlePipelineLayout,0,1,&material->second.descriptorSet,
                    0,nullptr);
                ParticleUniforms constants;
                constants.viewProjection=clipSpaceCorrection(GraphicsApi::Vulkan)*
                    particleViewProjection;
                constants.cameraRightTime=glm::vec4(particleCameraRight,particleTime);
                constants.cameraUpIntensity=glm::vec4(particleCameraUp,
                                                       particleIntensity);
                constants.atlasParams.x=static_cast<float>(
                    material->second.desc.atlasTilesPerSide);
                constants.atlasParams.y=framebufferSrgb?0.0f:1.0f;
                vkCmdPushConstants(command,particlePipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,sizeof(constants),&constants);
                vkCmdDraw(command,6,static_cast<uint32_t>(submittedParticles.size()),
                          0,0);
            }
        }
        if (wireQueued) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              wirePipeline);
            const WireUniforms constants{wireModelViewProjection,
                {framebufferSrgb ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f}};
            vkCmdPushConstants(command, wirePipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(constants), &constants);
            vkCmdDraw(command, 24, 1, 0, 0);
        }
        if (!submittedUi.empty()) {
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,uiPipeline);
            const UiFrameBuffers& buffers=uiBuffers[currentFrame];
            const VkDeviceSize uiOffset=0;
            vkCmdBindVertexBuffers(command,0,1,&buffers.vertex.handle,&uiOffset);
            vkCmdBindIndexBuffer(command,buffers.index.handle,0,VK_INDEX_TYPE_UINT32);
            uint32_t firstIndex=0;
            int32_t vertexOffset=0;
            for(const UiSubmission& batch:submittedUi){
                const auto material=materials.find(batch.material.value);
                if(material==materials.end())continue;
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout,0,1,&material->second.descriptorSet,0,nullptr);
                const UiConstants constants{batch.projection,
                    {framebufferSrgb ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f}};
                vkCmdPushConstants(command,pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,sizeof(constants),&constants);
                vkCmdDrawIndexed(command,static_cast<uint32_t>(batch.indices.size()),
                    1,firstIndex,vertexOffset,0);
                firstIndex+=static_cast<uint32_t>(batch.indices.size());
                vertexOffset+=static_cast<int32_t>(batch.vertices.size());
            }
        }
        vkCmdEndRenderPass(command);
        require(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    }

    void drawFrame() {
        if (window.isMinimized() || window.width() <= 0 || window.height() <= 0)
            return;
        if (swapchainDirty) recreateSwapchain();
        require(vkWaitForFences(device, 1, &frameFences[currentFrame], VK_TRUE,
                                UINT64_MAX), "vkWaitForFences");
        for (GpuMesh& mesh : retiredMeshes[currentFrame]) {
            destroyBuffer(mesh.index);
            destroyBuffer(mesh.vertex);
        }
        retiredMeshes[currentFrame].clear();
        uint32_t imageIndex = 0;
        const VkResult acquire = vkAcquireNextImageKHR(
            device, swapchain, UINT64_MAX, imageAvailable[currentFrame],
            VK_NULL_HANDLE, &imageIndex);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
            require(acquire, "vkAcquireNextImageKHR");
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
            require(vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE,
                                    UINT64_MAX), "vkWaitForFences");
        imagesInFlight[imageIndex] = frameFences[currentFrame];
        prepareSkyBuffer();
        prepareChunkBuffer();
        prepareCloudBuffer();
        prepareParticleBuffer();
        prepareModelBuffer();
        prepareUiBuffers();
        prepareBufferUploads();
        recordCommandBuffer(imageIndex);
        require(vkResetFences(device, 1, &frameFences[currentFrame]), "vkResetFences");
        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable[currentFrame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffers[imageIndex];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished[currentFrame];
        require(vkQueueSubmit(graphicsQueue, 1, &submit, frameFences[currentFrame]),
                "vkQueueSubmit");
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished[currentFrame];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        const VkResult result = vkQueuePresentKHR(presentQueue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
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

    void destroySwapchain() {
        if (!device) return;
        if (!commandBuffers.empty() && commandPool)
            vkFreeCommandBuffers(device, commandPool, commandBuffers.size(),
                                 commandBuffers.data());
        commandBuffers.clear();
        for (VkFramebuffer framebuffer : framebuffers)
            if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffers.clear();
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
        if (translucentPipeline)
            vkDestroyPipeline(device, translucentPipeline, nullptr);
        translucentPipeline = VK_NULL_HANDLE;
        for (VkPipeline* basic : {&basicPipeline, &basicNoCullPipeline,
                &basicNoDepthPipeline, &basicNoDepthNoCullPipeline}) {
            if (*basic) vkDestroyPipeline(device, *basic, nullptr);
            *basic = VK_NULL_HANDLE;
        }
        if (uiPipeline) vkDestroyPipeline(device, uiPipeline, nullptr);
        uiPipeline = VK_NULL_HANDLE;
        if (skyPipeline) vkDestroyPipeline(device, skyPipeline, nullptr);
        skyPipeline = VK_NULL_HANDLE;
        if (cloudPipeline) vkDestroyPipeline(device, cloudPipeline, nullptr);
        cloudPipeline = VK_NULL_HANDLE;
        if (wirePipeline) vkDestroyPipeline(device, wirePipeline, nullptr);
        wirePipeline = VK_NULL_HANDLE;
        for (VkPipeline* modelPipeline : {&modelOpaquePipeline,
                &modelOpaqueDoubleSidedPipeline, &modelBlendPipeline,
                &modelBlendDoubleSidedPipeline}) {
            if (*modelPipeline) vkDestroyPipeline(device, *modelPipeline, nullptr);
            *modelPipeline = VK_NULL_HANDLE;
        }
        if (particlePipeline)
            vkDestroyPipeline(device,particlePipeline,nullptr);
        particlePipeline=VK_NULL_HANDLE;
        if (particlePipelineLayout)
            vkDestroyPipelineLayout(device,particlePipelineLayout,nullptr);
        particlePipelineLayout=VK_NULL_HANDLE;
        if (skyPipelineLayout)
            vkDestroyPipelineLayout(device, skyPipelineLayout, nullptr);
        skyPipelineLayout = VK_NULL_HANDLE;
        if (cloudPipelineLayout)
            vkDestroyPipelineLayout(device, cloudPipelineLayout, nullptr);
        cloudPipelineLayout = VK_NULL_HANDLE;
        if (wirePipelineLayout)
            vkDestroyPipelineLayout(device, wirePipelineLayout, nullptr);
        wirePipelineLayout = VK_NULL_HANDLE;
        if (modelPipelineLayout)
            vkDestroyPipelineLayout(device, modelPipelineLayout, nullptr);
        modelPipelineLayout = VK_NULL_HANDLE;
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
        for (VkImageView view : depthImageViews)
            if (view) vkDestroyImageView(device, view, nullptr);
        depthImageViews.clear();
        if (allocator) {
            for (size_t i = 0; i < depthImages.size(); ++i) {
                if (depthImages[i])
                    vmaDestroyImage(allocator, depthImages[i], depthAllocations[i]);
            }
        }
        depthImages.clear();
        depthAllocations.clear();
        for (VkImageView view : colorImageViews)
            if (view) vkDestroyImageView(device, view, nullptr);
        colorImageViews.clear();
        if (allocator) {
            for (size_t i = 0; i < colorImages.size(); ++i) {
                if (colorImages[i])
                    vmaDestroyImage(allocator, colorImages[i], colorAllocations[i]);
            }
        }
        colorImages.clear();
        colorAllocations.clear();
        for (VkImageView view : imageViews)
            if (view) vkDestroyImageView(device, view, nullptr);
        imageViews.clear();
        images.clear();
        imagesInFlight.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    void recreateSwapchain() {
        if (window.isMinimized() || window.width() <= 0 || window.height() <= 0)
            return;
        require(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
        destroySwapchain();
        createSwapchain();
    }

    void cleanup() {
        if (device) vkDeviceWaitIdle(device);
        destroySwapchain();
        if (device && descriptorPool)
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
        if (allocator) {
            for (auto& [id, mesh] : meshes) {
                (void)id;
                destroyBuffer(mesh.index);
                destroyBuffer(mesh.vertex);
            }
            meshes.clear();
            for (auto& retired : retiredMeshes) {
                for (GpuMesh& mesh : retired) {
                    destroyBuffer(mesh.index);
                    destroyBuffer(mesh.vertex);
                }
                retired.clear();
            }
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
            for(auto& buffer:particleBuffers)
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
        if (device && descriptorSetLayout)
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
        if (device && skyDescriptorSetLayout)
            vkDestroyDescriptorSetLayout(device, skyDescriptorSetLayout, nullptr);
        skyDescriptorSetLayout = VK_NULL_HANDLE;
        if (device && chunkDescriptorSetLayout)
            vkDestroyDescriptorSetLayout(device, chunkDescriptorSetLayout, nullptr);
        chunkDescriptorSetLayout = VK_NULL_HANDLE;
        if (device && modelUniformDescriptorSetLayout)
            vkDestroyDescriptorSetLayout(device, modelUniformDescriptorSetLayout, nullptr);
        modelUniformDescriptorSetLayout = VK_NULL_HANDLE;
        if (device) {
            for (size_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
                if (frameFences[i]) vkDestroyFence(device, frameFences[i], nullptr);
                if (renderFinished[i])
                    vkDestroySemaphore(device, renderFinished[i], nullptr);
                if (imageAvailable[i])
                    vkDestroySemaphore(device, imageAvailable[i], nullptr);
            }
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
        }
        commandPool = VK_NULL_HANDLE;
        if (allocator) vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
        if (device) vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
};

namespace model {

class VulkanModelBackend final : public IModelRenderBackend {
public:
    explicit VulkanModelBackend(VulkanRenderer& renderer) : m_renderer(renderer) {}
    ~VulkanModelBackend() override { clear(); }

    ModelHandle upload(std::shared_ptr<const ModelAsset> asset) override {
        if (!asset) throw std::invalid_argument("cannot upload a null model asset");
        auto& impl = *m_renderer.m_impl;
        VulkanRenderer::Impl::ModelResource resource;
        resource.asset = std::move(asset);
        try {
            for (const ImageData& image : resource.asset->images) {
                TextureData texture;
                texture.width = static_cast<uint32_t>(image.width);
                texture.height = static_cast<uint32_t>(image.height);
                texture.pixels = image.pixels;
                TextureSamplerDesc sampler;
                sampler.addressU = TextureAddressMode::ClampToEdge;
                sampler.addressV = TextureAddressMode::ClampToEdge;
                const RenderTextureHandle textureHandle =
                    m_renderer.createTexture(texture, sampler);
                resource.textures.push_back(textureHandle);
                MaterialDesc material;
                material.pipeline = MaterialPipeline::UiTextured;
                material.baseColorTexture = textureHandle;
                resource.textureMaterials.push_back(
                    m_renderer.createMaterial(material));
            }
            std::vector<int> nodes(resource.asset->primitives.size(), -1);
            for (size_t node = 0; node < resource.asset->nodes.size(); ++node)
                for (int primitive : resource.asset->nodes[node].primitives)
                    if (primitive >= 0 && static_cast<size_t>(primitive) < nodes.size())
                        nodes[static_cast<size_t>(primitive)] = static_cast<int>(node);
            for (size_t index = 0; index < resource.asset->primitives.size(); ++index) {
                const Primitive& source = resource.asset->primitives[index];
                VulkanRenderer::Impl::ModelPrimitive primitive;
                primitive.vertex = impl.uploadDeviceBuffer(
                    source.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                try {
                    primitive.index = impl.uploadDeviceBuffer(
                        source.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
                } catch (...) {
                    impl.destroyBuffer(primitive.vertex);
                    throw;
                }
                primitive.indexCount = static_cast<uint32_t>(source.indices.size());
                primitive.material = source.material;
                primitive.skin = source.skin;
                primitive.node = nodes[index];
                resource.primitives.push_back(std::move(primitive));
            }
        } catch (...) {
            destroy(resource);
            throw;
        }
        impl.models.push_back(std::move(resource));
        return static_cast<ModelHandle>(impl.models.size());
    }

    void queue(const ModelDraw& draw) override {
        if (draw.model && draw.model <= m_renderer.m_impl->models.size())
            m_draws.push_back(draw);
    }

    void flushOpaque(const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) override {
        append(m_renderer.m_impl->submittedModelOpaque, vp, environment,
               camera, fogStart, fogEnd);
    }

    void flushBlend(const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) override {
        sortBlended(m_draws);
        append(m_renderer.m_impl->submittedModelBlend, vp, environment,
               camera, fogStart, fogEnd);
        m_draws.clear();
        if (!m_renderer.m_impl->submittedModelOpaque.empty() ||
            !m_renderer.m_impl->submittedModelBlend.empty())
            m_renderer.m_impl->drawQueued = true;
    }

    void clear() override {
        m_draws.clear();
        if (!m_renderer.m_impl) return;
        for (auto& resource : m_renderer.m_impl->models) destroy(resource);
        m_renderer.m_impl->models.clear();
    }

private:
    VulkanRenderer& m_renderer;
    std::vector<ModelDraw> m_draws;

    void append(std::vector<VulkanRenderer::Impl::ModelPassSubmission>& target,
        const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) {
        for (const ModelDraw& draw : m_draws)
            target.push_back({draw, vp, environment, camera, fogStart, fogEnd});
    }

    void destroy(VulkanRenderer::Impl::ModelResource& resource) {
        auto& impl = *m_renderer.m_impl;
        for (auto& primitive : resource.primitives) {
            impl.destroyBuffer(primitive.index);
            impl.destroyBuffer(primitive.vertex);
        }
        resource.primitives.clear();
        for (RenderMaterialHandle material : resource.textureMaterials)
            if (material && impl.materials.count(material.value))
                m_renderer.destroyMaterial(material);
        resource.textureMaterials.clear();
        for (RenderTextureHandle texture : resource.textures)
            if (texture && impl.textures.count(texture.value))
                m_renderer.destroyTexture(texture);
        resource.textures.clear();
    }
};

std::unique_ptr<ModelRenderer> createVulkanModelRenderer(
    ::VulkanRenderer& renderer) {
    return std::make_unique<ModelRenderer>(
        std::make_unique<VulkanModelBackend>(renderer));
}

} // namespace model

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
    material.descriptorSet = m_impl->createMaterialDescriptor(texture->second);
    m_impl->materials.emplace(handle.value, material);
    return handle;
}

void VulkanRenderer::destroyMaterial(RenderMaterialHandle handle) {
    const auto found = m_impl->materials.find(handle.value);
    if (found == m_impl->materials.end())
        throw std::invalid_argument("Unknown Vulkan material handle");
    require(vkFreeDescriptorSets(m_impl->device, m_impl->descriptorPool, 1,
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
    m_impl->submittedChunkEnvironment.materialParams = {
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Lava)),
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Water)),
        Config::FOG_START_FRACTION, m_impl->framebufferSrgb ? 0.0f : 1.0f};
    m_impl->frameBegun = true;
    m_impl->drawQueued = false;
    m_impl->submittedDraws.clear();
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
    m_impl->submittedDraws.push_back(command);
    m_impl->drawQueued = true;
}

void VulkanRenderer::endFrame() {
    if (!m_impl->frameBegun)
        throw std::logic_error("No active Vulkan frame");
    if (m_impl->drawQueued) m_impl->drawFrame();
    m_impl->frameBegun = false;
    m_impl->drawQueued = false;
}

void VulkanRenderer::resize(int, int) { m_impl->swapchainDirty = true; }

void VulkanRenderer::waitIdle() {
    if (m_impl->device) require(vkDeviceWaitIdle(m_impl->device), "vkDeviceWaitIdle");
}

void VulkanRenderer::initialize(Window& window,
                                const GraphicsCapabilities& capabilities,
                                const std::filesystem::path& assetRoot) {
    if (capabilities.api != GraphicsApi::Vulkan)
        throw std::invalid_argument("VulkanRenderer requires a Vulkan window");
    if (m_impl) throw std::logic_error("VulkanRenderer is already initialized");
    m_window = &window;
    m_assetRoot = assetRoot;
    m_impl = std::make_unique<Impl>(window, assetRoot);
    const BlockAtlasData atlas = buildBlockAtlasData(assetRoot);
    TextureSamplerDesc sampler;
    sampler.minFilter = TextureFilter::NearestMipmapLinear;
    sampler.addressU = TextureAddressMode::ClampToEdge;
    sampler.addressV = TextureAddressMode::ClampToEdge;
    m_blockAtlas = createTexture(atlas.texture, sampler);
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

void VulkanRenderer::reinitialize(const GraphicsCapabilities& capabilities,
                                  const std::filesystem::path& assetRoot) {
    if (!m_window) throw std::logic_error("VulkanRenderer has no window");
    waitIdle();
    m_modelRenderer.reset();
    for (RenderMeshHandle mesh : m_compatibilityCubes)
        if (mesh) destroyMesh(mesh);
    if (m_entityMaterial) destroyMaterial(m_entityMaterial);
    if (m_entityAtlas) destroyTexture(m_entityAtlas);
    if (m_chunkTranslucent) destroyMaterial(m_chunkTranslucent);
    if (m_chunkOpaque) destroyMaterial(m_chunkOpaque);
    if (m_blockAtlas) destroyTexture(m_blockAtlas);
    m_chunkTranslucent = {};
    m_chunkOpaque = {};
    m_blockAtlas = {};
    m_entityMaterial = {};
    m_entityAtlas = {};
    m_compatibilityCubes.fill({});
    m_impl.reset();
    initialize(*m_window, capabilities, assetRoot);
}

void VulkanRenderer::beginFrame() {
    FrameData frame;
    frame.projection = m_viewProjection;
    frame.ambientColor = m_environment.ambientColor * m_environment.ambientIntensity;
    frame.directColor = m_environment.directColor * m_environment.directIntensity;
    frame.lightDirection = m_environment.lightDirection;
    beginFrame(frame);
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
    chunk.materialParams = {
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Lava)),
        static_cast<float>(getAtlasTextureIndex(BlockTexture::Water)),
        Config::FOG_START_FRACTION, m_impl->framebufferSrgb ? 0.0f : 1.0f};
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
        clipSpaceCorrection(GraphicsApi::Vulkan) * viewProjection);
    sky.cameraPosition = glm::vec4(cameraPosition, 0.0f);
    sky.sunDirection = glm::vec4(environment.sunDirection, 0.0f);
    sky.moonDirection = glm::vec4(environment.moonDirection, 0.0f);
    sky.zenithColor = glm::vec4(environment.zenithColor, 0.0f);
    sky.horizonColor = glm::vec4(environment.horizonColor, 0.0f);
    sky.weather = {environment.starIntensity, environment.rainIntensity,
                   environment.thunderIntensity,
                   static_cast<float>(RuntimeClock::seconds(RuntimeClock{}.now()))};
    sky.options = {renderClouds ? 1.0f : 0.0f,
                   m_impl->framebufferSrgb ? 0.0f : 1.0f, 0.0f, 0.0f};
    m_impl->skyQueued = true;
    m_impl->drawQueued = true;
}

void VulkanRenderer::uploadChunkMesh(ChunkMesh& mesh) {
    if (mesh.empty()) {
        releaseChunkMesh(mesh);
        return;
    }
    if (mesh.renderHandle) destroyMesh(mesh.renderHandle);
    MeshData data;
    data.layout = MeshVertexLayout::Chunk;
    data.chunkVertices = mesh.vertices;
    data.indices.assign(mesh.indices.begin(), mesh.indices.end());
    data.opaqueIndexCount = static_cast<uint32_t>(mesh.opaqueIndexCount);
    data.translucentIndexOffset = static_cast<uint32_t>(mesh.translucentIndexOffset);
    data.translucentIndexCount = static_cast<uint32_t>(mesh.translucentIndexCount);
    mesh.renderHandle = createMesh(data);
    mesh.indexCount = mesh.indices.size();
    mesh.gpuReady = true;
}

void VulkanRenderer::releaseChunkMesh(ChunkMesh& mesh) {
    if (mesh.renderHandle) destroyMesh(mesh.renderHandle);
    mesh.abandonGpuResources();
}

void VulkanRenderer::renderChunk(const ChunkMesh& mesh,
                                 const glm::mat4& model,
                                 const glm::mat4&, bool translucent) {
    if (!mesh.gpuReady || !mesh.renderHandle) return;
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

void VulkanRenderer::beginTranslucent() {}
void VulkanRenderer::endTranslucent() {}
void VulkanRenderer::bindBlockShader() const {}
void VulkanRenderer::unbindBlockShader() const {}
void VulkanRenderer::renderWireframe(const glm::vec3& blockPosition,
                                     const glm::mat4& viewProjection) {
    if (!m_impl || !m_impl->frameBegun) return;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), blockPosition);
    model = glm::translate(model, glm::vec3(0.5f));
    model = glm::scale(model, glm::vec3(1.003f));
    model = glm::translate(model, glm::vec3(-0.5f));
    m_impl->wireModelViewProjection =
        clipSpaceCorrection(GraphicsApi::Vulkan) * viewProjection * model;
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
    const float skyLight = std::pow(std::clamp(light.sky, 0.0f, 1.0f), 1.35f);
    const float blockLight = std::pow(std::clamp(light.block, 0.0f, 1.0f), 1.35f);
    const glm::vec3 illumination = glm::max(glm::vec3(skyLight),
        glm::vec3(1.0f, 0.72f, 0.38f) * blockLight);
    command.tint = glm::vec4(color * glm::max(illumination, glm::vec3(0.025f)), 1.0f);
    draw(command);
}
model::ModelRenderer& VulkanRenderer::modelRenderer() { return *m_modelRenderer; }
bool VulkanRenderer::usesFramebufferSrgb() const {
    return m_impl && m_impl->framebufferSrgb;
}
void VulkanRenderer::flushModels(const glm::mat4& viewProjection) {
    const float fogEnd = (static_cast<float>(Config::RENDER_DISTANCE) + 0.5f) *
                         Config::CHUNK_SIZE_X;
    m_modelRenderer->flushOpaque(viewProjection, m_environment,
        glm::vec3(0.0f), fogEnd * Config::FOG_START_FRACTION, fogEnd);
    m_modelRenderer->flushBlend(viewProjection, m_environment,
        glm::vec3(0.0f), fogEnd * Config::FOG_START_FRACTION, fogEnd);
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
    if (!m_impl || !m_impl->frameBegun)
        throw std::logic_error("Vulkan clouds require an active frame");
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
