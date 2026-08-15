#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "renderer/backend/vulkan/VulkanRendererInternal.h"

#include "model/ModelRenderer.h"
#include "model/ModelRenderLogic.h"

#include <stdexcept>
#include <utility>
#include <vector>

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
