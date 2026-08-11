#include "ui/UIRenderer.h"
#include "renderer/GameRenderer.h"

#include <stdexcept>

UIRenderer::~UIRenderer() = default;

void UIRenderer::initialize(IGameRenderer& renderer,
    RenderTextureHandle atlas, bool srgb, const std::filesystem::path& root,
    GraphicsApi api) {
#if defined(MINECRAFTC_ENABLE_VULKAN)
    if (api == GraphicsApi::Vulkan)
        m_backend = createVulkanUIBackend(renderer, atlas, root);
#if defined(MINECRAFTC_ENABLE_OPENGL)
    else
#endif
#else
    (void)renderer;
#endif
#if defined(MINECRAFTC_ENABLE_OPENGL)
        m_backend = createOpenGLUIBackend(atlas, srgb, root, api);
#else
    (void)srgb;
    (void)api;
#endif
    if (!m_backend) throw std::runtime_error("Could not create UI renderer backend");
    m_backend->setLocalization(m_localization);
}

void UIRenderer::reinitialize(IGameRenderer& renderer,
    RenderTextureHandle atlas, bool srgb, const std::filesystem::path& root,
    GraphicsApi api) {
    resetGraphics();
    initialize(renderer, atlas, srgb, root, api);
}

void UIRenderer::resetGraphics() { m_backend.reset(); }
void UIRenderer::setLocalization(const Localization& value) {
    m_localization = &value;
    if (m_backend) m_backend->setLocalization(m_localization);
}
void UIRenderer::beginUIFrame(int w,int h){m_backend->beginUIFrame(w,h);}
void UIRenderer::setCanvas(float x,float y,float w,float h){m_backend->setCanvas(x,y,w,h);}
void UIRenderer::endUIFrame(){m_backend->endUIFrame();}
void UIRenderer::drawRect(float x,float y,float w,float h,const glm::vec4& c){m_backend->drawRect(x,y,w,h,c);}
void UIRenderer::drawBlockIcon(float x,float y,float w,float h,BlockId b){m_backend->drawBlockIcon(x,y,w,h,b);}
void UIRenderer::drawItemIcon(float x,float y,float w,float h,const ItemStack& s){m_backend->drawItemIcon(x,y,w,h,s);}
void UIRenderer::drawDurability(float x,float y,float w,const ItemStack& s){m_backend->drawDurability(x,y,w,s);}
void UIRenderer::drawPanel(float x,float y,float w,float h,const glm::vec4& c){m_backend->drawPanel(x,y,w,h,c);}
void UIRenderer::drawTooltip(float x,float y,const ItemStack& s){m_backend->drawTooltip(x,y,s);}
void UIRenderer::renderText(const std::string& s,float x,float y,float z,const glm::vec3& c){m_backend->renderText(s,x,y,z,c);}
glm::vec2 UIRenderer::measureText(const std::string& s,float z){return m_backend->measureText(s,z);}
