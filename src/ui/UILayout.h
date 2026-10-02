#pragma once

#include "Config.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

struct UiCanvasFit {
    float scale;
    float x;
    float y;
    UiCanvasFit(float width,float height,float designWidth,float designHeight,
                float margin=12.0f)
        : scale(std::max(0.01f,std::min({1.0f,(width-2*margin)/designWidth,
                                        (height-2*margin)/designHeight}))),
          x((width-designWidth*scale)*.5f), y((height-designHeight*scale)*.5f) {}
    template<class Rect> Rect transform(Rect r) const {
        return {x+r.x*scale,y+r.y*scale,r.w*scale,r.h*scale};
    }
};

struct UiHotbarLayout {
    float scale,slot,gap,padX,padY,x,y,width,height;
    explicit UiHotbarLayout(float screenWidth) {
        const float design=9*Config::HOTBAR_SLOT_SIZE+8*Config::HOTBAR_GAP+
                           2*Config::HOTBAR_PAD_X;
        scale=std::min(1.0f,std::max(1.0f,screenWidth-16)/design);
        slot=Config::HOTBAR_SLOT_SIZE*scale;gap=Config::HOTBAR_GAP*scale;
        padX=Config::HOTBAR_PAD_X*scale;padY=Config::HOTBAR_PAD_Y*scale;
        width=design*scale;height=slot+padY*2;x=(screenWidth-width)*.5f;y=4;
    }
};

// Time-based feedback has no wall-clock/global state and never gates actions.
struct UiTransition {
    float value=0;
    void tick(float dt,bool target,float duration) {
        const float step=std::max(0.0f,dt)/std::max(0.001f,duration);
        value=target?std::min(1.0f,value+step):std::max(0.0f,value-step);
    }
};

inline int uiTextLineCapacity(float height,float reserved,float lineHeight) {
    if (!(lineHeight>0)) return 0;
    return static_cast<int>(std::max(0.0f,std::floor((height-reserved)/lineHeight)));
}
