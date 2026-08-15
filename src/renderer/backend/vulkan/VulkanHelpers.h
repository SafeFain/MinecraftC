#pragma once

#include "renderer/RenderDevice.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <optional>
#include <vector>

// Pure Vulkan selection, query, and decode helpers shared by the backend
// implementation files. No state, no ownership; every function takes its
// inputs explicitly.

namespace vkhelp {

void require(VkResult result, const char* operation);

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const {
        return graphics.has_value() && present.has_value();
    }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
bool supportsDeviceExtension(VkPhysicalDevice device, const char* name);
#if defined(__APPLE__)
bool supportsInstanceExtension(const char* name);
#endif
bool supportsSwapchain(VkPhysicalDevice device);

VkSurfaceFormatKHR chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats);
bool isSrgbFormat(VkFormat format);
VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes,
                                   bool synchronize);
VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
    VkCompositeAlphaFlagsKHR supported);
VkSurfaceTransformFlagBitsKHR chooseSurfaceTransform(
    const VkSurfaceCapabilitiesKHR& capabilities);
VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                        int windowWidth, int windowHeight);
VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

TextureData loadRgbaTexture(const std::filesystem::path& path);

}  // namespace vkhelp
