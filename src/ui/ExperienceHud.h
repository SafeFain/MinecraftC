#pragma once
#include "game/Experience.h"
#include "ui/UIStyle.h"

// Shared by the gameplay HUD and real Vulkan readback regression.
template<class Renderer>
inline void renderExperienceHud(Renderer& renderer,const Experience& experience,int screenWidth) {
    const UiHotbarLayout bar(static_cast<float>(screenWidth));
    const float s=bar.scale;
    const float xpY=bar.y+bar.height+5*s;
    const float xpW=bar.width;
    renderer.drawRect(bar.x,xpY,xpW,5*s,glm::vec4(.035f,.045f,.015f,1));
    renderer.drawRect(bar.x+s,xpY+s,xpW-2*s,3*s,glm::vec4(.12f,.18f,.035f,1));
    const float filled=float(experience.progress())*(xpW-2*s);
    if(filled>0){renderer.drawRect(bar.x+s,xpY+s,filled,3*s,glm::vec4(.38f,.78f,.08f,1));
        renderer.drawRect(bar.x+s,xpY+3*s,filled,s,glm::vec4(.70f,1.f,.28f,1));}
    if(experience.level()>0){const auto label=std::to_string(experience.level());
        const float textScale=.95f*s;const auto size=renderer.measureText(label,textScale);
        UiTheme::textWithShadow(renderer,label,(screenWidth-size.x)*.5f,xpY+8*s,textScale,glm::vec4(.55f,1.f,.20f,1));}
}
