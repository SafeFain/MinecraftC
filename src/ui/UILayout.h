#pragma once

#include "Config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Each widget owns its feedback; rendering it twice in a frame must not speed
// up motion. A short pulse makes a tap/keyboard activation visible even when
// press and release happen between two rendered frames.
struct UiFeedback {
    UiTransition hover, focus, press, pulse;
    uint64_t frame = UINT64_MAX;

    void activate() { pulse.value = 1; }
    void sample(uint64_t serial, float dt, bool hovered, bool focused, bool pressed) {
        if (frame == serial) return;
        frame = serial;
        hover.tick(dt, hovered || focused || pressed, Config::UI_HOVER_SECONDS);
        focus.tick(dt, focused, Config::UI_FOCUS_SECONDS);
        press.tick(dt, pressed, pressed ? Config::UI_PRESS_SECONDS : Config::UI_RELEASE_SECONDS);
        pulse.tick(dt, false, Config::UI_ACTIVATE_SECONDS);
    }
    static float ease(float value) { return value * value * (3 - 2 * value); }
    float hoverAmount() const { return ease(hover.value); }
    float focusAmount() const { return ease(focus.value); }
    float pressAmount() const { return ease(std::max(press.value, pulse.value)); }
};

// Navigation uses actual fitted rectangle centers, including small touch
// canvases. Keeping an index lets focus follow its slot after a resize.
template<class Rects>
inline int uiDirectionalNeighbor(const Rects& rects,int current,int dx,int dy) {
    if (rects.empty()) return -1;
    current=std::clamp(current,0,static_cast<int>(rects.size())-1);
    if (!dx && !dy) return current;
    if (dy) dx=0;
    const auto& origin=rects[static_cast<size_t>(current)];
    const float x=origin.x+origin.w*.5f,y=origin.y+origin.h*.5f;
    float best=1e30f;
    int chosen=current;
    for (size_t i=0;i<rects.size();++i) {
        const auto& r=rects[i];
        const float vx=r.x+r.w*.5f-x,vy=r.y+r.h*.5f-y;
        if ((dx && vx*dx<=1) || (dy && vy*dy<=1)) continue;
        const float score=(dx?std::abs(vx):std::abs(vy))+2*(dx?std::abs(vy):std::abs(vx));
        if (score<best) { best=score;chosen=static_cast<int>(i); }
    }
    return chosen;
}

inline int uiTextLineCapacity(float height,float reserved,float lineHeight) {
    if (!(lineHeight>0)) return 0;
    return static_cast<int>(std::max(0.0f,std::floor((height-reserved)/lineHeight)));
}
