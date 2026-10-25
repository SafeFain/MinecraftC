#include "renderer/backend/vulkan/VulkanPluginBackend.h"
#include "renderer/backend/vulkan/VulkanHelpers.h"
#include "renderer/RenderDevice.h"
#include "plugins/ContentQueries.h"
#include "Config.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <tuple>
#include <set>

namespace vkp {
namespace {
void ok(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("Plugin Vulkan resource operation failed");}
}
struct VulkanPluginBackend::State {
    VkDevice device;VmaAllocator allocator;VkCommandPool pool;VkQueue queue;
    struct Buffer {VkBuffer handle=VK_NULL_HANDLE;VmaAllocation allocation=nullptr;void* mapped=nullptr;};
    struct Image {VkImage handle=VK_NULL_HANDLE;VmaAllocation allocation=nullptr;VkImageView view=VK_NULL_HANDLE;VkSampler sampler=VK_NULL_HANDLE;VkRenderPass pass=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;uint32_t width=0,height=0;};
    struct Mesh {Buffer vertices,indices;uint32_t count=0;};
    struct Request {MC_RenderHandle pipeline=0,target=0,uniforms=0;PluginSceneSubmission mesh;};
    std::map<MC_RenderHandle,std::array<Buffer,2>> buffers;
    std::array<std::vector<Buffer>,2> posedBuffers;
    std::map<MC_RenderHandle,Image> images;
    std::map<MC_RenderHandle,Mesh> meshes;
    std::map<MC_RenderHandle,VkShaderModule> shaders;
    std::map<std::tuple<MC_RenderHandle,VkRenderPass,uint32_t,uint32_t,uint32_t>,VkPipeline> pipelines;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;
    std::array<VkDescriptorPool,2> pools{};
    VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
    uint32_t frame=0;uint64_t revision=1;
    Image sceneColor;
    std::array<std::vector<Request>,MC_RENDER_STAGE_COUNT> requests;
    std::array<std::vector<PluginSceneSubmission>,MC_RENDER_STAGE_COUNT> scene;
    std::array<bool,MC_RENDER_STAGE_COUNT> replacement{};
    uint32_t activeStage=MC_RENDER_STAGE_COUNT;
    glm::mat4 viewProjection{1};uint32_t width=0,height=0;
    Image white;Buffer zero;
    State(VkDevice d,VmaAllocator a,VkCommandPool p,VkQueue q):device(d),allocator(a),pool(p),queue(q){}
    Buffer buffer(const void* data,size_t bytes,VkBufferUsageFlags usage) {
        Buffer result;VkBufferCreateInfo b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;b.size=bytes;b.usage=usage;
        VmaAllocationCreateInfo alloc{};alloc.usage=VMA_MEMORY_USAGE_AUTO;alloc.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};ok(vmaCreateBuffer(allocator,&b,&alloc,&result.handle,&result.allocation,&info));result.mapped=info.pMappedData;
        if(data)std::memcpy(result.mapped,data,bytes);else std::memset(result.mapped,0,bytes);
        try{ok(vmaFlushAllocation(allocator,result.allocation,0,bytes));}
        catch(...){destroy(result);throw;}
        return result;
    }
    Image image(uint32_t w,uint32_t h,const void* bytes,bool target) {
        Image result;result.width=w;result.height=h;
        Buffer upload;VkCommandBuffer cmd=VK_NULL_HANDLE;bool submitted=false;
        try {
        VkImageCreateInfo desc{};desc.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;desc.imageType=VK_IMAGE_TYPE_2D;desc.format=target?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_R8G8B8A8_SRGB;
        desc.extent={w,h,1};desc.mipLevels=1;desc.arrayLayers=1;desc.samples=VK_SAMPLE_COUNT_1_BIT;desc.tiling=VK_IMAGE_TILING_OPTIMAL;
        desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(target?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:0);
        VmaAllocationCreateInfo allocation{};allocation.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        ok(vmaCreateImage(allocator,&desc,&allocation,&result.handle,&result.allocation,nullptr));
        VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=result.handle;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=desc.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        ok(vkCreateImageView(device,&view,nullptr,&result.view));
        VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        ok(vkCreateSampler(device,&sampler,nullptr,&result.sampler));
        VkCommandBufferAllocateInfo ca{};ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;ok(vkAllocateCommandBuffers(device,&ca,&cmd));
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;ok(vkBeginCommandBuffer(cmd,&begin));
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=result.handle;barrier.subresourceRange=view.subresourceRange;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(bytes){upload=buffer(bytes,uint64_t(w)*h*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};vkCmdCopyBufferToImage(cmd,upload.handle,result.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);}
        else {VkClearColorValue clear{};vkCmdClearColorImage(cmd,result.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&view.subresourceRange);}
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        ok(vkEndCommandBuffer(cmd));VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        ok(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));submitted=true;ok(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(device,pool,1,&cmd);cmd=VK_NULL_HANDLE;destroy(upload);
        if(target) {
            VkAttachmentDescription attachment{};attachment.format=desc.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
            std::array<VkSubpassDependency,2> deps{};deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            VkRenderPassCreateInfo rp{};rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=2;rp.pDependencies=deps.data();ok(vkCreateRenderPass(device,&rp,nullptr,&result.pass));
            VkFramebufferCreateInfo fb{};fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=result.pass;fb.attachmentCount=1;fb.pAttachments=&result.view;fb.width=w;fb.height=h;fb.layers=1;ok(vkCreateFramebuffer(device,&fb,nullptr,&result.framebuffer));
        }
        return result;
        }catch(...) {
            if(submitted)vkQueueWaitIdle(queue);
            if(cmd)vkFreeCommandBuffers(device,pool,1,&cmd);
            destroy(upload);destroy(result);throw;
        }
    }
    void destroy(Buffer& b){if(b.handle)vmaDestroyBuffer(allocator,b.handle,b.allocation);b={};}
    void destroy(Image& i){if(i.framebuffer)vkDestroyFramebuffer(device,i.framebuffer,nullptr);if(i.pass)vkDestroyRenderPass(device,i.pass,nullptr);if(i.sampler)vkDestroySampler(device,i.sampler,nullptr);if(i.view)vkDestroyImageView(device,i.view,nullptr);if(i.handle)vmaDestroyImage(allocator,i.handle,i.allocation);i={};}
    bool owned(size_t owner,MC_RenderHandle id,uint32_t kind) const {
        auto it=Plugins::content().extensions.renderResources.find(id);return it!=Plugins::content().extensions.renderResources.end()&&it->second.owner==owner&&it->second.descriptor.kind==kind;
    }
    void init() {
        std::array<VkDescriptorSetLayoutBinding,2> bindings{};
        bindings[0]={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};bindings[1]={1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo sl{};sl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;sl.bindingCount=2;sl.pBindings=bindings.data();ok(vkCreateDescriptorSetLayout(device,&sl,nullptr,&setLayout));
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,80};VkPipelineLayoutCreateInfo pl{};pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pl.setLayoutCount=1;pl.pSetLayouts=&setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;ok(vkCreatePipelineLayout(device,&pl,nullptr,&layout));
        const uint8_t pixel[]={255,255,255,255};white=image(1,1,pixel,false);zero=buffer(nullptr,16384,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        for(const auto& entry:Plugins::content().extensions.renderResources) {
            const auto id=entry.first;const auto& r=entry.second;const auto& d=r.descriptor;
            if(d.kind==MC_RENDER_SHADER){std::vector<uint32_t> words(r.data.size()/4);std::memcpy(words.data(),r.data.data(),r.data.size());VkShaderModuleCreateInfo module{};module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module.codeSize=r.data.size();module.pCode=words.data();ok(vkCreateShaderModule(device,&module,nullptr,&shaders[id]));}
            if(d.kind==MC_RENDER_TEXTURE||d.kind==MC_RENDER_TARGET)images[id]=image(d.width,d.height,d.kind==MC_RENDER_TEXTURE?r.data.data():nullptr,d.kind==MC_RENDER_TARGET);
            if(d.kind==MC_RENDER_MESH){auto& m=meshes[id];m.vertices=buffer(r.data.data(),r.data.size(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);m.indices=buffer(r.indices.data(),r.indices.size()*4,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);m.count=static_cast<uint32_t>(r.indices.size());}
            if(d.kind==MC_RENDER_BUFFER)for(auto& b:buffers[id])b=buffer(r.data.data(),r.data.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        }
    }
    void invalidate(){for(auto& p:pipelines)vkDestroyPipeline(device,p.second,nullptr);pipelines.clear();for(auto& p:pools){if(p)vkDestroyDescriptorPool(device,p,nullptr);p=VK_NULL_HANDLE;}descriptorPool=VK_NULL_HANDLE;++revision;}
    ~State(){invalidate();for(auto& frameBuffers:posedBuffers)for(auto& b:frameBuffers)destroy(b);for(auto& m:meshes){destroy(m.second.vertices);destroy(m.second.indices);}for(auto& b:buffers)for(auto& frameBuffer:b.second)destroy(frameBuffer);for(auto& i:images)destroy(i.second);for(auto& s:shaders)vkDestroyShaderModule(device,s.second,nullptr);destroy(white);destroy(zero);if(layout)vkDestroyPipelineLayout(device,layout,nullptr);if(setLayout)vkDestroyDescriptorSetLayout(device,setLayout,nullptr);}
    VkPipeline pipeline(const Request& request,VkRenderPass pass,VkSampleCountFlagBits samples,uint32_t colors,bool depthOnly) {
        const auto key=std::make_tuple(request.pipeline,pass,request.mesh.stride,static_cast<uint32_t>(samples),colors);
        auto found=pipelines.find(key);if(found!=pipelines.end())return found->second;
        const auto& d=Plugins::content().extensions.renderResources.at(request.pipeline).descriptor;
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=shaders.at(d.vertex_shader);stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=shaders.at(d.fragment_shader);
        VkVertexInputBindingDescription binding{0,request.mesh.stride,VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,2> attributes{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,request.mesh.uvOffset}}};
        VkPipelineVertexInputStateCreateInfo vertex{};vertex.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.vertexAttributeDescriptionCount=2;vertex.pVertexAttributeDescriptions=attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo multi{};multi.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multi.rasterizationSamples=samples;
        VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=d.depth_test;depth.depthWriteEnable=d.depth_test&&!d.blend;depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        std::vector<VkPipelineColorBlendAttachmentState> attachments(colors);for(size_t i=0;i<attachments.size();++i){auto& a=attachments[i];a.colorWriteMask=i==0?15:0;a.blendEnable=i==0&&d.blend;a.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;a.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;a.colorBlendOp=VK_BLEND_OP_ADD;a.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;a.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;a.alphaBlendOp=VK_BLEND_OP_ADD;}
        VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;blend.attachmentCount=colors;blend.pAttachments=attachments.data();
        const VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamicStates;
        VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.stageCount=depthOnly?1:2;info.pStages=stages.data();info.pVertexInputState=&vertex;info.pInputAssemblyState=&assembly;info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&multi;info.pDepthStencilState=&depth;info.pColorBlendState=&blend;info.pDynamicState=&dynamic;info.layout=layout;info.renderPass=pass;
        VkPipeline result=VK_NULL_HANDLE;const auto status=vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&result);
        if(status!=VK_SUCCESS){if(result)vkDestroyPipeline(device,result,nullptr);ok(status);}
        pipelines[key]=result;return result;
    }
    void record(VkCommandBuffer cmd,VkRenderPass pass,VkSampleCountFlagBits samples,uint32_t stage,uint32_t colors,bool depthOnly,MC_RenderHandle target=0,int phase=0) {
        for(const auto& request:requests[stage]) {
            if(request.target!=target||(phase==1&&!request.mesh.lod)||(phase==2&&request.mesh.lod))continue;
            const auto& descriptor=Plugins::content().extensions.renderResources.at(request.pipeline).descriptor;
            const Image& texture=descriptor.texture==MC_RENDER_SCENE_COLOR?sceneColor:descriptor.texture?images.at(descriptor.texture):white;
            VkDescriptorSet set;VkDescriptorSetAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate.descriptorPool=descriptorPool;allocate.descriptorSetCount=1;allocate.pSetLayouts=&setLayout;ok(vkAllocateDescriptorSets(device,&allocate,&set));
            const Buffer& b=request.uniforms?buffers.at(request.uniforms)[frame]:zero;
            VkDescriptorImageInfo imageInfo{texture.sampler,texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkDescriptorBufferInfo bufferInfo{b.handle,0,request.uniforms?Plugins::content().extensions.renderResources.at(request.uniforms).data.size():16384};
            std::array<VkWriteDescriptorSet,2> writes{};for(auto& w:writes){w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.descriptorCount=1;}writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[0].pImageInfo=&imageInfo;writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[1].pBufferInfo=&bufferInfo;
            vkUpdateDescriptorSets(device,2,writes.data(),0,nullptr);
            if(request.mesh.viewport.width>0){vkCmdSetViewport(cmd,0,1,&request.mesh.viewport);vkCmdSetScissor(cmd,0,1,&request.mesh.scissor);}
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(request,pass,samples,colors,depthOnly));vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
            vkCmdBindVertexBuffers(cmd,0,1,&request.mesh.vertex,&request.mesh.vertexOffset);vkCmdBindIndexBuffer(cmd,request.mesh.index,0,VK_INDEX_TYPE_UINT32);
            struct Push {glm::mat4 mvp;glm::vec4 tint;} push{clipSpaceCorrection()*request.mesh.transform,request.mesh.tint};
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
            vkCmdDrawIndexed(cmd,request.mesh.count,1,request.mesh.first,request.mesh.base,0);
        }
    }
};
VulkanPluginBackend::VulkanPluginBackend(VkDevice d,VmaAllocator a,VkCommandPool p,VkQueue q):state(std::make_unique<State>(d,a,p,q)){state->init();}
VulkanPluginBackend::~VulkanPluginBackend()=default;
void VulkanPluginBackend::invalidate(){state->invalidate();}
void VulkanPluginBackend::prepare(uint32_t frameIndex,uint32_t width,uint32_t height,double time,const glm::mat4& vp,const glm::dvec3& origin,VkImageView source,VkSampler sampler,const std::array<std::vector<PluginSceneSubmission>,MC_RENDER_STAGE_COUNT>& scene) {
    auto& s=*state;s.frame=frameIndex;s.descriptorPool=s.pools[frameIndex];s.sceneColor.view=source;s.sceneColor.sampler=sampler;s.scene=scene;s.viewProjection=vp;s.width=width;s.height=height;s.replacement={};for(auto& r:s.requests)r.clear();
    for(auto& b:s.posedBuffers[frameIndex])s.destroy(b);
    s.posedBuffers[frameIndex].clear();size_t posedBytes=0;
    for(auto& stage:s.scene)for(auto& mesh:stage)if(!mesh.posedVertices.empty()) {
        posedBytes+=mesh.posedVertices.size()*sizeof(MC_RenderVertex);
        if(posedBytes>Config::PLUGIN_RENDER_DATA_BUDGET)throw std::runtime_error("Plugin posed scene budget exceeded");
        s.posedBuffers[frameIndex].push_back(s.buffer(mesh.posedVertices.data(),mesh.posedVertices.size()*sizeof(MC_RenderVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
        mesh.vertex=s.posedBuffers[frameIndex].back().handle;mesh.vertexOffset=0;mesh.base=0;
        mesh.stride=sizeof(MC_RenderVertex);mesh.uvOffset=offsetof(MC_RenderVertex,uv);mesh.posedVertices.clear();
    }
    // The fence for this frame has completed before its buffer/pool is reused.
    if(s.descriptorPool)ok(vkResetDescriptorPool(s.device,s.descriptorPool,0));
    else {std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4096},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4096}}};VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=4096;pool.poolSizeCount=2;pool.pPoolSizes=sizes.data();ok(vkCreateDescriptorPool(s.device,&pool,nullptr,&s.descriptorPool));s.pools[frameIndex]=s.descriptorPool;}
    for(uint32_t stage=0;stage<MC_RENDER_STAGE_COUNT;++stage){s.activeStage=stage;MC_RenderFrame frame{};frame.size=sizeof(frame);frame.stage=stage;frame.width=width;frame.height=height;frame.time=time;for(int i=0;i<3;++i)frame.origin[i]=origin[i];frame.resource_revision=s.revision;frame.scene_count=scene[stage].size();std::memcpy(frame.view_projection,glm::value_ptr(vp),64);
        for(const auto& p:Plugins::content().extensions.renderPasses)if(p.stage==stage){p.callback(frame);s.replacement[stage]=s.replacement[stage]||p.replace;}}
    s.activeStage=MC_RENDER_STAGE_COUNT;
}
bool VulkanPluginBackend::draw(size_t owner,const MC_RenderDraw& d) {
    auto& s=*state;if(s.activeStage>=MC_RENDER_STAGE_COUNT||s.requests[s.activeStage].size()>=512||!s.owned(owner,d.mesh,MC_RENDER_MESH)||!s.owned(owner,d.pipeline,MC_RENDER_PIPELINE)||
        (d.target&&!s.owned(owner,d.target,MC_RENDER_TARGET))||(d.uniforms&&!s.owned(owner,d.uniforms,MC_RENDER_BUFFER)))return false;
    for(float x:d.transform)if(!std::isfinite(x))return false;
    for(float x:d.tint)if(!std::isfinite(x))return false;
    const auto& m=s.meshes.at(d.mesh);PluginSceneSubmission mesh;mesh.vertex=m.vertices.handle;mesh.index=m.indices.handle;mesh.count=m.count;
    if(d.clip_space>1)return false;
    const auto input=Plugins::content().extensions.renderResources.at(d.pipeline).descriptor.texture;
    if(input==MC_RENDER_SCENE_COLOR&&(s.activeStage!=MC_RENDER_POST||d.target))return false;
    mesh.transform=(d.clip_space?glm::mat4(1):s.viewProjection)*glm::make_mat4(d.transform);mesh.tint=glm::make_vec4(d.tint);
    s.requests[s.activeStage].push_back({d.pipeline,d.target,d.uniforms,mesh});return true;
}
bool VulkanPluginBackend::sceneDraw(size_t owner,uint64_t index,MC_RenderHandle pipeline) {
    auto& s=*state;if(s.activeStage>=MC_RENDER_STAGE_COUNT||index>=s.scene[s.activeStage].size()||!s.owned(owner,pipeline,MC_RENDER_PIPELINE)||s.requests[s.activeStage].size()>=512)return false;
    if(Plugins::content().extensions.renderResources.at(pipeline).descriptor.texture==MC_RENDER_SCENE_COLOR&&s.activeStage!=MC_RENDER_POST)return false;
    s.requests[s.activeStage].push_back({pipeline,0,0,s.scene[s.activeStage][index]});return true;
}
bool VulkanPluginBackend::updateBuffer(size_t owner,MC_RenderHandle handle,const void* data,uint32_t bytes) {
    auto& s=*state;if(s.activeStage>=MC_RENDER_STAGE_COUNT||!data||!s.owned(owner,handle,MC_RENDER_BUFFER)||bytes!=Plugins::content().extensions.renderResources.at(handle).data.size())return false;
    std::memcpy(s.buffers.at(handle)[s.frame].mapped,data,bytes);ok(vmaFlushAllocation(s.allocator,s.buffers.at(handle)[s.frame].allocation,0,bytes));return true;
}
bool VulkanPluginBackend::replaces(uint32_t stage) const {return state->replacement.at(stage);}
void VulkanPluginBackend::record(VkCommandBuffer cmd,VkRenderPass pass,VkSampleCountFlagBits samples,uint32_t stage,uint32_t colors,bool depthOnly,int phase){state->record(cmd,pass,samples,stage,colors,depthOnly,0,phase);}
void VulkanPluginBackend::targets(VkCommandBuffer cmd) {
    auto& s=*state;std::set<MC_RenderHandle> done,visiting;
    std::function<void(MC_RenderHandle)> render=[&](MC_RenderHandle target){if(done.count(target))return;if(!visiting.insert(target).second)throw std::runtime_error("Plugin render target dependency cycle");
        for(const auto& stage:s.requests)for(const auto& request:stage)if(request.target==target){const auto input=Plugins::content().extensions.renderResources.at(request.pipeline).descriptor.texture;if(input&&input!=MC_RENDER_SCENE_COLOR&&s.images.at(input).pass)render(input);}
        auto& image=s.images.at(target);VkClearValue clear{};VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=image.pass;begin.framebuffer=image.framebuffer;begin.renderArea.extent={image.width,image.height};begin.clearValueCount=1;begin.pClearValues=&clear;vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0,0,static_cast<float>(image.width),static_cast<float>(image.height),0,1};const VkRect2D scissor{{0,0},{image.width,image.height}};vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);
        for(uint32_t stage=0;stage<MC_RENDER_STAGE_COUNT;++stage)s.record(cmd,image.pass,VK_SAMPLE_COUNT_1_BIT,stage,1,false,target);
        vkCmdEndRenderPass(cmd);done.insert(target);visiting.erase(target);};
    for(const auto& image:s.images)if(image.second.pass)render(image.first);
}
} // namespace vkp

void vkp::VulkanPluginBackend::validate(VkRenderPass scene,VkRenderPass present,VkRenderPass shadow,VkSampleCountFlagBits samples,uint32_t colors) {
    auto& s=*state;std::map<MC_RenderHandle,std::set<MC_RenderHandle>> dependencies;
    for(uint32_t stage=0;stage<MC_RENDER_STAGE_COUNT;++stage)for(const auto& request:s.requests[stage]) {
        if(!request.target&&stage==MC_RENDER_SHADOWS&&!shadow)throw std::runtime_error("Plugin shadow drawing requires enabled shadows");
        const auto input=Plugins::content().extensions.renderResources.at(request.pipeline).descriptor.texture;
        if(request.target&&input&&input!=MC_RENDER_SCENE_COLOR&&s.images.at(input).pass)dependencies[request.target].insert(input);
        const auto pass=request.target?s.images.at(request.target).pass:stage==MC_RENDER_POST?present:stage==MC_RENDER_SHADOWS?shadow:scene;
        const bool shadowOnly=!request.target&&stage==MC_RENDER_SHADOWS;
        s.pipeline(request,pass,request.target||stage==MC_RENDER_POST||shadowOnly?VK_SAMPLE_COUNT_1_BIT:samples,shadowOnly?0:request.target||stage==MC_RENDER_POST?1:colors,shadowOnly);
    }
    std::set<MC_RenderHandle> visited,visiting;
    std::function<void(MC_RenderHandle)> visit=[&](MC_RenderHandle id){if(visited.count(id))return;if(!visiting.insert(id).second)throw std::runtime_error("Plugin render target dependency cycle");for(auto next:dependencies[id])visit(next);visiting.erase(id);visited.insert(id);};
    for(const auto& image:s.images)if(image.second.pass)visit(image.first);
}
