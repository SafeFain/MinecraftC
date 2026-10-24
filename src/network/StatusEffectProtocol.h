#pragma once

#include "network/Protocol.h"
#include "game/StatusEffects.h"

namespace Lan {
inline void writeStatusEffects(Writer& writer,const std::vector<ActiveStatusEffect>& effects) {
    if(!validStatusEffects(effects))throw ProtocolError("Invalid status effects");
    writer.u8(static_cast<uint8_t>(effects.size()));
    for(const auto& e:effects){writer.u8(static_cast<uint8_t>(e.type));writer.u8(e.amplifier);writer.u32(e.remaining);}
}
inline std::vector<ActiveStatusEffect> readStatusEffects(Reader& reader) {
    const auto count=reader.u8();
    if(count>7)throw ProtocolError("Too many status effects");
    std::vector<ActiveStatusEffect> effects;
    for(uint8_t i=0;i<count;++i)effects.push_back({static_cast<StatusEffectType>(reader.u8()),reader.u8(),reader.u32()});
    if(!validStatusEffects(effects))throw ProtocolError("Invalid status effects");
    return effects;
}
}
