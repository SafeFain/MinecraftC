#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

inline uint32_t rebaseVulkanIndex(uint32_t index, uint32_t vertexBase) {
    if (index > std::numeric_limits<uint32_t>::max() - vertexBase)
        throw std::overflow_error("Vulkan rebased index exceeds uint32_t");
    return index + vertexBase;
}
