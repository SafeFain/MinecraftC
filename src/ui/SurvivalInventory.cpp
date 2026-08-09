#include "ui/SurvivalInventory.h"

#include "game/SurvivalRules.h"
#include "game/InventoryInteraction.h"
#include "ui/UIRenderer.h"

#include "core/Window.h"
#include "core/RuntimeClock.h"
#include <algorithm>
#include <string>

void SurvivalInventoryScreen::layout(int screenWidth, int screenHeight) {
    constexpr float slot = 44.0f;
    constexpr float gap = 4.0f;
    const float width = 9 * slot + 8 * gap;
    const float originX = (screenWidth - width) * 0.5f;
    const float originY = (screenHeight - (4 * slot + 3 * gap)) * 0.5f - 55.0f;

    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) {
        const int row = i < 9 ? 0 : 1 + static_cast<int>((i - 9) / 9);
        const int col = i < 9 ? static_cast<int>(i) : static_cast<int>((i - 9) % 9);
        const int visualRow = row == 0 ? 0 : 4 - row;
        m_inventoryRects[i] = {
            originX + col * (slot + gap),
            originY + visualRow * (slot + gap), slot, slot
        };
    }
    const int craftSize = m_craftingTable ? 3 : 2;
    const float craftX = screenWidth * 0.5f - (m_craftingTable ? 150.0f : 118.0f);
    const float craftY = originY + 4 * (slot + gap) + 28.0f;
    for (auto& rect : m_craftingRects) rect = {};
    for (int i = 0; i < craftSize * craftSize; ++i) {
        m_craftingRects[i] = {
            craftX + (i % craftSize) * (slot + gap),
            craftY + (craftSize - 1 - i / craftSize) * (slot + gap), slot, slot
        };
    }
    m_outputRect = {craftX + craftSize * (slot + gap) + 42.0f,
                    craftY + (craftSize - 1) * 24.0f, slot, slot};
    for (size_t i = 0; i < m_armorRects.size(); ++i)
        m_armorRects[i] = {originX - 64.0f, originY + (3 - i) * (slot + gap),
                           slot, slot};
    m_offhandRect = {originX + width + 20.0f, originY, slot, slot};
}

bool SurvivalInventoryScreen::contains(const Rect& rect, int x, int y) {
    return x >= rect.x && x <= rect.x + rect.w &&
           y >= rect.y && y <= rect.y + rect.h;
}

void SurvivalInventoryScreen::drawStack(
    UIRenderer& ui, const Rect& rect, const ItemStack& stack, bool hovered) {
    ui.drawRect(rect.x, rect.y, rect.w, rect.h,
                hovered ? glm::vec4(0.34f, 0.34f, 0.38f, 0.98f)
                        : glm::vec4(0.18f, 0.18f, 0.21f, 0.96f));
    if (stack.empty()) return;
    ui.drawItemIcon(rect.x + 4.0f, rect.y + 4.0f,
                    rect.w - 8.0f, rect.h - 8.0f, stack);
    ui.drawDurability(rect.x + 3.0f, rect.y + 2.0f, rect.w - 6.0f, stack);
    if (stack.count > 1) {
        const std::string text = std::to_string(stack.count);
        const auto size = ui.measureText(text, 0.9f);
        ui.renderText(text, rect.x + rect.w - size.x - 2.0f, rect.y + 2.0f,
                      0.9f, glm::vec3(1.0f));
    }
}

ItemStack SurvivalInventoryScreen::craftingOutput() const {
    std::array<ItemId, 9> grid{};
    grid.fill(ItemId::EMPTY);
    const size_t count = m_craftingTable ? 9 : 4;
    for (size_t i = 0; i < count; ++i)
        grid[i] = m_crafting[i].empty() ? ItemId::EMPTY : m_crafting[i].id;
    const auto* recipe = findCraftingRecipe(
        grid, m_craftingTable ? 3 : 2, m_craftingTable ? 3 : 2);
    return recipe ? recipe->output : ItemStack{};
}

void SurvivalInventoryScreen::render(
    UIRenderer& ui, int screenWidth, int screenHeight, int mouseX, int mouseY) {
    layout(screenWidth, screenHeight);
    if(m_focusX||m_focusY){mouseX=m_focusX;mouseY=m_focusY;}
    ui.drawRect(0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight),
                glm::vec4(0, 0, 0, 0.62f));
    const std::string title = ui.localization().text("inventory.survival");
    const auto titleSize = ui.measureText(title, 2.0f);
    ui.renderText(title, (screenWidth - titleSize.x) * 0.5f,
                  screenHeight * 0.78f, 2.0f, glm::vec3(1.0f, 0.85f, 0.3f));
    ui.renderText(ui.localization().text("inventory.crafting"),
                  screenWidth * 0.5f - 118.0f,
                  m_craftingRects[0].y + 54.0f, 1.1f, glm::vec3(0.85f));

    const ItemStack* tooltip = nullptr;
    for (size_t i = 0; i < m_inventoryRects.size(); ++i) {
        drawStack(ui, m_inventoryRects[i], m_inventory.slot(i),
                  contains(m_inventoryRects[i], mouseX, mouseY));
        if (contains(m_inventoryRects[i], mouseX, mouseY)) tooltip = &m_inventory.slot(i);
    }
    const size_t craftSlots = m_craftingTable ? 9 : 4;
    for (size_t i = 0; i < craftSlots; ++i)
        drawStack(ui, m_craftingRects[i], m_crafting[i],
                  contains(m_craftingRects[i], mouseX, mouseY));
    drawStack(ui, m_outputRect, craftingOutput(),
              contains(m_outputRect, mouseX, mouseY));
    for (size_t i = 0; i < m_armorRects.size(); ++i)
        drawStack(ui, m_armorRects[i], m_inventory.armor()[i],
                  contains(m_armorRects[i], mouseX, mouseY));
    drawStack(ui, m_offhandRect, m_inventory.offhand(),
              contains(m_offhandRect, mouseX, mouseY));

    if (tooltip && !tooltip->empty()) ui.drawTooltip(mouseX + 12.0f, mouseY + 12.0f, *tooltip);

    if (!m_cursor.empty()) {
        Rect cursor{static_cast<float>(mouseX + 8), static_cast<float>(mouseY + 8), 38, 38};
        drawStack(ui, cursor, m_cursor, true);
    }
}

void SurvivalInventoryScreen::clickStack(ItemStack& stack, bool rightClick) {
    InventoryInteraction::click(m_cursor, stack, rightClick);
}

void SurvivalInventoryScreen::quickMove(int x, int y) {
    for(size_t i=0;i<m_inventoryRects.size();++i)if(contains(m_inventoryRects[i],x,y)){
        auto& source=m_inventory.slot(i);if(source.empty())return;
        if(getItemProps(source.id).kind==ItemKind::Armor)for(size_t a=0;a<InventoryModel::ARMOR_SIZE;++a)
            if(m_inventory.armor()[a].empty()&&acceptsArmor(a,source.id)){m_inventory.armor()[a]=source;source.clear();return;}
        std::vector<ItemStack*> targets;const size_t begin=i<9?9:0,end=i<9?36:9;
        for(size_t slot=begin;slot<end;++slot)targets.push_back(&m_inventory.slot(slot));
        InventoryInteraction::transfer(source,targets);return;
    }
    for(size_t i=0;i<m_armorRects.size();++i)if(contains(m_armorRects[i],x,y)){
        std::vector<ItemStack*> targets;for(size_t slot=0;slot<36;++slot)targets.push_back(&m_inventory.slot(slot));
        InventoryInteraction::transfer(m_inventory.armor()[i],targets);return;
    }
    if(contains(m_outputRect,x,y))while(!craftingOutput().empty()){
        ItemStack output=craftingOutput();if(m_inventory.add(output)!=0)break;
        for(auto& ingredient:m_crafting)if(!ingredient.empty()&&!--ingredient.count)ingredient.clear();
    }
}

void SurvivalInventoryScreen::takeCraftingOutput() {
    const ItemStack output = craftingOutput();
    if (output.empty()) return;
    if (!m_cursor.empty() &&
        (m_cursor.id != output.id || m_cursor.damage != output.damage ||
         m_cursor.count + output.count > getItemProps(output.id).maxStack)) return;
    if (m_cursor.empty()) m_cursor = output;
    else m_cursor.count += output.count;
    for (auto& ingredient : m_crafting) {
        if (!ingredient.empty() && --ingredient.count == 0) ingredient.clear();
    }
}

void SurvivalInventoryScreen::performClick(int button, int mouseX, int mouseY) {
    if (button != MouseButton::Left && button != MouseButton::Right) return;
    const bool right = button == MouseButton::Right;
    if (contains(m_outputRect, mouseX, mouseY)) {
        if (!right) takeCraftingOutput();
        return;
    }
    for (size_t i = 0; i < m_inventoryRects.size(); ++i) {
        if (contains(m_inventoryRects[i], mouseX, mouseY)) {
            clickStack(m_inventory.slot(i), right);
            return;
        }
    }
    for (size_t i = 0; i < m_armorRects.size(); ++i) {
        if (contains(m_armorRects[i], mouseX, mouseY)) {
            if (m_cursor.empty() || acceptsArmor(i, m_cursor.id))
                clickStack(m_inventory.armor()[i], right);
            return;
        }
    }
    if (contains(m_offhandRect, mouseX, mouseY)) {
        clickStack(m_inventory.offhand(), right);
        return;
    }
    const size_t craftSlots = m_craftingTable ? 9 : 4;
    for (size_t i = 0; i < craftSlots; ++i) {
        if (contains(m_craftingRects[i], mouseX, mouseY)) {
            clickStack(m_crafting[i], right);
            return;
        }
    }
}

void SurvivalInventoryScreen::onMouseButton(
    int button, ButtonAction action, int mouseX, int mouseY, int mods) {
    if(action==ButtonAction::Press){m_focusX=mouseX;m_focusY=mouseY;}
    if (button != MouseButton::Left && button != MouseButton::Right) return;
    if (action == ButtonAction::Press) {
        m_pointerPressed = true;
        m_pressedButton = button;
        m_pressX = mouseX;
        m_pressY = mouseY;
        m_pressMods = mods;
        m_cursorHeldAtPress = !m_cursor.empty();
        m_dragTargets.clear();
        return;
    }
    if (action != ButtonAction::Release || !m_pointerPressed || button != m_pressedButton)
        return;

    if ((m_pressMods & KeyModifier::Shift) && button == MouseButton::Left) {
        quickMove(mouseX,mouseY);m_pointerPressed=false;m_pressedButton=-1;return;
    }
    const int deltaX = mouseX - m_pressX;
    const int deltaY = mouseY - m_pressY;
    const bool dragged = deltaX * deltaX + deltaY * deltaY >= 16;
    const double now=RuntimeClock::seconds(RuntimeClock{}.now());
    if(dragged && m_cursorHeldAtPress && !m_dragTargets.empty()) {
        InventoryInteraction::distribute(m_cursor,m_dragTargets,
                                         button==MouseButton::Right);
    } else if(!dragged && button==MouseButton::Left && !m_cursor.empty() &&
       m_lastClickSeconds>=0.0 && now-m_lastClickSeconds<=0.30) {
        std::vector<ItemStack*> sources;
        for(size_t i=0;i<36;++i)sources.push_back(&m_inventory.slot(i));
        for(auto& stack:m_crafting)sources.push_back(&stack);
        InventoryInteraction::gather(m_cursor,sources);
    } else if (dragged) {
        // Pick up from the press position, then place at the release position.
        performClick(button, m_pressX, m_pressY);
        performClick(button, mouseX, mouseY);
    } else {
        // A normal click changes the cursor stack exactly once.
        performClick(button, mouseX, mouseY);
    }
    m_lastClickSeconds=now;
    m_pointerPressed = false;
    m_pressedButton = -1;
}

void SurvivalInventoryScreen::onMouseMove(int x,int y){
    if(!m_pointerPressed||!m_cursorHeldAtPress) return;
    ItemStack* target=nullptr;
    for(size_t i=0;i<m_inventoryRects.size();++i)if(contains(m_inventoryRects[i],x,y)){target=&m_inventory.slot(i);break;}
    if(!target){const size_t count=m_craftingTable?9:4;for(size_t i=0;i<count;++i)if(contains(m_craftingRects[i],x,y)){target=&m_crafting[i];break;}}
    if(target&&std::find(m_dragTargets.begin(),m_dragTargets.end(),target)==m_dragTargets.end())m_dragTargets.push_back(target);
}

void SurvivalInventoryScreen::onGamepadNavigate(int dx,int dy) {
    std::vector<Rect> rects(m_inventoryRects.begin(),m_inventoryRects.end());
    const size_t craftingCount=m_craftingTable?9:4;
    for(size_t i=0;i<craftingCount;++i)rects.push_back(m_craftingRects[i]);
    rects.insert(rects.end(),m_armorRects.begin(),m_armorRects.end());
    rects.push_back(m_offhandRect);rects.push_back(m_outputRect);
    if(rects.empty())return;
    if(!m_focusX&&!m_focusY){m_focusX=static_cast<int>(rects[0].x+22);m_focusY=static_cast<int>(rects[0].y+22);}
    float best=1e30f;const Rect* chosen=nullptr;
    for(const Rect& r:rects){const float cx=r.x+22,cy=r.y+22,vx=cx-m_focusX,vy=cy-m_focusY;
        if((dx&&vx*dx<=1)||(dy&&vy*dy<=1))continue;
        const float primary=dx?std::abs(vx):std::abs(vy),secondary=dx?std::abs(vy):std::abs(vx);
        const float score=primary+secondary*2.0f;if(score<best){best=score;chosen=&r;}}
    if(chosen){m_focusX=static_cast<int>(chosen->x+22);m_focusY=static_cast<int>(chosen->y+22);}
}

void SurvivalInventoryScreen::onGamepadAction(int action) {
    if(action==2)quickMove(m_focusX,m_focusY);
    else performClick(action==1?MouseButton::Right:MouseButton::Left,m_focusX,m_focusY);
}

bool SurvivalInventoryScreen::acceptsArmor(size_t slot, ItemId item) {
    if (getItemProps(item).kind != ItemKind::Armor) return false;
    const uint16_t relative = static_cast<uint16_t>(item) -
                              static_cast<uint16_t>(ItemId::LEATHER_HELMET);
    return relative % 4 == slot;
}

void SurvivalInventoryScreen::onClose() {
    m_pointerPressed = false;
    m_pressedButton = -1;
    for (auto& stack : m_crafting) {
        if (!stack.empty() && m_inventory.add(stack) == 0) stack.clear();
    }
    if (!m_cursor.empty() && m_inventory.add(m_cursor) == 0) m_cursor.clear();
}
