#include "renderer/backend/vulkan/VulkanIndexRebase.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        require(rebaseVulkanIndex(0, 24) == 24,
                "zero index did not receive the vertex base");
        require(rebaseVulkanIndex(5, 24) == 29,
                "index was not rebased correctly");
        require(rebaseVulkanIndex(std::numeric_limits<uint32_t>::max(), 0) ==
                    std::numeric_limits<uint32_t>::max(),
                "zero vertex base changed the index");
        bool overflowed = false;
        try {
            (void)rebaseVulkanIndex(std::numeric_limits<uint32_t>::max(), 1);
        } catch (const std::overflow_error&) {
            overflowed = true;
        }
        require(overflowed, "rebased index overflow was not rejected");
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
