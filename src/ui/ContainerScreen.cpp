#include "ui/ContainerScreen.h"

#include "game/SurvivalRules.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "ui/UILayout.h"
#include "game/InventoryInteraction.h"
#include "game/SessionAccess.h"
#include "world/BlockEntity.h"

#include "core/Window.h"
#include "core/RuntimeClock.h"
#include <algorithm>

bool ContainerScreen::open(IContainerAccess& access, const glm::ivec3& position) {
    m_gamepadFocus=false;m_focusIndex=0;m_slotFeedback={};
    m_access = &access;
    m_position = position;
    return valid();
}

bool ContainerScreen::valid() const {
    return m_access && m_access->blockEntityAt(m_position) != nullptr;
}

bool ContainerScreen::contains(const Rect& r, int x, int y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

void ContainerScreen::layout(int width, int height) {
    constexpr float slot = 44, gap = 4;
    const UiCanvasFit fit(width,height,460,480);
    m_layoutScale=fit.scale;m_panelRect=fit.transform(Rect{0,0,460,480});
    const float x0=16,invY=32;
    for (size_t i = 0; i < m_inventoryRects.size(); ++i) {
        const int row = i < 9 ? 0 : 1 + static_cast<int>((i - 9) / 9);
        const int col = i < 9 ? static_cast<int>(i) : static_cast<int>((i - 9) % 9);
        const int visual = row == 0 ? 0 : 4 - row;
        m_inventoryRects[i] = {x0 + col * (slot + gap), invY + visual * (slot + gap), slot, slot};
    }
    for (auto& rect : m_containerRects) rect = {};
    const BlockEntity* entity = m_access ? m_access->blockEntityAt(m_position) : nullptr;
    if (!entity) return;
    if (entity->type == BlockEntityType::Chest) {
        const float y0 = invY + 235.0f;
        for (int i = 0; i < 27; ++i)
            m_containerRects[i] = {x0 + (i % 9) * (slot + gap),
                y0 + (2 - i / 9) * (slot + gap), slot, slot};
    } else {
        const float cx=230,cy=invY+285;
        m_containerRects[0] = {cx - 120, cy + 36, slot, slot};
        m_containerRects[1] = {cx - 120, cy - 24, slot, slot};
        m_containerRects[2] = {cx + 76, cy + 6, slot, slot};
    }
    for (auto& r:m_inventoryRects) r=fit.transform(r);
    for (auto& r:m_containerRects) r=fit.transform(r);

}

void ContainerScreen::drawStack(UIRenderer& ui, const Rect& r,
                                const ItemStack& stack, bool hovered, bool selected, UiFeedback* feedback) {
    UiTheme::slot(ui, r.x, r.y, r.w, r.h,
                  selected?UiTheme::WidgetState::Selected:
                  hovered ? UiTheme::WidgetState::Hover
                          : UiTheme::WidgetState::Normal,
                  UiTheme::SLOT,1,feedback);
    if (stack.empty()) return;
    const float s=r.w/44;
    ui.drawItemIcon(r.x+4*s,r.y+4*s,r.w-8*s,r.h-8*s,stack);
    ui.drawDurability(r.x+3*s,r.y+2*s,r.w-6*s,stack);
    if (stack.count > 1)
        UiTheme::textWithShadow(ui, std::to_string(stack.count),
                                r.x+r.w-16*s,r.y+2*s,.9f*s, glm::vec3(1));
}

void ContainerScreen::moveStack(ItemStack& cursor, ItemStack& slot, bool right) {
    InventoryInteraction::click(cursor,slot,right);
}

void ContainerScreen::quickMove(int x,int y) {
    BlockEntity* entity=m_access?m_access->blockEntityAt(m_position):nullptr;if(!entity)return;
    for(size_t i=0;i<m_inventoryRects.size();++i)if(contains(m_inventoryRects[i],x,y)){
        auto& source=m_inventory.slot(i);std::vector<ItemStack*> targets;
        if(entity->type==BlockEntityType::Chest)for(auto& slot:entity->chest)targets.push_back(&slot);
        else if(findSmeltingRecipe(source.id))targets.push_back(&entity->input);
        else if(fuelTicks(source.id))targets.push_back(&entity->fuel);
        InventoryInteraction::transfer(source,targets);return;
    }
    const int count=entity->type==BlockEntityType::Chest?27:3;
    for(int i=0;i<count;++i)if(contains(m_containerRects[i],x,y)){
        ItemStack* source=entity->type==BlockEntityType::Chest?&entity->chest[i]:
            (i==0?&entity->input:i==1?&entity->fuel:&entity->output);
        std::vector<ItemStack*> targets;for(size_t slot=0;slot<36;++slot)targets.push_back(&m_inventory.slot(slot));
        InventoryInteraction::transfer(*source,targets);return;
    }
}

void ContainerScreen::render(UIRenderer& ui, int width, int height, int mx, int my) {
    layout(width, height);
    m_pointerX=mx;m_pointerY=my;
    if(m_gamepadFocus){updateFocusPosition();mx=m_focusX;my=m_focusY;}
    const BlockEntity* entity = m_access ? m_access->blockEntityAt(m_position) : nullptr;
    if (!entity) return;
    ui.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), {0,0,0,.62f});
    const auto drawSlot=[&](size_t index,const Rect& rect,const ItemStack& stack,bool hovered,bool selected=false) {
        auto& feedback=m_slotFeedback[index];
        drawStack(ui,rect,stack,hovered,selected||(m_gamepadFocus&&hovered),&feedback);
    };
    const float scale=m_layoutScale;
    UiTheme::panel(ui,m_panelRect.x,m_panelRect.y,m_panelRect.w,m_panelRect.h);
    UiTheme::rect(ui,m_panelRect.x+16*scale,m_panelRect.y+242*scale,
                  m_panelRect.w-32*scale,1,UiTheme::BORDER);
    const std::string title=ui.localization().text(
        entity->type==BlockEntityType::Chest?"container.chest":"container.furnace");
    UiTheme::textWithShadow(ui,title,m_panelRect.x+16*scale,m_panelRect.y+432*scale,
        UiTheme::fittedScale(ui,title,2*scale,m_panelRect.w-32*scale),UiTheme::TEXT);
    for (size_t i=0;i<m_inventoryRects.size();++i)
        drawSlot(i,m_inventoryRects[i],m_inventory.slot(i),contains(m_inventoryRects[i],mx,my));
    if (entity->type == BlockEntityType::Chest) {
        for (int i=0;i<27;++i) drawSlot(36+i,m_containerRects[i],entity->chest[i],contains(m_containerRects[i],mx,my));
    } else {
        drawSlot(36,m_containerRects[0],entity->input,contains(m_containerRects[0],mx,my));
        drawSlot(37,m_containerRects[1],entity->fuel,contains(m_containerRects[1],mx,my));
        drawSlot(38,m_containerRects[2],entity->output,contains(m_containerRects[2],mx,my));
        if (entity->burnTotal) {
            const float burn = std::clamp(
                static_cast<float>(entity->burnRemaining) /
                static_cast<float>(entity->burnTotal), 0.0f, 1.0f);
            UiTheme::sprite(ui, m_containerRects[1].x + 48.0f*scale,
                            m_containerRects[1].y + 6.0f*scale, 1.5f*scale,
                            UiTheme::FLAME, UiTheme::FLAME_PALETTE,
                            0.55f + 0.45f * burn);
        }
        if (entity->cookTotal) {
            const float cook = std::clamp(
                static_cast<float>(entity->cookProgress) /
                static_cast<float>(entity->cookTotal), 0.0f, 1.0f);
            UiTheme::progressBar(ui, m_containerRects[0].x + 55.0f*scale,
                                 m_containerRects[0].y + 16.0f*scale, 100.0f*scale, 12.0f*scale,
                                 cook, UiTheme::ACCENT);
            UiTheme::sprite(ui, m_containerRects[0].x + 157.0f*scale,
                            m_containerRects[0].y + 17.0f*scale, 2.0f*scale,
                            UiTheme::ARROW_RIGHT, UiTheme::ARROW_PALETTE);
        }
    }
    if (!m_cursor.empty()) drawStack(ui,{static_cast<float>(mx+8),static_cast<float>(my+8),38,38},m_cursor,true);
    if (m_cursor.empty()) {
        for (size_t i=0;i<m_inventoryRects.size();++i)
            if (contains(m_inventoryRects[i],mx,my) && !m_inventory.slot(i).empty())
                ui.drawTooltip(mx+12,my+12,m_inventory.slot(i));
        const int count = entity->type == BlockEntityType::Chest ? 27 : 3;
        for (int i=0;i<count;++i) if (contains(m_containerRects[i],mx,my)) {
            const ItemStack* stack = entity->type == BlockEntityType::Chest ? &entity->chest[i] :
                (i==0?&entity->input:i==1?&entity->fuel:&entity->output);
            if (!stack->empty()) ui.drawTooltip(mx+12,my+12,*stack);
        }
    }
}

void ContainerScreen::click(int button, int x, int y) {
    BlockEntity* entity = m_access ? m_access->blockEntityAt(m_position) : nullptr;
    if (!entity) return;
    const bool right = button == MouseButton::Right;
    for (size_t i=0;i<m_inventoryRects.size();++i) if (contains(m_inventoryRects[i],x,y)) {
        moveStack(m_cursor,m_inventory.slot(i),right); return;
    }
    const int count = entity->type == BlockEntityType::Chest ? 27 : 3;
    for (int i=0;i<count;++i) if (contains(m_containerRects[i],x,y)) {
        if (entity->type == BlockEntityType::Chest) moveStack(m_cursor,entity->chest[i],right);
        else if (i == 0 && (m_cursor.empty() || findSmeltingRecipe(m_cursor.id)))
            moveStack(m_cursor,entity->input,right);
        else if (i == 1 && (m_cursor.empty() || fuelTicks(m_cursor.id)))
            moveStack(m_cursor,entity->fuel,right);
        else if (i == 2 && m_cursor.empty()) moveStack(m_cursor,entity->output,right);
        return;
    }
}

void ContainerScreen::onMouseButton(int button,ButtonAction action,int x,int y,int mods) {
    m_pointerX=x;m_pointerY=y;
    if(action==ButtonAction::Press){m_gamepadFocus=false;m_focusX=x;m_focusY=y;}
    if (button!=MouseButton::Left && button!=MouseButton::Right) return;
    if (action==ButtonAction::Press) { m_pressed=true;m_button=button;m_pressX=x;m_pressY=y;m_pressMods=mods;
        m_cursorHeldAtPress=!m_cursor.empty();m_dragTargets.clear();return; }
    if (action!=ButtonAction::Release || !m_pressed || m_button!=button) return;
    if((m_pressMods&KeyModifier::Shift)&&button==MouseButton::Left){quickMove(x,y);m_pressed=false;m_button=-1;return;}
    const int dx=x-m_pressX,dy=y-m_pressY;const bool dragged=dx*dx+dy*dy>=16;
    const double now=RuntimeClock::seconds(RuntimeClock{}.now());
    if(dragged&&m_cursorHeldAtPress&&!m_dragTargets.empty())InventoryInteraction::distribute(m_cursor,m_dragTargets,button==MouseButton::Right);
    else if(!dragged&&button==MouseButton::Left&&!m_cursor.empty()&&m_lastClickSeconds>=0&&now-m_lastClickSeconds<=.30){
        std::vector<ItemStack*> sources;for(size_t i=0;i<36;++i)sources.push_back(&m_inventory.slot(i));
        BlockEntity* entity=m_access?m_access->blockEntityAt(m_position):nullptr;if(entity){if(entity->type==BlockEntityType::Chest)for(auto& slot:entity->chest)sources.push_back(&slot);
            else{sources.push_back(&entity->input);sources.push_back(&entity->fuel);sources.push_back(&entity->output);}}
        InventoryInteraction::gather(m_cursor,sources);
    } else if (dragged) { click(button,m_pressX,m_pressY);click(button,x,y); } else click(button,x,y);
    m_lastClickSeconds=now;
    m_pressed=false;m_button=-1;
}

void ContainerScreen::onMouseMove(int x,int y){
    m_gamepadFocus=false;m_pointerX=x;m_pointerY=y;if(!m_pressed||!m_cursorHeldAtPress)return;ItemStack* target=nullptr;
    for(size_t i=0;i<m_inventoryRects.size();++i)if(contains(m_inventoryRects[i],x,y)){target=&m_inventory.slot(i);break;}
    BlockEntity* entity=m_access?m_access->blockEntityAt(m_position):nullptr;if(!target&&entity&&entity->type==BlockEntityType::Chest)
        for(int i=0;i<27;++i)if(contains(m_containerRects[i],x,y)){target=&entity->chest[i];break;}
    if(target&&std::find(m_dragTargets.begin(),m_dragTargets.end(),target)==m_dragTargets.end())m_dragTargets.push_back(target);}

std::vector<ContainerScreen::Rect> ContainerScreen::focusRects() const {
    std::vector<Rect> rects(m_inventoryRects.begin(),m_inventoryRects.end());
    const BlockEntity* entity=m_access?m_access->blockEntityAt(m_position):nullptr;
    const int count=entity?(entity->type==BlockEntityType::Chest?27:3):0;
    for(int i=0;i<count;++i)rects.push_back(m_containerRects[static_cast<size_t>(i)]);
    return rects;
}

void ContainerScreen::updateFocusPosition() {
    const auto rects=focusRects();
    if (rects.empty()) return;
    m_focusIndex=std::clamp(m_focusIndex,0,static_cast<int>(rects.size())-1);
    const auto& r=rects[static_cast<size_t>(m_focusIndex)];
    m_focusX=static_cast<int>(r.x+r.w*.5f);m_focusY=static_cast<int>(r.y+r.h*.5f);
}

void ContainerScreen::onGamepadNavigate(int dx,int dy) {
    const auto rects=focusRects();
    if (rects.empty()) return;
    if (!m_gamepadFocus) {
        m_focusIndex=0;
        for (size_t i=0;i<rects.size();++i)
            if (contains(rects[i],m_pointerX,m_pointerY)) { m_focusIndex=static_cast<int>(i);break; }
    }
    m_gamepadFocus=true;
    m_focusIndex=uiDirectionalNeighbor(rects,m_focusIndex,dx,dy);
    updateFocusPosition();
}


void ContainerScreen::onGamepadAction(int action) {
    onGamepadNavigate(0,0);
    if(action==2)quickMove(m_focusX,m_focusY);
    else click(action==1?MouseButton::Right:MouseButton::Left,m_focusX,m_focusY);
}

ItemStack* ContainerScreen::hoveredStack(int x, int y) {
    for (size_t i = 0; i < m_inventoryRects.size(); ++i)
        if (contains(m_inventoryRects[i], x, y)) return &m_inventory.slot(i);
    BlockEntity* entity = m_access ? m_access->blockEntityAt(m_position) : nullptr;
    if (!entity) return nullptr;
    const int count = entity->type == BlockEntityType::Chest ? 27 : 3;
    for (int i = 0; i < count; ++i) {
        if (!contains(m_containerRects[static_cast<size_t>(i)], x, y)) continue;
        if (entity->type == BlockEntityType::Chest) return &entity->chest[i];
        return i == 0 ? &entity->input : i == 1 ? &entity->fuel : &entity->output;
    }
    return nullptr;
}

bool ContainerScreen::swapHoveredWithHotbar(int hotbarSlot) {
    if (hotbarSlot < 0 || hotbarSlot >= static_cast<int>(InventoryModel::HOTBAR_SIZE))
        return false;
    ItemStack* hovered = hoveredStack(m_pointerX, m_pointerY);
    if (!hovered) return false;
    ItemStack& hotbar = m_inventory.slot(static_cast<size_t>(hotbarSlot));
    if (hovered == &hotbar) return true;
    BlockEntity* entity=m_access?m_access->blockEntityAt(m_position):nullptr;
    if(entity&&entity->type==BlockEntityType::Furnace){
        if(hovered==&entity->input&&!hotbar.empty()&&!findSmeltingRecipe(hotbar.id))return false;
        if(hovered==&entity->fuel&&!hotbar.empty()&&!fuelTicks(hotbar.id))return false;
        if(hovered==&entity->output&&!hotbar.empty())return false;
    }
    std::swap(*hovered, hotbar);
    return true;
}

bool ContainerScreen::swapHoveredWithOffhand() {
    ItemStack* hovered = hoveredStack(m_pointerX, m_pointerY);
    if (!hovered) return false;
    ItemStack& offhand = m_inventory.offhand();
    if (hovered == &offhand) return true;
    BlockEntity* entity=m_access?m_access->blockEntityAt(m_position):nullptr;
    if(entity&&entity->type==BlockEntityType::Furnace){
        if(hovered==&entity->input&&!offhand.empty()&&!findSmeltingRecipe(offhand.id))return false;
        if(hovered==&entity->fuel&&!offhand.empty()&&!fuelTicks(offhand.id))return false;
        if(hovered==&entity->output&&!offhand.empty())return false;
    }
    std::swap(*hovered, offhand);
    return true;
}

ItemStack ContainerScreen::dropHovered(bool entireStack) {
    ItemStack* hovered = hoveredStack(m_pointerX, m_pointerY);
    if (!hovered || hovered->empty()) return {};
    if (!entireStack) return InventoryInteraction::takeOne(*hovered);
    ItemStack dropped = *hovered;
    hovered->clear();
    return dropped;
}

void ContainerScreen::close(const std::function<void(ItemStack)>& drop) {
    if (!m_cursor.empty()) {
        const uint32_t remaining=m_inventory.add(m_cursor);
        if (remaining) { m_cursor.count=static_cast<uint8_t>(remaining);drop(m_cursor); }
        m_cursor.clear();
    }
    m_access=nullptr;m_pressed=false;m_button=-1;
}

void ContainerScreen::onPointerCancel() {
    m_pressed=false;m_button=-1;
    m_dragTargets.clear();m_cursorHeldAtPress=false;
}
