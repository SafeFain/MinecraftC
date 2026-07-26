#include "ui/ContainerScreen.h"

#include "game/SurvivalRules.h"
#include "ui/UIRenderer.h"
#include "world/World.h"

#include <GLFW/glfw3.h>
#include <algorithm>

bool ContainerScreen::open(World& world, const glm::ivec3& position) {
    m_world = &world;
    m_position = position;
    return valid();
}

bool ContainerScreen::valid() const {
    return m_world && m_world->getBlockEntity(m_position) != nullptr;
}

bool ContainerScreen::contains(const Rect& r, int x, int y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

void ContainerScreen::layout(int width, int height) {
    constexpr float slot = 44, gap = 4;
    const float x0 = (width - (9 * slot + 8 * gap)) * 0.5f;
    const float invY = height * 0.5f - 190.0f;
    for (size_t i = 0; i < m_inventoryRects.size(); ++i) {
        const int row = i < 9 ? 0 : 1 + static_cast<int>((i - 9) / 9);
        const int col = i < 9 ? static_cast<int>(i) : static_cast<int>((i - 9) % 9);
        const int visual = row == 0 ? 0 : 4 - row;
        m_inventoryRects[i] = {x0 + col * (slot + gap), invY + visual * (slot + gap), slot, slot};
    }
    for (auto& rect : m_containerRects) rect = {};
    const BlockEntity* entity = m_world ? m_world->getBlockEntity(m_position) : nullptr;
    if (!entity) return;
    if (entity->type == BlockEntityType::Chest) {
        const float y0 = invY + 235.0f;
        for (int i = 0; i < 27; ++i)
            m_containerRects[i] = {x0 + (i % 9) * (slot + gap),
                y0 + (2 - i / 9) * (slot + gap), slot, slot};
    } else {
        const float cx = width * 0.5f, cy = invY + 285.0f;
        m_containerRects[0] = {cx - 120, cy + 36, slot, slot};
        m_containerRects[1] = {cx - 120, cy - 24, slot, slot};
        m_containerRects[2] = {cx + 76, cy + 6, slot, slot};
    }
}

void ContainerScreen::drawStack(UIRenderer& ui, const Rect& r,
                                const ItemStack& stack, bool hovered) {
    ui.drawRect(r.x, r.y, r.w, r.h, hovered ? glm::vec4(.34f,.34f,.38f,.98f)
                                             : glm::vec4(.18f,.18f,.21f,.96f));
    if (stack.empty()) return;
    const auto& props = getItemProps(stack.id);
    if (props.placedBlock) ui.drawBlockIcon(r.x+4, r.y+4, r.w-8, r.h-8, *props.placedBlock);
    else ui.renderText(props.name.empty() ? "?" : props.name.substr(0,1),
                       r.x+15, r.y+13, 1.5f, glm::vec3(.95f));
    if (stack.count > 1) ui.renderText(std::to_string(stack.count), r.x+r.w-16, r.y+2,
                                      .9f, glm::vec3(1));
}

void ContainerScreen::moveStack(ItemStack& cursor, ItemStack& slot, bool right) {
    if (cursor.empty()) {
        if (slot.empty()) return;
        if (right && slot.count > 1) {
            const uint8_t count = static_cast<uint8_t>((slot.count + 1) / 2);
            cursor = {slot.id, count, slot.damage}; slot.count -= count;
        } else std::swap(cursor, slot);
    } else if (slot.empty()) {
        if (right) { slot = {cursor.id, 1, cursor.damage}; if (--cursor.count == 0) cursor.clear(); }
        else std::swap(cursor, slot);
    } else if (slot.id == cursor.id && slot.damage == cursor.damage) {
        const int moved = std::min<int>(right ? 1 : cursor.count,
            getItemProps(slot.id).maxStack - slot.count);
        slot.count += moved; cursor.count -= moved; if (!cursor.count) cursor.clear();
    } else if (!right) std::swap(cursor, slot);
}

void ContainerScreen::render(UIRenderer& ui, int width, int height, int mx, int my) {
    layout(width, height);
    const BlockEntity* entity = m_world ? m_world->getBlockEntity(m_position) : nullptr;
    if (!entity) return;
    ui.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), {0,0,0,.62f});
    ui.renderText(entity->type == BlockEntityType::Chest ? "CHEST" : "FURNACE",
                  width*.5f-60, height*.78f, 2, {1,.85f,.3f});
    for (size_t i=0;i<m_inventoryRects.size();++i)
        drawStack(ui,m_inventoryRects[i],m_inventory.slot(i),contains(m_inventoryRects[i],mx,my));
    if (entity->type == BlockEntityType::Chest) {
        for (int i=0;i<27;++i) drawStack(ui,m_containerRects[i],entity->chest[i],contains(m_containerRects[i],mx,my));
    } else {
        drawStack(ui,m_containerRects[0],entity->input,contains(m_containerRects[0],mx,my));
        drawStack(ui,m_containerRects[1],entity->fuel,contains(m_containerRects[1],mx,my));
        drawStack(ui,m_containerRects[2],entity->output,contains(m_containerRects[2],mx,my));
        if (entity->burnTotal) ui.drawRect(m_containerRects[1].x+50,m_containerRects[1].y,10,
            44.0f*entity->burnRemaining/entity->burnTotal,{1,.45f,.08f,1});
        if (entity->cookTotal) ui.drawRect(m_containerRects[0].x+55,m_containerRects[0].y+16,
            100.0f*entity->cookProgress/entity->cookTotal,12,{.8f,.8f,.8f,1});
    }
    if (!m_cursor.empty()) drawStack(ui,{static_cast<float>(mx+8),static_cast<float>(my+8),38,38},m_cursor,true);
}

void ContainerScreen::click(int button, int x, int y) {
    BlockEntity* entity = m_world ? m_world->getBlockEntity(m_position) : nullptr;
    if (!entity) return;
    const bool right = button == GLFW_MOUSE_BUTTON_RIGHT;
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

void ContainerScreen::onMouseButton(int button,int action,int x,int y) {
    if (button!=GLFW_MOUSE_BUTTON_LEFT && button!=GLFW_MOUSE_BUTTON_RIGHT) return;
    if (action==GLFW_PRESS) { m_pressed=true;m_button=button;m_pressX=x;m_pressY=y;return; }
    if (action!=GLFW_RELEASE || !m_pressed || m_button!=button) return;
    const int dx=x-m_pressX,dy=y-m_pressY;
    if (dx*dx+dy*dy>=16) { click(button,m_pressX,m_pressY);click(button,x,y); } else click(button,x,y);
    m_pressed=false;m_button=-1;
}

void ContainerScreen::close(const std::function<void(ItemStack)>& drop) {
    if (!m_cursor.empty()) {
        const uint32_t remaining=m_inventory.add(m_cursor);
        if (remaining) { m_cursor.count=static_cast<uint8_t>(remaining);drop(m_cursor); }
        m_cursor.clear();
    }
    m_world=nullptr;m_pressed=false;m_button=-1;
}
