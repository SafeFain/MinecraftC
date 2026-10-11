#pragma once
#include "plugins/ContentRegistry.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>

namespace vkp {
struct PluginSceneSubmission {
    VkBuffer vertex=VK_NULL_HANDLE,index=VK_NULL_HANDLE;
    VkDeviceSize vertexOffset=0;
    uint32_t first=0,count=0,stride=20,uvOffset=12;
    int32_t base=0;
    bool lod=false;
    VkViewport viewport{};
    VkRect2D scissor{};
    glm::mat4 transform{1};
    glm::vec4 tint{1};
    std::vector<MC_RenderVertex> posedVertices;
};
class VulkanPluginBackend {
public:
    VulkanPluginBackend(VkDevice,VmaAllocator,VkCommandPool,VkQueue);
    ~VulkanPluginBackend();
    void prepare(uint32_t,uint32_t,uint32_t,double,const glm::mat4&,const glm::dvec3&,VkImageView,VkSampler,
                 const std::array<std::vector<PluginSceneSubmission>,MC_RENDER_STAGE_COUNT>&);
    bool draw(size_t,const MC_RenderDraw&);
    bool sceneDraw(size_t,uint64_t,MC_RenderHandle);
    bool updateBuffer(size_t,MC_RenderHandle,const void*,uint32_t);
    bool replaces(uint32_t) const;
    void validate(VkRenderPass,VkRenderPass,VkRenderPass,VkSampleCountFlagBits,uint32_t);
    void targets(VkCommandBuffer);
    void record(VkCommandBuffer,VkRenderPass,VkSampleCountFlagBits,uint32_t,uint32_t,bool=false,int=0);
    void invalidate(); // caller has waited for device idle
private:
    struct State;
    std::unique_ptr<State> state;
};
}
