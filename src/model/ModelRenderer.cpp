#include "model/ModelRenderer.h"

#include <stdexcept>

namespace model {

ModelRenderer::ModelRenderer(std::unique_ptr<IModelRenderBackend> backend)
    : m_backend(std::move(backend)) {
    if (!m_backend) throw std::invalid_argument("Model renderer backend is null");
}

ModelRenderer::~ModelRenderer() = default;

ModelHandle ModelRenderer::upload(std::shared_ptr<const ModelAsset> asset) {
    return m_backend->upload(std::move(asset));
}

void ModelRenderer::queue(const ModelDraw& draw) { m_backend->queue(draw); }

void ModelRenderer::flushOpaque(const glm::mat4& vp,
    const RenderEnvironment& environment, const glm::vec3& camera,
    float fogStart, float fogEnd) {
    m_backend->flushOpaque(vp, environment, camera, fogStart, fogEnd);
}

void ModelRenderer::flushBlend(const glm::mat4& vp,
    const RenderEnvironment& environment, const glm::vec3& camera,
    float fogStart, float fogEnd) {
    m_backend->flushBlend(vp, environment, camera, fogStart, fogEnd);
}

void ModelRenderer::clear() { m_backend->clear(); }

} // namespace model
