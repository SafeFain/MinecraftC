#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

enum class StatusEffectType : uint8_t {
    FireResistance, Strength, Regeneration, Healing, Harming, Weakness, Wither
};

struct ActiveStatusEffect {
    StatusEffectType type=StatusEffectType::FireResistance;
    uint8_t amplifier=0;
    uint32_t remaining=0;
};

inline bool validStatusEffects(const std::vector<ActiveStatusEffect>& effects) {
    if(effects.size()>7)return false;
    uint32_t seen=0;
    for(const auto& effect:effects) {
        const auto type=static_cast<uint8_t>(effect.type);
        if(type>6||effect.amplifier>1||!effect.remaining||effect.remaining>72000||
           effect.type==StatusEffectType::Healing||effect.type==StatusEffectType::Harming||
           (seen&(1u<<type)))return false;
        seen|=1u<<type;
    }
    return true;
}

class StatusEffects {
public:
    const std::vector<ActiveStatusEffect>& active() const { return m_active; }
    void restore(std::vector<ActiveStatusEffect> effects) {m_active=std::move(effects);}
    void clear() {m_active.clear();}
    int level(StatusEffectType type) const {
        for(const auto& e:m_active)if(e.type==type&&e.remaining)return e.amplifier+1;
        return 0;
    }
    void apply(StatusEffectType type,uint8_t amplifier,uint32_t ticks) {
        if(!ticks)return;
        for(auto& e:m_active)if(e.type==type) {
            if(amplifier>e.amplifier)e={type,amplifier,ticks};
            else if(amplifier==e.amplifier)e.remaining=std::max(e.remaining,ticks);
            return;
        }
        m_active.push_back({type,amplifier,ticks});
    }
    // Call once per authoritative game tick. Healing/damage bypass hunger.
    template<class Heal,class Hurt> void tick(Heal heal,Hurt hurt) {
        for(auto& e:m_active) {
            if(!e.remaining)continue;
            const uint32_t shift=std::min<uint8_t>(e.amplifier,5);
            if(e.type==StatusEffectType::Regeneration&&e.remaining%std::max(1u,50u>>shift)==0)heal(1.f);
            if(e.type==StatusEffectType::Wither&&e.remaining%std::max(1u,40u>>shift)==0)hurt(1.f);
            --e.remaining;
        }
        m_active.erase(std::remove_if(m_active.begin(),m_active.end(),[](const auto& e){return e.remaining==0;}),m_active.end());
    }
private:
    std::vector<ActiveStatusEffect> m_active;
};
