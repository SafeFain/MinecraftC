#include "ui/Inventory.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "Config.h"
#include "game/Item.h"

#include <algorithm>
#include <cmath>

CreativeInventory::CreativeInventory() {
    selectCategory(CreativeItemCategory::BuildingBlocks);
}

void CreativeInventory::selectCategory(CreativeItemCategory category) {
    m_activeCategory = category;
    m_slots.clear();
    for (const ItemId id : creativeInventoryItemsIn(category))
        m_slots.push_back({id,0,0,false,false});
    m_scrollRow = 0;
    m_focus = 0;
    m_tabMode = false;
    m_tabFocus = static_cast<int>(category);
    updateTabHover();
    for (auto& item : m_slots) item.hovered = false;
}

void CreativeInventory::updateSlotHover() {
    for (size_t i = 0; i < m_slots.size(); ++i)
        m_slots[i].hovered = static_cast<int>(i) == m_focus;
}

void CreativeInventory::updateTabHover() {
    for (size_t i = 0; i < m_tabs.size(); ++i)
        m_tabs[i].hovered = static_cast<int>(i) == m_tabFocus;
}

void CreativeInventory::layoutSlots(int width,int height) {
    constexpr float slot=44.0f,gap=5.0f,padding=14.0f,header=52.0f,footer=34.0f;
    constexpr float tabBarH=26.0f,tabGapY=14.0f,tabGapX=2.0f;
    constexpr float tabExtra=tabBarH+tabGapY;
    m_columns=std::clamp(static_cast<int>((width-80.0f)/(slot+gap)),5,10);
    m_totalRows=m_slots.empty()?0:
        static_cast<int>((m_slots.size()+static_cast<size_t>(m_columns)-1)/m_columns);
    m_visibleRows=std::clamp(
        static_cast<int>((height-(150.0f+tabExtra))/(slot+gap)),2,7);
    m_visibleRows=std::min(m_visibleRows,m_totalRows);
    m_scrollRow=std::clamp(m_scrollRow,0,std::max(0,m_totalRows-m_visibleRows));
    const float gridW=m_columns*slot+(m_columns-1)*gap;
    const float gridH=std::max(0.0f,m_visibleRows*slot+(m_visibleRows-1)*gap);
    m_panelW=gridW+padding*2+12.0f;
    m_panelH=header+tabBarH+tabGapY+gridH+footer;
    m_panelX=(width-m_panelW)*.5f;
    m_panelY=(height-m_panelH)*.5f;
    m_playerButtonX=m_panelX+14.0f;
    m_playerButtonY=m_panelY+8.0f;
    m_playerButtonW=std::min(150.0f,m_panelW*.46f);
    m_playerButtonH=22.0f;
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

    // Minecraft-style icon tabs fill the strip below the title header.
    const float tabCount=static_cast<float>(m_tabs.size());
    const float availableW=std::max(0.0f,m_panelW-padding*2.0f);
    float tabW=(availableW-tabGapX*(tabCount-1.0f))/tabCount;
    float tabX=m_panelX+padding;
    if(tabW<20.0f){
        tabW=20.0f;
        const float total=tabW*tabCount+tabGapX*(tabCount-1.0f);
        tabX=m_panelX+(m_panelW-total)*.5f;
    }
    const float tabY=m_panelY+m_panelH-header-tabBarH;
    for(size_t i=0;i<m_tabs.size();++i){
        auto& tab=m_tabs[i];
        tab.x=tabX+static_cast<float>(i)*(tabW+tabGapX);
        tab.y=tabY;tab.w=tabW;tab.h=tabBarH;
    }
}

void CreativeInventory::render(UIRenderer& ui,int width,int height,int mouseX,int mouseY) {
    layoutSlots(width,height);
    constexpr float slot=44.0f,footer=34.0f,tabGapY=14.0f;
    ui.drawRect(0,0,static_cast<float>(width),static_cast<float>(height),
                glm::vec4(0.0f,0.0f,0.0f,.58f));
    const std::string title = ui.localization().text("inventory.creative");
    UiTheme::panel(ui,m_panelX,m_panelY,m_panelW,m_panelH,UiTheme::PANEL,
                   title,1.6f);
    const std::string page = ui.localization().format("inventory.page", {
        std::to_string(m_scrollRow + 1),
        std::to_string(std::max(1, m_totalRows - m_visibleRows + 1))});
    const auto pageSize=ui.measureText(page,.8f);
    UiTheme::textWithShadow(ui,page,m_panelX+m_panelW-pageSize.x-18.0f,
                            m_panelY+14.0f,.8f,UiTheme::TEXT_DIM);
    const std::string playerLabel=ui.localization().text("inventory.player_tab");
    UiTheme::button(ui,m_playerButtonX,m_playerButtonY,m_playerButtonW,
                    m_playerButtonH,playerLabel,UiTheme::WidgetState::Normal,
                    false,0.72f);

    // Icon tabs (Minecraft style): one representative item per category.
    int hoveredTab=-1;
    for(size_t i=0;i<m_tabs.size();++i){
        const auto& tab=m_tabs[i];
        const bool active=static_cast<int>(i)==
            static_cast<int>(m_activeCategory);
        const UiTheme::WidgetState state=active?UiTheme::WidgetState::Selected
            :tab.hovered?UiTheme::WidgetState::Hover:UiTheme::WidgetState::Normal;
        UiTheme::button(ui,tab.x,tab.y,tab.w,tab.h,{},state,false,0.0f);
        const auto& info=creativeCategoryInfo(static_cast<CreativeItemCategory>(i));
        const float iconSize=std::min({20.0f,tab.h-6.0f,tab.w-4.0f});
        ui.drawItemIcon(tab.x+(tab.w-iconSize)*.5f,tab.y+(tab.h-iconSize)*.5f,
                        iconSize,iconSize,{info.icon,1,0});
        if(tab.hovered)hoveredTab=static_cast<int>(i);
    }
    const auto& activeInfo=creativeCategoryInfo(m_activeCategory);
    const std::string categoryName=ui.localization().text(activeInfo.localizationKey);
    const auto categorySize=ui.measureText(categoryName,.55f);
    const float tabBarBottom=m_panelY+m_panelH-52.0f-26.0f;
    UiTheme::textWithShadow(ui,categoryName,
        m_panelX+(m_panelW-categorySize.x)*.5f,
        tabBarBottom-(tabGapY+categorySize.y)*.5f,.55f,UiTheme::TEXT_DIM);

    const Slot* hovered=nullptr;
    for(const auto& item:m_slots){
        if(!item.visible) continue;
        const auto& props=getItemProps(item.id);
        const glm::vec3 background = props.placedBlock
            ? getBlockProps(*props.placedBlock).color * .28f
            : glm::vec3(.10f,.09f,.08f);
        const UiTheme::WidgetState state = item.id==m_selected
            ? UiTheme::WidgetState::Selected
            : item.hovered ? UiTheme::WidgetState::Hover
                           : UiTheme::WidgetState::Normal;
        UiTheme::slot(ui,item.x,item.y,slot,slot,state,
                      glm::vec4(background,1.0f));
        ui.drawItemIcon(item.x+4,item.y+4,slot-8,slot-8,{item.id,1,0});
        if(item.hovered)hovered=&item;
    }

    if(m_totalRows>m_visibleRows){
        const float trackX=m_panelX+m_panelW-16.0f;
        const float trackY=m_panelY+footer+6.0f;
        const float trackH=m_visibleRows*slot+(m_visibleRows-1)*5.0f-12.0f;
        UiTheme::scrollBar(ui,trackX,trackY,6.0f,trackH,
                           m_scrollRow,m_visibleRows,m_totalRows);
    }
    if(hovered)ui.drawTooltip(mouseX+12.0f,mouseY+12.0f,{hovered->id,1,0});
    else if(hoveredTab>=0){
        const auto& info=creativeCategoryInfo(
            static_cast<CreativeItemCategory>(hoveredTab));
        UiTheme::tooltip(ui,mouseX+12.0f,mouseY+12.0f,
                         ui.localization().text(info.localizationKey));
    }
}

void CreativeInventory::onMouseMove(int x,int y){
    constexpr float slot=44.0f;
    for(auto& item:m_slots)item.hovered=item.visible&&x>=item.x&&x<=item.x+slot&&y>=item.y&&y<=item.y+slot;
    for(auto& tab:m_tabs)tab.hovered=x>=tab.x&&x<=tab.x+tab.w&&y>=tab.y&&y<=tab.y+tab.h;
}

void CreativeInventory::onMouseClick(int button,int x,int y,
                                     std::function<void(ItemId)> select,
                                     std::function<void()> openPlayerInventory){
    if(button!=MouseButton::Left) return;
    for(size_t i=0;i<m_tabs.size();++i){
        const auto& tab=m_tabs[i];
        if(x>=tab.x&&x<=tab.x+tab.w&&y>=tab.y&&y<=tab.y+tab.h){
            selectCategory(static_cast<CreativeItemCategory>(i));
            return;
        }
    }
    if(x>=m_playerButtonX&&x<=m_playerButtonX+m_playerButtonW&&
       y>=m_playerButtonY&&y<=m_playerButtonY+m_playerButtonH){
        if(openPlayerInventory)openPlayerInventory();
        return;
    }
    constexpr float slot=44.0f;
    for(const auto& item:m_slots)if(item.visible&&x>=item.x&&x<=item.x+slot&&y>=item.y&&y<=item.y+slot){
        m_selected=item.id;if(select)select(item.id);return;}
}

void CreativeInventory::onScroll(double yOffset){
    const int maximum=std::max(0,m_totalRows-m_visibleRows);
    m_scrollRow=std::clamp(m_scrollRow+(yOffset<0?1:-1),0,maximum);
}

void CreativeInventory::onGamepadNavigate(int dx,int dy) {
    const int categoryCount=static_cast<int>(m_tabs.size());
    if(m_tabMode){
        if(dx!=0){
            m_tabFocus=std::clamp(m_tabFocus+dx,0,categoryCount-1);
            updateTabHover();
        }else if(dy>0){
            m_tabMode=false;
            updateSlotHover();
        }
        return;
    }
    if(m_slots.empty())return;
    const int row=m_focus/m_columns,col=m_focus%m_columns;
    if(dy<0&&row==m_scrollRow){
        m_tabMode=true;
        m_tabFocus=static_cast<int>(m_activeCategory);
        updateTabHover();
        for(auto& item:m_slots)item.hovered=false;
        return;
    }
    const int nextRow=std::clamp(row+dy,0,std::max(0,m_totalRows-1));
    const int nextCol=std::clamp(col+dx,0,m_columns-1);
    m_focus=std::min(static_cast<int>(m_slots.size())-1,nextRow*m_columns+nextCol);
    if(nextRow<m_scrollRow)m_scrollRow=nextRow;
    if(nextRow>=m_scrollRow+m_visibleRows)m_scrollRow=nextRow-m_visibleRows+1;
    updateSlotHover();
}

void CreativeInventory::onGamepadAction(bool select,std::function<void(ItemId)> callback) {
    if(!select)return;
    if(m_tabMode){
        selectCategory(static_cast<CreativeItemCategory>(m_tabFocus));
        return;
    }
    if(m_focus<0||m_focus>=static_cast<int>(m_slots.size()))return;
    m_selected=m_slots[static_cast<size_t>(m_focus)].id;
    if(callback)callback(m_selected);
}
