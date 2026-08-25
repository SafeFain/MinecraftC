#include "ui/VillagerTradeScreen.h"

#include "entity/EntityManager.h"
#include "game/VillagerTrade.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"

#include <algorithm>

bool VillagerTradeScreen::open(EntityManager& entities, uint64_t entityId) {
    const Entity* entity = entities.entityById(entityId);
    if (!entity || entity->type != EntityType::Villager ||
        entity->villager.profession == VillagerProfession::Unemployed)
        return false;
    m_entities = &entities;
    m_entityId = entityId;
    m_selected = 0;
    return true;
}

bool VillagerTradeScreen::valid(
    const glm::dvec3& eye, const glm::vec3& direction) const {
    return m_entities &&
        m_entities->villagerUsable(m_entityId, eye, direction, 3.0f);
}

void VillagerTradeScreen::close() {
    m_entities = nullptr;
    m_entityId = 0;
}

bool VillagerTradeScreen::contains(const Rect& rect, int x, int y) {
    return x >= rect.x && x <= rect.x + rect.w &&
           y >= rect.y && y <= rect.y + rect.h;
}

void VillagerTradeScreen::layout(int width, int height) {
    constexpr float rowWidth = 310.0f;
    constexpr float rowHeight = 52.0f;
    const float x = (width - rowWidth) * .5f;
    const float baseY = height * .5f - 132.0f;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        m_rows[i] = {x, baseY + (4 - static_cast<int>(i)) * 58.0f,
                     rowWidth, rowHeight};
        m_outputs[i] = {x + 250.0f, m_rows[i].y + 4.0f, 44.0f, 44.0f};
    }
}

void VillagerTradeScreen::drawStack(
    UIRenderer& ui, const Rect& rect, const ItemStack& stack, bool highlighted) {
    UiTheme::slot(ui, rect.x, rect.y, rect.w, rect.h,
        highlighted ? UiTheme::WidgetState::Hover
                    : UiTheme::WidgetState::Normal, UiTheme::SLOT);
    if (stack.empty()) return;
    ui.drawItemIcon(rect.x + 4, rect.y + 4, rect.w - 8, rect.h - 8, stack);
    if (stack.count > 1)
        UiTheme::textWithShadow(ui, std::to_string(stack.count),
            rect.x + rect.w - 16, rect.y + 2, .9f, glm::vec3(1.0f));
}

void VillagerTradeScreen::render(
    UIRenderer& ui, int width, int height, int mouseX, int mouseY) {
    layout(width, height);
    const Entity* entity = m_entities ? m_entities->entityById(m_entityId) : nullptr;
    if (!entity) return;
    ui.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height),
                {0, 0, 0, .62f});
    UiTheme::panel(ui, m_rows[0].x - 14.0f, m_rows[4].y - 14.0f,
                   m_rows[0].w + 28.0f,
                   m_rows[0].y + m_rows[0].h - m_rows[4].y + 28.0f,
                   UiTheme::PANEL);
    const std::string title = ui.localization().text("trade.title");
    const glm::vec2 titleSize = ui.measureText(title, 2.0f);
    UiTheme::textWithShadow(ui, title, (width - titleSize.x) * .5f,
        m_rows[0].y + 72.0f, 2.0f, UiTheme::TEXT_TITLE);
    const auto& offers = villagerOffers(entity->villager.profession);
    const uint8_t unlocked = unlockedTradeCount(entity->villager);
    for (uint8_t i = 0; i < 5; ++i) {
        const bool enabled = i < unlocked &&
            entity->villager.uses[i] < offers[i].maximumUses;
        const bool selected = i == m_selected;
        const bool hovered = contains(m_rows[i], mouseX, mouseY);
        ui.drawRect(m_rows[i].x, m_rows[i].y, m_rows[i].w, m_rows[i].h,
            selected ? glm::vec4(.24f,.43f,.22f,.95f) :
            hovered ? glm::vec4(.25f,.25f,.28f,.95f) :
                      glm::vec4(.15f,.15f,.17f,.92f));
        Rect input{m_rows[i].x + 8.0f, m_rows[i].y + 4.0f, 44.0f, 44.0f};
        drawStack(ui, input, offers[i].input, false);
        UiTheme::sprite(ui, m_rows[i].x + 118.0f, m_rows[i].y + 16.0f,
                        2.0f, UiTheme::ARROW_RIGHT, UiTheme::ARROW_PALETTE,
                        enabled ? 1.0f : .35f);
        drawStack(ui, m_outputs[i], offers[i].output,
                  enabled && contains(m_outputs[i], mouseX, mouseY));
        UiTheme::textWithShadow(ui,
            std::to_string(entity->villager.uses[i]) + "/" +
            std::to_string(offers[i].maximumUses),
            m_rows[i].x + 170.0f, m_rows[i].y + 18.0f, .8f,
            enabled ? glm::vec3(.85f) : glm::vec3(.45f));
    }
}

void VillagerTradeScreen::executeSelected() {
    if (m_entities)
        m_entities->tradeWith(m_entityId, m_selected, m_inventory);
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
