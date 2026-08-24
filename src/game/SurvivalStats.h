#pragma once

#include "game/GameRules.h"
#include "game/Item.h"

class SurvivalStats {
public:
    static constexpr float MAX_HEALTH = 20.0f;
    static constexpr uint8_t MAX_HUNGER = 20;

    float health() const { return m_health; }
    uint8_t hunger() const { return m_hunger; }
    float saturation() const { return m_saturation; }
    float exhaustion() const { return m_exhaustion; }
    uint32_t foodTickTimer() const { return m_foodTickTimer; }
    bool dead() const { return m_health <= 0.0f; }
    bool canSprint() const { return m_hunger > 6; }

    void set(float health, uint8_t hunger, float saturation, float exhaustion,
             uint32_t foodTickTimer = 0);
    void resetAfterRespawn();
    void addExhaustion(float amount);
    bool eat(ItemId food);
    void damage(float amount);
    void heal(float amount);
    void tick(Difficulty difficulty, uint32_t ticks = 1);

private:
    float m_health = MAX_HEALTH;
    uint8_t m_hunger = MAX_HUNGER;
    float m_saturation = 5.0f;
    float m_exhaustion = 0.0f;
    uint32_t m_foodTickTimer = 0;

    void consumeExhaustion(Difficulty difficulty);
};
