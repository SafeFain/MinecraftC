#include "ui/UIGeometry.h"
#include "ui/UIColor.h"
#include "ui/UILayout.h"
#include "ui/UIStyle.h"
#include "ui/SettingsMenu.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition,const char* message) {
    if (!condition) { std::cerr<<"FAILED: "<<message<<'\n';std::exit(1); }
}
struct Rect { float x,y,w,h; };
}

int main() {
    const auto linear=uiLinearColor({.5f,.04045f,1,.6f},.5f);
    require(std::abs(linear.r-.214041f)<.00001f&&std::abs(linear.g-.0031308f)<.00001f&&
            linear.b==1&&linear.a==.3f,"authored sRGB and opacity convert exactly once");
    std::vector<UiMeshVertex> vertices;
    std::vector<uint32_t> indices;
    appendRoundedRect(vertices,indices,10,20,200,48,6,{1,1,1,.6f});
    require(vertices.size()==73 && indices.size()==324,"rounded geometry is bounded");
    for (uint32_t index:indices) require(index<vertices.size(),"valid rounded indices");
    for (const auto& v:vertices) {
        require(std::isfinite(v.position.x)&&std::isfinite(v.position.y),"finite rounded vertices");
        require(v.position.x>=9.24f&&v.position.x<=210.76f&&
                v.position.y>=19.24f&&v.position.y<=68.76f,"rounded coverage stays near bounds");
        require(v.color.a==0 || std::abs(v.color.a-.6f)<.001f,"true alpha coverage");
    }
    const size_t before=vertices.size();
    appendRoundedRect(vertices,indices,0,0,-1,0,6,{1,1,1,1});
    require(vertices.size()==before,"invalid rectangles produce no geometry");
    vertices.clear();indices.clear();
    appendRoundedRect(vertices,indices,0,0,2,1,10,{1,1,1,1});
    for (const auto& v:vertices) require(std::isfinite(v.position.x),"tiny rounded surfaces are safe");

    for (auto size:{glm::vec2(960,600),glm::vec2(320,640),glm::vec2(640,240),glm::vec2(240,320)}) {
        for (auto design:{glm::vec2(600,470),glm::vec2(460,480),glm::vec2(460,560),glm::vec2(360,400)}) {
            const UiCanvasFit fit(size.x,size.y,design.x,design.y);
            const Rect panel=fit.transform(Rect{0,0,design.x,design.y});
            require(panel.x>=11.9f&&panel.y>=11.9f&&panel.x+panel.w<=size.x-11.9f&&
                    panel.y+panel.h<=size.y-11.9f,"fitted screen respects all margins");
            const Rect slot=fit.transform(Rect{20,40,44,44});
            const glm::vec2 center{slot.x+slot.w*.5f,slot.y+slot.h*.5f};
            require(center.x>=slot.x&&center.x<=slot.x+slot.w&&
                    center.y>=slot.y&&center.y<=slot.y+slot.h,"scaled hit geometry contains rendered center");
        }
        const UiHotbarLayout bar(size.x);
        require(bar.x>=7.9f&&bar.x+bar.width<=size.x-7.9f,"nine hotbar slots fit narrow screens");
    }
    const auto narrow=settingsButtonLayout(320,550,13,true,true);
    require(narrow.columns==1&&narrow.rowCount==13,"portrait settings use a single column");
    require(settingsGridNeighbor(3,13,0,1,true,1)==4&&
            settingsGridNeighbor(0,13,0,-1,true,1)==12&&
            settingsGridNeighbor(3,13,1,0,true,1)==3,"single-column focus follows visible order");
    const auto shortLayout=settingsButtonLayout(320,190,13,false,true);
    require(shortLayout.visibleRows<shortLayout.rowCount,"short settings scroll rather than overflow");
    const auto last=settingsButtonPosition(shortLayout,12,13,true,
        static_cast<int>(shortLayout.rowCount-shortLayout.visibleRows));
    require(last.y>=10,"scrolled Back remains visible");

    require(uiTextLineCapacity(240,18+86+8,25)==5 &&
            uiTextLineCapacity(80,18+86+8,25)==0,
            "chat history fits the space above multiline input and keyboard");
    UiTransition feedback;
    for (int i=0;i<10;++i) feedback.tick(.01f,true,.1f);
    require(feedback.value>.999f,"feedback reaches its duration independent of frame rate");
    feedback.tick(.05f,false,.1f);
    require(std::abs(feedback.value-.5f)<.001f,"feedback reverses without a discontinuity");
    feedback.tick(-1,true,.1f);
    require(std::abs(feedback.value-.5f)<.001f,"negative time does not advance animation");
    std::cout<<"Modern UI geometry, responsive layout and transitions passed\n";
}
