#include "renderer/backend/vulkan/VulkanHelpers.h"

#include "core/AssetStore.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace vkhelp {

void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) +
                                 " failed with Vulkan result " +
                                 std::to_string(result));
}

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

bool supportsDeviceExtension(VkPhysicalDevice device, const char* name) {
    uint32_t count = 0;
    require(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
            "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    require(vkEnumerateDeviceExtensionProperties(
                device, nullptr, &count, extensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::string(extension.extensionName) == name;
    });
}

#if defined(__APPLE__)
bool supportsInstanceExtension(const char* name) {
    uint32_t count = 0;
    require(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
            "vkEnumerateInstanceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    require(vkEnumerateInstanceExtensionProperties(
                nullptr, &count, extensions.data()),
            "vkEnumerateInstanceExtensionProperties");
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::string(extension.extensionName) == name;
    });
}
#endif

bool supportsSwapchain(VkPhysicalDevice device) {
    return supportsDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
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

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes,
                                  bool synchronize) {
    if (!synchronize &&
        std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
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

VkSurfaceTransformFlagBitsKHR chooseSurfaceTransform(
    const VkSurfaceCapabilitiesKHR& capabilities) {
    // Android commonly reports a 90/270-degree current transform after the
    // Activity enters sensor landscape. Using that transform promises that
    // the application has already pre-rotated every 3D and UI projection.
    // MinecraftC renders in SDL's logical orientation instead, so request
    // identity and let the compositor apply the display rotation.
    if ((capabilities.supportedTransforms &
         VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0)
        return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    return capabilities.currentTransform;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                        int windowWidth, int windowHeight) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;
    return {
        std::clamp(static_cast<uint32_t>(std::max(1, windowWidth)),
                   capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(std::max(1, windowHeight)),
                   capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height)};
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
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

}  // namespace vkhelp
