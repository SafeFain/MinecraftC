#include "ui/VillagerTradeScreen.h"

#include "entity/EntityManager.h"
#include "game/SessionAccess.h"
#include "game/VillagerTrade.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "ui/UILayout.h"

#include <algorithm>

bool VillagerTradeScreen::open(ITradeAccess& access, uint64_t entityId) {
    const Entity* entity = access.tradeEntity(entityId);
    if (!entity || entity->type != EntityType::Villager ||
        entity->villager.profession == VillagerProfession::Unemployed)
        return false;
    m_access = &access;
    m_entityId = entityId;
    m_selected = 0;
    m_feedback = {};
    return true;
}

bool VillagerTradeScreen::valid(
    const glm::dvec3& eye, const glm::vec3& direction) const {
    return m_access &&
        m_access->tradeUsable(m_entityId, eye, direction, 3.0f);
}

void VillagerTradeScreen::close() {
    m_access = nullptr;
    m_entityId = 0;
}

bool VillagerTradeScreen::contains(const Rect& rect, int x, int y) {
    return x >= rect.x && x <= rect.x + rect.w &&
           y >= rect.y && y <= rect.y + rect.h;
}

void VillagerTradeScreen::layout(int width, int height) {
    constexpr float rowWidth = 310.0f;
    constexpr float rowHeight = 52.0f;
    const UiCanvasFit fit(width,height,360,400);
    m_layoutScale=fit.scale;m_panelRect=fit.transform(Rect{0,0,360,400});
    const float x=25,baseY=32;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        m_rows[i] = {x, baseY + (4 - static_cast<int>(i)) * 58.0f,
                     rowWidth, rowHeight};
        m_outputs[i] = {x + 250.0f, m_rows[i].y + 4.0f, 44.0f, 44.0f};
    }
    for (auto& r:m_rows) r=fit.transform(r);
    for (auto& r:m_outputs) r=fit.transform(r);

}

void VillagerTradeScreen::drawStack(
    UIRenderer& ui, const Rect& rect, const ItemStack& stack, bool highlighted) {
    UiTheme::slot(ui, rect.x, rect.y, rect.w, rect.h,
        highlighted ? UiTheme::WidgetState::Hover
                    : UiTheme::WidgetState::Normal, UiTheme::SLOT);
    if (stack.empty()) return;
    const float s=rect.w/44;
    ui.drawItemIcon(rect.x+4*s,rect.y+4*s,rect.w-8*s,rect.h-8*s,stack);
    if (stack.count > 1)
        UiTheme::textWithShadow(ui, std::to_string(stack.count),
            rect.x+rect.w-16*s,rect.y+2*s,.9f*s, glm::vec3(1.0f));
}

void VillagerTradeScreen::render(
    UIRenderer& ui, int width, int height, int mouseX, int mouseY) {
    layout(width, height);
    const Entity* entity = m_access ? m_access->tradeEntity(m_entityId) : nullptr;
    if (!entity) return;
    ui.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height),
                {0, 0, 0, .62f});
    const float scale=m_layoutScale;
    UiTheme::panel(ui,m_panelRect.x,m_panelRect.y,m_panelRect.w,m_panelRect.h);
    const std::string title=ui.localization().text("trade.profession."+
        std::string(villagerProfessionKey(entity->villager.profession)))+"  "+
        ui.localization().text("trade.level")+" "+std::to_string(entity->villager.level);
    UiTheme::textWithShadow(ui,title,m_panelRect.x+24*scale,m_panelRect.y+352*scale,
        UiTheme::fittedScale(ui,title,2*scale,m_panelRect.w-48*scale),UiTheme::TEXT);
    const auto& offers = villagerOffers(entity->villager.profession);
    const uint8_t unlocked = unlockedTradeCount(entity->villager);
    for (uint8_t i = 0; i < 5; ++i) {
        const auto quote=villagerQuote(entity->villager,i);
        const bool enabled = i < unlocked &&
            entity->villager.uses[i] < offers[i].maximumUses;
        const bool selected = i == m_selected;
        const bool hovered = enabled && contains(m_rows[i], mouseX, mouseY);
        UiTheme::button(ui,m_rows[i].x,m_rows[i].y,m_rows[i].w,m_rows[i].h,{},
            selected?UiTheme::WidgetState::Selected:hovered?UiTheme::WidgetState::Hover:
                UiTheme::WidgetState::Normal,false,0,enabled?1.0f:.55f,-1,-1,false,0,&m_feedback[i]);
        Rect input{m_rows[i].x+8*scale,m_rows[i].y+4*scale,44*scale,44*scale};
        drawStack(ui, input, quote.input, false);
        UiTheme::sprite(ui, m_rows[i].x+118*scale,m_rows[i].y+16*scale,
                        2*scale, UiTheme::ARROW_RIGHT, UiTheme::ARROW_PALETTE,
                        enabled ? 1.0f : .35f);
        drawStack(ui, m_outputs[i], quote.output,
                  enabled && contains(m_outputs[i], mouseX, mouseY));
        UiTheme::textWithShadow(ui,
            (entity->villager.uses[i]>=offers[i].maximumUses ? ui.localization().text("trade.exhausted")+" " : std::string{})+
            std::to_string(entity->villager.uses[i]) + "/" +
            std::to_string(offers[i].maximumUses),
            m_rows[i].x+170*scale,m_rows[i].y+18*scale,.8f*scale,
            enabled ? glm::vec3(.85f) : glm::vec3(.45f));
    }
}

void VillagerTradeScreen::executeSelected() {
    if (m_access) {
        m_feedback[m_selected].activate();
        m_access->executeTrade(m_entityId, m_selected, m_inventory);
    }
}

void VillagerTradeScreen::onMouseButton(
    int button, ButtonAction action, int mouseX, int mouseY) {
    if (button != MouseButton::Left || action != ButtonAction::Release) return;
    for (uint8_t i = 0; i < 5; ++i) {
        if (contains(m_outputs[i], mouseX, mouseY)) {
            m_selected = i;
            executeSelected();
            return;
        }
        if (contains(m_rows[i], mouseX, mouseY)) {
            m_selected = i;
            return;
        }
    }
}

void VillagerTradeScreen::onGamepadNavigate(int, int dy) {
    if (dy < 0 && m_selected > 0) --m_selected;
    if (dy > 0 && m_selected < 4) ++m_selected;
}

void VillagerTradeScreen::onGamepadAction() { executeSelected(); }
