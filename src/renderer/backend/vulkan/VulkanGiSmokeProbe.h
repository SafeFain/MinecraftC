#pragma once

// Explicit display-dependent regression probe. Vulkan readback stays in the
// adapter; the smoke driver uses only renderer/world contracts.
#include "renderer/backend/vulkan/VulkanRendererInternal.h"

class VulkanGiSmokeProbe {
public:
    static void forceFullUpdate(VulkanRenderer& renderer) {
        renderer.waitIdle();
        auto& impl = *renderer.m_impl;
        for (int i = 0; i < impl.voxelGiGpu.levelCount; ++i) {
            // Re-upload the complete CPU mirrors too, so a wrong partial copy
            // cannot pass merely by feeding the same wrong input to full compute.
            impl.voxelGiGpu.levels[static_cast<size_t>(i)].albedoOpacity.initialized = false;
            impl.voxelGiGpu.levels[static_cast<size_t>(i)].lightValidity.initialized = false;
            impl.voxelGiGpu.levels[static_cast<size_t>(i)].dirty.markFull();
        }
    }

    static vkp::VoxelGiScreenUniforms lastUniforms(VulkanRenderer& renderer) {
        const auto& impl = *renderer.m_impl;
        const size_t frame = (impl.currentFrame + impl.FRAMES_IN_FLIGHT - 1) %
            impl.FRAMES_IN_FLIGHT;
        vkp::VoxelGiScreenUniforms result;
        std::memcpy(&result, impl.voxelGiGpu.uniforms[frame].mapped, sizeof(result));
        return result;
    }

    static std::vector<float> readEffects(VulkanRenderer& renderer) {
        renderer.waitIdle();
        auto& impl = *renderer.m_impl;
        const auto& times = impl.voxelGiPreviousHistoryTime;
        if (times.empty()) throw std::runtime_error("GI readback requires a rendered frame");
        const size_t imageIndex = static_cast<size_t>(
            std::max_element(times.begin(), times.end()) - times.begin());
        if (times[imageIndex] == 0)
            throw std::runtime_error("GI readback requires a rendered frame");
        const VkImage image = impl.swapchain.screenEffectImages[imageIndex];
        const auto extent = impl.swapchain.screenEffectExtent;
        const size_t count = static_cast<size_t>(extent.width) * extent.height * 4;
        auto buffer = impl.createBuffer(count * sizeof(uint16_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        VkCommandBuffer command = VK_NULL_HANDLE;
        try {
            VkCommandBufferAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocation.commandPool = impl.commandPool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocation.commandBufferCount = 1;
            vkhelp::require(vkAllocateCommandBuffers(impl.device, &allocation, &command),
                            "allocate GI readback command");
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkhelp::require(vkBeginCommandBuffer(command, &begin), "begin GI readback");
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  buffer.handle, 1, &copy);
            std::swap(barrier.oldLayout, barrier.newLayout);
            std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
            VkBufferMemoryBarrier host{};
            host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = buffer.handle;
            host.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                0, 0, nullptr, 1, &host, 1, &barrier);
            vkhelp::require(vkEndCommandBuffer(command), "end GI readback");
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            vkhelp::require(vkQueueSubmit(impl.graphicsQueue, 1, &submit, VK_NULL_HANDLE),
                            "submit GI readback");
            vkhelp::require(vkQueueWaitIdle(impl.graphicsQueue), "wait GI readback");
            vkhelp::require(vmaInvalidateAllocation(impl.allocator, buffer.allocation, 0,
                            VK_WHOLE_SIZE), "invalidate GI readback");
            const auto* halves = static_cast<const uint16_t*>(buffer.mapped);
            std::vector<float> result(count);
            for (size_t i = 0; i < count; ++i) {
                const int exponent = (halves[i] >> 10) & 31;
                const int mantissa = halves[i] & 1023;
                if (exponent == 31) throw std::runtime_error("non-finite GPU GI pixel");
                const float value = exponent == 0 ? std::ldexp(float(mantissa), -24) :
                    std::ldexp(float(1024 + mantissa), exponent - 25);
                result[i] = (halves[i] & 32768) ? -value : value;
            }
            vkFreeCommandBuffers(impl.device, impl.commandPool, 1, &command);
            impl.destroyBuffer(buffer);
            return result;
        } catch (...) {
            if (command) vkFreeCommandBuffers(impl.device, impl.commandPool, 1, &command);
            impl.destroyBuffer(buffer);
            throw;
        }
    }
};
