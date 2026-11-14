#pragma once

#include "renderer/VisualQuality.h"
#include <glm/vec4.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class VulkanRenderer;

// Adapter diagnostics for captures, GPU ray queries and reproducible GI comparisons.
// Results are CPU values; no GPU handles, private renderer layouts or Vulkan headers
// cross this boundary. Call on the render thread after renderer initialization.
class VulkanDiagnostics {
public:
    struct Ray {
        glm::vec4 origin{0.0f};
        glm::vec4 direction{0.0f};
        glm::vec4 result{0.0f};
    };

    static std::string deviceDescription(VulkanRenderer& renderer);
    static void configureDiagnostic(VulkanRenderer& renderer, const VoxelGiConfig& config);
    static void showReuseMask(VulkanRenderer& renderer, bool enabled);
    // Freeze shader particle animation for repeatable captures; nullopt restores
    // the normal presentation clock. Does not change simulated particle state.
    static void setParticleTime(VulkanRenderer& renderer, std::optional<float> seconds);
    static void requestCapture(VulkanRenderer& renderer);
    static std::vector<uint8_t> readCapture(VulkanRenderer& renderer);
    static std::vector<Ray> traceRays(VulkanRenderer& renderer,
                                     std::vector<Ray> rays,int firstLevel=0);
    static void forceFullUpdate(VulkanRenderer& renderer);
    static bool reusesHistory(VulkanRenderer& renderer);
    static std::vector<float> readEffects(VulkanRenderer& renderer);
};
