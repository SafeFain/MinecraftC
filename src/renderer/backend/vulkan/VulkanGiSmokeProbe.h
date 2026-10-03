#pragma once

// Explicit display-dependent regression probe. Vulkan readback stays in the
// adapter; the smoke driver uses only renderer/world contracts.
#include "renderer/backend/vulkan/VulkanRendererInternal.h"

class VulkanGiSmokeProbe {
public:
    static std::string deviceDescription(VulkanRenderer& renderer) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(renderer.m_impl->physicalDevice,&properties);
        return std::string(properties.deviceName)+"; driver="+std::to_string(properties.driverVersion)+
            "; swapchain images="+std::to_string(renderer.m_impl->swapchain.images.size());
    }

    static void configureDiagnostic(VulkanRenderer& renderer, const VoxelGiConfig& config) {
        auto& impl = *renderer.m_impl;
        const auto before = impl.effectiveVoxelGiConfig();
        impl.diagnosticGiConfig = config;
        impl.applyVoxelGiConfigChange(before, config);
    }

    static void showReuseMask(VulkanRenderer& renderer, bool enabled) {
        renderer.m_impl->diagnosticGiReuseMask = enabled;
    }

    static void requestCapture(VulkanRenderer& renderer) {
        renderer.waitIdle();
        auto& impl = *renderer.m_impl;
        if (!impl.swapchain.captureSupported) throw std::runtime_error("surface capture unavailable");
        const auto format = impl.swapchain.swapchainFormat;
        if (format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB &&
            format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM)
            throw std::runtime_error("capture requires an RGBA8/BGRA8 swapchain");
        impl.destroyBuffer(impl.diagnosticCapture);
        impl.diagnosticCaptureExtent = impl.swapchain.swapchainExtent;
        const auto e = impl.diagnosticCaptureExtent;
        impl.diagnosticCapture = impl.createBuffer(size_t(e.width)*e.height*4,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        impl.diagnosticCapturePending = true;
    }

    static std::vector<uint8_t> readCapture(VulkanRenderer& renderer) {
        renderer.waitIdle();
        auto& impl = *renderer.m_impl;
        if (!impl.diagnosticCapture.handle || impl.diagnosticCapturePending)
            throw std::runtime_error("capture needs a completed frame");
        const auto e = impl.diagnosticCaptureExtent;
        vkhelp::require(vmaInvalidateAllocation(impl.allocator,impl.diagnosticCapture.allocation,
            0,VK_WHOLE_SIZE),"invalidate frame capture");
        const auto* data = static_cast<const uint8_t*>(impl.diagnosticCapture.mapped);
        std::vector<uint8_t> rgba(data,data+size_t(e.width)*e.height*4);
        if (impl.swapchain.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB ||
            impl.swapchain.swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM)
            for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
        return rgba;
    }

    struct Ray {
        glm::vec4 origin{0.0f};
        glm::vec4 direction{0.0f};
        glm::vec4 result{0.0f};
    };
    static_assert(sizeof(Ray) == 48 && offsetof(Ray,result) == 32);

    // Simulate missing finer coverage by moving only this diagnostic uniform's
    // fine windows away. Production volume contents and frame state stay intact.
    static std::vector<Ray> traceRays(VulkanRenderer& renderer,
                                     std::vector<Ray> rays,int firstLevel=0) {
        if (rays.empty()) return rays;
        renderer.waitIdle();
        auto& impl = *renderer.m_impl;
        auto uniforms = lastUniforms(renderer);
        for (int i = 0; i < firstLevel; ++i)
            uniforms.minimumCellAndSize[i] = glm::vec4(100000,100000,100000,
                uniforms.minimumCellAndSize[i].w);
        auto uniform = impl.createBuffer(sizeof(uniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT);
        decltype(uniform) output{};
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkShaderModule shader = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        const auto cleanup = [&] {
            if(command)vkFreeCommandBuffers(impl.device,impl.commandPool,1,&command);
            if(pipeline)vkDestroyPipeline(impl.device,pipeline,nullptr);
            if(shader)vkDestroyShaderModule(impl.device,shader,nullptr);
            if(pipelineLayout)vkDestroyPipelineLayout(impl.device,pipelineLayout,nullptr);
            if(pool)vkDestroyDescriptorPool(impl.device,pool,nullptr);
            if(layout)vkDestroyDescriptorSetLayout(impl.device,layout,nullptr);
            impl.destroyBuffer(output); impl.destroyBuffer(uniform);
        };
        try {
            output = impl.createBuffer(rays.size()*sizeof(Ray),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT);
            std::memcpy(uniform.mapped,&uniforms,sizeof(uniforms));
            std::memcpy(output.mapped,rays.data(),rays.size()*sizeof(Ray));
            vkhelp::require(vmaFlushAllocation(impl.allocator,uniform.allocation,0,VK_WHOLE_SIZE),"flush probe uniform");
            vkhelp::require(vmaFlushAllocation(impl.allocator,output.allocation,0,VK_WHOLE_SIZE),"flush probe rays");
            std::array<VkDescriptorSetLayoutBinding,10> bindings{};
            for(uint32_t i=0;i<4;++i)bindings[i]={2+i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
            bindings[4]={10,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
            for(uint32_t i=0;i<5;++i)bindings[5+i]={11+i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
            VkDescriptorSetLayoutCreateInfo li{}; li.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            li.bindingCount=bindings.size(); li.pBindings=bindings.data();
            vkhelp::require(vkCreateDescriptorSetLayout(impl.device,&li,nullptr,&layout),"probe descriptor layout");
            const std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,5}}};
            VkDescriptorPoolCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pi.maxSets=1; pi.poolSizeCount=sizes.size(); pi.pPoolSizes=sizes.data();
            vkhelp::require(vkCreateDescriptorPool(impl.device,&pi,nullptr,&pool),"probe descriptor pool");
            VkDescriptorSet set=VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool=pool; ai.descriptorSetCount=1; ai.pSetLayouts=&layout;
            vkhelp::require(vkAllocateDescriptorSets(impl.device,&ai,&set),"probe descriptor set");
            std::array<VkDescriptorImageInfo,4> images{};
            std::array<VkWriteDescriptorSet,10> writes{};
            const VkDescriptorBufferInfo uniformInfo{uniform.handle,0,sizeof(uniforms)};
            const VkDescriptorBufferInfo outputInfo{output.handle,0,rays.size()*sizeof(Ray)};
            for(uint32_t i=0;i<10;++i){
                auto& w=writes[i]; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet=set; w.dstBinding=bindings[i].binding; w.descriptorCount=1;
                w.descriptorType=bindings[i].descriptorType;
                if(i<4){images[i]={impl.voxelGiGpu.sampler,impl.voxelGiGpu.irradianceViews[i],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; w.pImageInfo=&images[i];}
                else w.pBufferInfo=i==4?&uniformInfo:i==9?&outputInfo:&impl.voxelGiGpu.auxiliaryInfos[i-5];
            }
            vkUpdateDescriptorSets(impl.device,writes.size(),writes.data(),0,nullptr);
            VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(uint32_t)};
            VkPipelineLayoutCreateInfo pli{}; pli.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pli.setLayoutCount=1; pli.pSetLayouts=&layout; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&push;
            vkhelp::require(vkCreatePipelineLayout(impl.device,&pli,nullptr,&pipelineLayout),"probe pipeline layout");
            shader=impl.loadVoxelGiShader("voxel_gi_probe.comp.spv");
            VkComputePipelineCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};
            ci.layout=pipelineLayout;
            vkhelp::require(vkCreateComputePipelines(impl.device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline),"probe pipeline");
            VkCommandBufferAllocateInfo ca{}; ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ca.commandPool=impl.commandPool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1;
            vkhelp::require(vkAllocateCommandBuffers(impl.device,&ca,&command),"probe command");
            VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkhelp::require(vkBeginCommandBuffer(command,&begin),"begin probe");
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT};
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,1,&before,0,nullptr,0,nullptr);
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&set,0,nullptr);
            const auto count=static_cast<uint32_t>(rays.size());
            vkCmdPushConstants(command,pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(count),&count);
            vkCmdDispatch(command,count,1,1);
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT};
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                0,1,&host,0,nullptr,0,nullptr);
            vkhelp::require(vkEndCommandBuffer(command),"end probe");
            VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
            vkhelp::require(vkQueueSubmit(impl.graphicsQueue,1,&submit,VK_NULL_HANDLE),"submit probe");
            vkhelp::require(vkQueueWaitIdle(impl.graphicsQueue),"wait probe");
            vkhelp::require(vmaInvalidateAllocation(impl.allocator,output.allocation,0,VK_WHOLE_SIZE),"invalidate probe");
            std::memcpy(rays.data(),output.mapped,rays.size()*sizeof(Ray));
            for(const auto& ray:rays)for(int c=0;c<4;++c)
                if(!std::isfinite(ray.result[c]))throw std::runtime_error("nonfinite production GI ray");
            cleanup(); return rays;
        } catch(...) { cleanup(); throw; }
    }
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
