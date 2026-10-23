#include "renderer/backend/vulkan/VulkanRenderer.h"
#include "renderer/backend/vulkan/VulkanRendererInternal.h"

#include "model/ModelRenderer.h"
#include "model/ModelRenderLogic.h"
#include "model/ModelTexture.h"
#include <atomic>

#include <stdexcept>
#include <utility>
#include <vector>

namespace model {
namespace {
uint32_t nextModelGeneration() {
    static std::atomic<uint32_t> next{1};
    const auto value=next.fetch_add(1);
    if(!value) throw std::runtime_error("model generation space exhausted");
    return value;
}
}

class VulkanModelBackend final : public IModelRenderBackend {
public:
    explicit VulkanModelBackend(VulkanRenderer& renderer) : m_renderer(renderer) {
        m_renderer.m_impl->modelGeneration=nextModelGeneration();
    }
    ~VulkanModelBackend() override { clear(); }

    ModelHandle upload(std::shared_ptr<const ModelAsset> asset) override {
        if (!asset) throw std::invalid_argument("cannot upload a null model asset");
        auto& impl = *m_renderer.m_impl;
        for(size_t i=0;i<impl.models.size();++i) if(impl.models[i].asset==asset)
            return {static_cast<uint32_t>(i+1),impl.modelGeneration};
        if(impl.models.size()>=UINT32_MAX) throw std::runtime_error("model handle space exhausted");
        impl.models.reserve(impl.models.size()+1);
        VulkanRenderer::Impl::ModelResource resource;
        resource.asset = std::move(asset);
        try {
            resource.primitives.reserve(resource.asset->primitives.size());
            const auto textureCount=resource.asset->textures.empty()?resource.asset->images.size():resource.asset->textures.size();
            resource.textures.reserve(textureCount);resource.textureMaterials.reserve(textureCount);
            auto prepared=std::make_shared<ModelAsset>(*resource.asset);
            std::string error;
            if(!prepareAsset(*prepared,error)) throw std::invalid_argument(error);
            resource.bindings=prepared->bindings;
            for(const auto& binding:resource.bindings) {
                const int material=resource.asset->primitives[binding.primitive].material;
                const bool blend=material>=0 && resource.asset->materials[material].alphaMode==AlphaMode::Blend;
                resource.hasBlend|=blend;resource.hasOpaque|=!blend;
            }
            resource.bindInstance=std::make_shared<ModelInstance>();
            resource.bindInstance->pose=bindPose(*prepared);composeGlobals(*prepared,resource.bindInstance->pose);
            updatePalettes(*prepared,*resource.bindInstance);
            auto uploadTexture=[&](int image,const Sampler& sampler) {
                if(image<0 || static_cast<size_t>(image)>=resource.asset->images.size()) throw std::invalid_argument("invalid model texture image");
                const auto textureHandle=m_renderer.createTexture(modelTexture(resource.asset->images[image],sampler),textureSampler(sampler));
                resource.textures.push_back(textureHandle);
                MaterialDesc material;material.pipeline=MaterialPipeline::UiTextured;material.baseColorTexture=textureHandle;
                resource.textureMaterials.push_back(m_renderer.createMaterial(material));
            };
            if(resource.asset->textures.empty()) {
                Sampler legacy;legacy.wrapS=legacy.wrapT=33071;
                for(size_t i=0;i<resource.asset->images.size();++i) uploadTexture(static_cast<int>(i),legacy);
            } else for(const auto& texture:resource.asset->textures) {
                const Sampler fallback;
                uploadTexture(texture.image,texture.sampler>=0?resource.asset->samplers.at(texture.sampler):fallback);
            }
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

                resource.primitives.push_back(std::move(primitive));
            }
        } catch (...) {
            destroy(resource);
            throw;
        }
        impl.models.push_back(std::move(resource));
        return {static_cast<uint32_t>(impl.models.size()),impl.modelGeneration};
    }

    void queue(const ModelDraw& draw) override {
        auto& impl=*m_renderer.m_impl;
        if(!draw.model || draw.model.generation!=impl.modelGeneration || draw.model.value>impl.models.size()) return;
        ModelDraw owned=draw;
        if(draw.instance) {
            std::shared_ptr<ModelInstance> snapshot;
            while(m_snapshotCursor<m_snapshots.size()) {
                auto& candidate=m_snapshots[m_snapshotCursor++];
                if(candidate.use_count()==1) {snapshot=candidate;break;}
            }
            if(!snapshot) {snapshot=std::make_shared<ModelInstance>();m_snapshots.push_back(snapshot);}
            snapshot->pose.global=draw.instance->pose.global;
            snapshot->jointPalettes=draw.instance->jointPalettes;
            snapshot->revision=draw.instance->revision;
            owned.snapshot=std::move(snapshot);
        } else owned.snapshot=impl.models[draw.model.value-1].bindInstance;
        owned.instance=nullptr;
        m_draws.push_back(std::move(owned));
    }

    void setBatching(bool enabled,size_t pageBytes) override {
        m_renderer.m_impl->modelBatching=enabled;
        m_renderer.m_impl->modelPageBytes=pageBytes;
    }

    void flushOpaque(const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) override {
        append(m_renderer.m_impl->submittedModelOpaque, vp, environment,
               camera, fogStart, fogEnd, false);
    }

    void flushBlend(const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd) override {
        append(m_renderer.m_impl->submittedModelBlend, vp, environment,
               camera, fogStart, fogEnd, true);
        auto& blended=m_renderer.m_impl->submittedModelBlend;
        std::stable_sort(blended.begin(),blended.end(),[](const auto& a,const auto& b) {
            return a.draw.distanceSquared>b.draw.distanceSquared;
        });
        m_draws.clear();
        m_snapshotCursor=0;
        if (!m_renderer.m_impl->submittedModelOpaque.empty() ||
            !m_renderer.m_impl->submittedModelBlend.empty())
            m_renderer.m_impl->drawQueued = true;
    }

    void clear() override {
        m_draws.clear();
        if (!m_renderer.m_impl) return;
        // clear is a rare world/renderer lifecycle operation. Wait for every
        // in-flight use before releasing geometry, textures or descriptors.
        m_renderer.waitIdle();
        m_renderer.m_impl->submittedModelOpaque.clear();
        m_renderer.m_impl->submittedModelBlend.clear();
        m_renderer.m_impl->modelGeneration=nextModelGeneration();
        m_snapshots.clear();
        m_snapshotCursor=0;
        for (auto& resource : m_renderer.m_impl->models) destroy(resource);
        m_renderer.m_impl->models.clear();
    }

private:
    VulkanRenderer& m_renderer;
    std::vector<ModelDraw> m_draws;
    std::vector<std::shared_ptr<ModelInstance>> m_snapshots;
    size_t m_snapshotCursor=0;

    void append(std::vector<VulkanRenderer::Impl::ModelPassSubmission>& target,
        const glm::mat4& vp, const RenderEnvironment& environment,
        const glm::vec3& camera, float fogStart, float fogEnd, bool blended) {
        for (const ModelDraw& draw : m_draws) {
            const auto& resource=m_renderer.m_impl->models[draw.model.value-1];
            if(blended?resource.hasBlend:resource.hasOpaque)
                target.push_back({draw, vp, environment, camera, fogStart, fogEnd});
        }

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
    ::VulkanRenderer& renderer, std::shared_ptr<ModelAssetCache> assets) {
    return std::make_unique<ModelRenderer>(
        std::make_unique<VulkanModelBackend>(renderer),std::move(assets));
}

} // namespace model
