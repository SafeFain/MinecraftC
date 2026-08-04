#include "ui/Inventory.h"
#include "ui/UIRenderer.h"
#include "Config.h"
#include "game/Item.h"

#include <algorithm>
#include <cmath>

CreativeInventory::CreativeInventory() {
    for (const ItemId id : creativeInventoryItems())
        m_slots.push_back({id,0,0,false,false});
}

void CreativeInventory::layoutSlots(int width,int height) {
    constexpr float slot=44.0f,gap=5.0f,padding=14.0f,header=52.0f,footer=34.0f;
    m_columns=std::clamp(static_cast<int>((width-80.0f)/(slot+gap)),5,10);
    m_totalRows=(static_cast<int>(m_slots.size())+m_columns-1)/m_columns;
    m_visibleRows=std::clamp(static_cast<int>((height-150.0f)/(slot+gap)),2,7);
    m_visibleRows=std::min(m_visibleRows,m_totalRows);
    m_scrollRow=std::clamp(m_scrollRow,0,std::max(0,m_totalRows-m_visibleRows));
    const float gridW=m_columns*slot+(m_columns-1)*gap;
    const float gridH=m_visibleRows*slot+(m_visibleRows-1)*gap;
    m_panelW=gridW+padding*2+12.0f;
    m_panelH=header+gridH+footer;
    m_panelX=(width-m_panelW)*.5f;
    m_panelY=(height-m_panelH)*.5f;
    const float originX=m_panelX+padding;
    const float gridTop=m_panelY+footer+gridH;
    for(size_t i=0;i<m_slots.size();++i){
        auto& item=m_slots[i];const int row=static_cast<int>(i)/m_columns;
        const int visibleRow=row-m_scrollRow;item.visible=visibleRow>=0&&visibleRow<m_visibleRows;
        item.hovered=item.hovered&&item.visible;
        if(!item.visible)continue;
        const int column=static_cast<int>(i)%m_columns;
        item.x=originX+column*(slot+gap);
        item.y=gridTop-slot-visibleRow*(slot+gap);
    }
}

void CreativeInventory::render(UIRenderer& ui,int width,int height,int mouseX,int mouseY) {
    layoutSlots(width,height);
    constexpr float slot=44.0f,footer=34.0f;
    ui.drawRect(0,0,static_cast<float>(width),static_cast<float>(height),{0,0,0,.58f});
    ui.drawPanel(m_panelX,m_panelY,m_panelW,m_panelH,{.10f,.105f,.12f,.97f});
    const std::string title = ui.localization().text("inventory.creative");
    const auto titleSize=ui.measureText(title,1.8f);
    ui.renderText(title,m_panelX+(m_panelW-titleSize.x)*.5f,m_panelY+m_panelH-34.0f,
                  1.8f,Config::UIColors::TEXT_TITLE);
    const std::string page = ui.localization().format("inventory.page", {
        std::to_string(m_scrollRow + 1),
        std::to_string(std::max(1, m_totalRows - m_visibleRows + 1))});
    const auto pageSize=ui.measureText(page,.8f);
    ui.renderText(page,m_panelX+m_panelW-pageSize.x-18.0f,m_panelY+14.0f,.8f,{.68f,.68f,.72f});
    ui.renderText(ui.localization().text("inventory.help"),
                  m_panelX+16.0f,m_panelY+14.0f,.72f,{.65f,.65f,.70f});

    const Slot* hovered=nullptr;
    for(const auto& item:m_slots){
        if(!item.visible) continue;
        const auto& props=getItemProps(item.id);
        ui.drawRect(item.x,item.y,slot,slot,item.hovered?glm::vec4(.34f,.34f,.38f,1)
                                                       :glm::vec4(.17f,.17f,.20f,.98f));
        const glm::vec3 background = props.placedBlock
            ? getBlockProps(*props.placedBlock).color * .28f
            : glm::vec3(.08f,.08f,.10f);
        ui.drawRect(item.x+2,item.y+2,slot-4,slot-4,{background,1});
        ui.drawItemIcon(item.x+4,item.y+4,slot-8,slot-8,{item.id,1,0});
        if(item.id==m_selected||item.hovered){
            const glm::vec4 color=item.id==m_selected?glm::vec4(1,.82f,.22f,1):glm::vec4(1,1,1,.95f);
            ui.drawRect(item.x,item.y,slot,2,color);ui.drawRect(item.x,item.y+slot-2,slot,2,color);
            ui.drawRect(item.x,item.y,2,slot,color);ui.drawRect(item.x+slot-2,item.y,2,slot,color);
        }
        if(item.hovered)hovered=&item;
    }

    if(m_totalRows>m_visibleRows){
        const float trackX=m_panelX+m_panelW-12.0f;
        const float trackY=m_panelY+footer;
        const float trackH=m_visibleRows*slot+(m_visibleRows-1)*5.0f;
        const float thumbH=std::max(18.0f,trackH*m_visibleRows/m_totalRows);
        const float fraction=static_cast<float>(m_scrollRow)/std::max(1,m_totalRows-m_visibleRows);
        ui.drawRect(trackX,trackY,4,trackH,{.04f,.04f,.05f,1});
        ui.drawRect(trackX,trackY+(trackH-thumbH)*(1.0f-fraction),4,thumbH,{.70f,.70f,.74f,1});
    }
    if(hovered)ui.drawTooltip(mouseX+12.0f,mouseY+12.0f,{hovered->id,1,0});
}

void CreativeInventory::onMouseMove(int x,int y){
    constexpr float slot=44.0f;
    for(auto& item:m_slots)item.hovered=item.visible&&x>=item.x&&x<=item.x+slot&&y>=item.y&&y<=item.y+slot;
}

void CreativeInventory::onMouseClick(int button,int x,int y,std::function<void(ItemId)> select){
    if(button!=MouseButton::Left) return;
    constexpr float slot=44.0f;
    for(const auto& item:m_slots)if(item.visible&&x>=item.x&&x<=item.x+slot&&y>=item.y&&y<=item.y+slot){
        m_selected=item.id;if(select)select(item.id);return;}
}

void CreativeInventory::onScroll(double yOffset){
    const int maximum=std::max(0,m_totalRows-m_visibleRows);
    m_scrollRow=std::clamp(m_scrollRow+(yOffset<0?1:-1),0,maximum);
}
