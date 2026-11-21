#include "ui/UIRenderer.h"

#include <stdexcept>
#include <utility>

UIRenderer::~UIRenderer() = default;

void UIRenderer::initialize(std::unique_ptr<IUIRenderBackend> backend) {
    if (!backend) throw std::invalid_argument("UI renderer requires a backend");
    backend->setLocalization(m_localization);
    m_backend = std::move(backend);
}

void UIRenderer::resetGraphics() { m_backend.reset(); }
void UIRenderer::setLocalization(const Localization& value) {
    m_localization = &value;
    if (m_backend) m_backend->setLocalization(m_localization);
}
void UIRenderer::beginUIFrame(int w,int h){m_uiWidth=w;m_uiHeight=h;m_backend->beginUIFrame(w,h); }
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

void UIRenderer::drawRoundedRect(float x,float y,float w,float h,float r,const glm::vec4& c){m_backend->drawRoundedRect(x,y,w,h,r,c);}
void UIRenderer::setOpacity(float a){m_backend->setOpacity(a);}
void UIRenderer::renderTextAlpha(const std::string& s,float x,float y,float z,const glm::vec3& c,float a){m_backend->renderTextAlpha(s,x,y,z,c,a);}

UiFrameStats UIRenderer::frameStats() const { return m_backend->frameStats(); }
