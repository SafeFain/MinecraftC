#pragma once

#include "ui/UIStyle.h"

namespace UiTheme {

// Original five-by-seven block lettering, independent of the UI body font.
// Rows run from the top down; each occupied cell becomes solid UI geometry.
inline constexpr std::array<std::array<uint8_t,7>,10> MENU_WORDMARK{{
    {{17,27,21,21,17,17,17}}, // M
    {{31,4,4,4,4,4,31}},     // I
    {{17,25,25,21,19,19,17}}, // N
    {{31,16,16,30,16,16,31}}, // E
    {{15,16,16,16,16,16,15}}, // C
    {{30,17,17,30,20,18,17}}, // R
    {{14,17,17,31,17,17,17}}, // A
    {{31,16,16,30,16,16,16}}, // F
    {{31,4,4,4,4,4,4}},      // T
    {{15,16,16,16,16,16,15}}  // C
}};

// Returns the bottom of the complete brand, for menu content clearance.
template <class T>
inline float mainMenuBrand(T& ui,float x,float top,float width,
                           const std::string& subtitle,bool compact) {
    if (width<=8) return top;
    const float cell=std::min(compact?4.5f:8.0f,width/60.0f);
    const float left=x+cell*.2f,bottom=top-cell*7.2f;
    // Draw complete silhouettes before faces so adjacent cells have no seams.
    for (int pass=0;pass<3;++pass) {
        for (size_t letter=0;letter<MENU_WORDMARK.size();++letter) {
            for (int row=0;row<7;++row) for (int col=0;col<5;++col) {
                if (!(MENU_WORDMARK[letter][static_cast<size_t>(row)]&(16>>col))) continue;
                const float px=left+(static_cast<float>(letter)*6+col)*cell;
                const float py=bottom+(6-row)*cell;
                if (pass==0) {
                    rect(ui,px+cell*.55f,py-cell*.65f,cell,cell,{.07f,.16f,.18f,1});
                } else if (pass==1) {
                    const float edge=cell*.16f;
                    rect(ui,px-edge,py-edge,cell+edge*2,cell+edge*2,{.04f,.08f,.10f,1});
                } else {
                    const float shade=static_cast<float>(row)/6;
                    const auto color=letter+1==MENU_WORDMARK.size()
                        ? glm::mix(glm::vec4(.70f,.98f,.76f,1),glm::vec4(.26f,.65f,.49f,1),shade)
                        : glm::mix(glm::vec4(.94f,.97f,.91f,1),glm::vec4(.53f,.66f,.64f,1),shade);
                    rect(ui,px,py,cell,cell,color);
                }
            }
        }
    }
    const float nominalScale=compact?.95f:1.10f;
    const float tagWidth=std::min(width,ui.measureText(subtitle,nominalScale).x+36);
    const float scale=fittedScale(ui,subtitle,nominalScale,std::max(1.0f,tagWidth-32));
    const auto textSize=ui.measureText(subtitle,scale);
    const float tagHeight=std::max(20.0f,textSize.y+10);
    const float tagY=top-cell*8-8-tagHeight;
    rect(ui,x,tagY,tagWidth,tagHeight,{.06f,.14f,.18f,.94f});
    rect(ui,x,tagY+tagHeight-1,tagWidth,1,{.39f,.78f,.65f,1});
    rect(ui,x+7,tagY+tagHeight*.5f-3,6,6,{.48f,.86f,.64f,1});
    rect(ui,x+8,tagY+tagHeight*.5f,4,2,{.73f,.98f,.78f,1});
    ui.renderTextAlpha(subtitle,x+22,tagY+(tagHeight-textSize.y)*.5f,
                       scale,glm::vec3(.82f,.94f,.89f),1);
    return tagY;
}

} // namespace UiTheme
