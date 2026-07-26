#include "game/SurvivalStats.h"

#include <algorithm>

void SurvivalStats::set(float health, uint8_t hunger, float saturation,
                        float exhaustion) {
    m_health = std::clamp(health, 0.0f, MAX_HEALTH);
    m_hunger = std::min(hunger, MAX_HUNGER);
    m_saturation = std::clamp(saturation, 0.0f, static_cast<float>(m_hunger));
    m_exhaustion = std::clamp(exhaustion, 0.0f, 4.0f);
}

void SurvivalStats::resetAfterRespawn() {
    set(MAX_HEALTH, MAX_HUNGER, 5.0f, 0.0f);
    m_regenerationTicks = 0;
    m_starvationTicks = 0;
}

void SurvivalStats::addExhaustion(float amount) {
    m_exhaustion += std::max(amount, 0.0f);
    consumeExhaustion();
}

void SurvivalStats::consumeExhaustion() {
    while (m_exhaustion >= 4.0f) {
        m_exhaustion -= 4.0f;
        if (m_saturation > 0.0f) {
            m_saturation = std::max(0.0f, m_saturation - 1.0f);
        } else if (m_hunger > 0) {
            --m_hunger;
        }
    }
}

bool SurvivalStats::eat(ItemId food) {
    if (m_hunger >= MAX_HUNGER || !isValidItemId(food)) return false;
    const auto& props = getItemProps(food);
    if (props.kind != ItemKind::Food || props.food == 0) return false;
    m_hunger = static_cast<uint8_t>(
        std::min<int>(MAX_HUNGER, m_hunger + props.food));
    m_saturation = std::min<float>(
        static_cast<float>(m_hunger), m_saturation + props.saturation);
    return true;
}

void SurvivalStats::damage(float amount) {
    m_health = std::max(0.0f, m_health - std::max(amount, 0.0f));
}

void SurvivalStats::heal(float amount) {
    m_health = std::min(MAX_HEALTH, m_health + std::max(amount, 0.0f));
}

void SurvivalStats::tick(Difficulty difficulty, uint32_t ticks) {
    for (uint32_t i = 0; i < ticks; ++i) {
        if (dead()) return;
        if (difficulty == Difficulty::Peaceful) {
            ++m_regenerationTicks;
            if (m_hunger < MAX_HUNGER && m_regenerationTicks % 10 == 0) ++m_hunger;
            if (m_health < MAX_HEALTH && m_regenerationTicks % 20 == 0) heal(1.0f);
            continue;
        }

        if (m_health < MAX_HEALTH && m_hunger >= 18) {
            ++m_regenerationTicks;
            const uint32_t interval = m_hunger == MAX_HUNGER && m_saturation > 0.0f
                ? 10u : 80u;
            if (m_regenerationTicks >= interval) {
                heal(1.0f);
                addExhaustion(6.0f);
                m_regenerationTicks = 0;
            }
        } else {
            m_regenerationTicks = 0;
        }

        if (m_hunger == 0) {
            if (++m_starvationTicks >= 80) {
                const float floor = difficulty == Difficulty::Easy ? 10.0f
                                  : difficulty == Difficulty::Normal ? 1.0f : 0.0f;
                if (m_health > floor) damage(std::min(1.0f, m_health - floor));
                m_starvationTicks = 0;
            }
        } else {
            m_starvationTicks = 0;
        }
    }
}
