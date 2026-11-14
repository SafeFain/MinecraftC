#pragma once
#include "world/RedstoneRules.h"
#include <string_view>
#include <utility>

namespace Redstone {
// Relative native content slots. World integration will append these after the
// existing BlockId range; no existing serialized value is assigned or changed.
enum class NativeBlock : uint16_t {
    REDSTONE_DUST_FIRST = 0,
    LEVER_FIRST = REDSTONE_DUST_FIRST + 32,
    REDSTONE_TORCH_FIRST = LEVER_FIRST + 12,
    REPEATER_FIRST = REDSTONE_TORCH_FIRST + 10,
    COMPARATOR_FIRST = REPEATER_FIRST + 64,
    OBSERVER_FIRST = COMPARATOR_FIRST + 128,
    PISTON_FIRST = OBSERVER_FIRST + 12,
    PISTON_HEAD_FIRST = PISTON_FIRST + 24,
    REDSTONE_LAMP_FIRST = PISTON_HEAD_FIRST + 12,
    REDSTONE_BLOCK = REDSTONE_LAMP_FIRST + 2,
    REDSTONE_ORE, DEEPSLATE_REDSTONE_ORE, REDSTONE_ORE_LIT, DEEPSLATE_REDSTONE_ORE_LIT,
    Count
};
inline bool inRange(NativeBlock id,NativeBlock first,unsigned count) {
    const auto raw=static_cast<unsigned>(id), start=static_cast<unsigned>(first);
    return raw>=start && raw<start+count;
}
inline std::optional<State> nativeState(NativeBlock id) {
    State s;
    unsigned n=0;
    if(inRange(id,NativeBlock::REDSTONE_DUST_FIRST,32)) {
        n=unsigned(id)-unsigned(NativeBlock::REDSTONE_DUST_FIRST);s.kind=Kind::Dust;s.power=static_cast<uint8_t>(n%16);s.alternate=n>=16;
    } else if(inRange(id,NativeBlock::LEVER_FIRST,12)) {
        n=unsigned(id)-unsigned(NativeBlock::LEVER_FIRST);s.kind=Kind::Lever;s.facing=static_cast<Face>(n%6);s.alternate=n>=6;
    } else if(inRange(id,NativeBlock::REDSTONE_TORCH_FIRST,10)) {
        n=unsigned(id)-unsigned(NativeBlock::REDSTONE_TORCH_FIRST);s.kind=Kind::Torch;s.facing=n%5==0?Face::Up:static_cast<Face>(n%5+1);s.power=n>=5?15:0;
    } else if(inRange(id,NativeBlock::REPEATER_FIRST,64)) {
        n=unsigned(id)-unsigned(NativeBlock::REPEATER_FIRST);s.kind=Kind::Repeater;s.facing=static_cast<Face>(2+n%4);s.delay=static_cast<uint8_t>(1+(n/4)%4);s.power=n&16?15:0;s.locked=n&32;
    } else if(inRange(id,NativeBlock::COMPARATOR_FIRST,128)) {
        n=unsigned(id)-unsigned(NativeBlock::COMPARATOR_FIRST);s.kind=Kind::Comparator;s.facing=static_cast<Face>(2+n%4);s.alternate=n&4;s.power=static_cast<uint8_t>(n/8);
    } else if(inRange(id,NativeBlock::OBSERVER_FIRST,12)) {
        n=unsigned(id)-unsigned(NativeBlock::OBSERVER_FIRST);s.kind=Kind::Observer;s.facing=static_cast<Face>(n%6);s.power=n>=6?15:0;
    } else if(inRange(id,NativeBlock::PISTON_FIRST,24)) {
        n=unsigned(id)-unsigned(NativeBlock::PISTON_FIRST);s.kind=Kind::Piston;s.facing=static_cast<Face>(n%6);s.power=(n/6)%2?15:0;s.alternate=n>=12;
    } else if(inRange(id,NativeBlock::REDSTONE_LAMP_FIRST,2)) {
        s.kind=Kind::Lamp;s.power=id==NativeBlock::REDSTONE_LAMP_FIRST?0:15;
    } else if(id==NativeBlock::REDSTONE_BLOCK) {s.kind=Kind::Block;s.power=15;}
    else return {};
    return s;
}
inline NativeBlock nativeBlock(State s) {
    if(!s.valid())throw std::invalid_argument("Invalid native redstone state");
    unsigned first=0,n=0;
    switch(s.kind) {
        case Kind::Dust:first=unsigned(NativeBlock::REDSTONE_DUST_FIRST);n=s.power+(s.alternate?16:0);break;
        case Kind::Lever:first=unsigned(NativeBlock::LEVER_FIRST);n=unsigned(s.facing)+(s.alternate?6:0);break;
        case Kind::Torch:
            if(s.facing==Face::Down)throw std::invalid_argument("Ceiling redstone torch");
            first=unsigned(NativeBlock::REDSTONE_TORCH_FIRST);n=(s.facing==Face::Up?0:unsigned(s.facing)-1)+(s.power?5:0);break;
        case Kind::Repeater:first=unsigned(NativeBlock::REPEATER_FIRST);n=unsigned(s.facing)-2+4*(s.delay-1)+(s.power?16:0)+(s.locked?32:0);break;
        case Kind::Comparator:first=unsigned(NativeBlock::COMPARATOR_FIRST);n=unsigned(s.facing)-2+(s.alternate?4:0)+8*s.power;break;
        case Kind::Observer:first=unsigned(NativeBlock::OBSERVER_FIRST);n=unsigned(s.facing)+(s.power?6:0);break;
        case Kind::Piston:first=unsigned(NativeBlock::PISTON_FIRST);n=unsigned(s.facing)+(s.power?6:0)+(s.alternate?12:0);break;
        case Kind::Lamp:first=unsigned(NativeBlock::REDSTONE_LAMP_FIRST);n=s.power?1:0;break;
        case Kind::Block:first=unsigned(NativeBlock::REDSTONE_BLOCK);break;
    }
    const auto id=static_cast<NativeBlock>(first+n);
    const auto canonical=nativeState(id);
    if(!canonical || encode(*canonical)!=encode(s))
        throw std::invalid_argument("Noncanonical native redstone state");
    return id;
}
inline bool pistonHead(NativeBlock id) {return inRange(id,NativeBlock::PISTON_HEAD_FIRST,12);}
inline bool ore(NativeBlock id) {return inRange(id,NativeBlock::REDSTONE_ORE,4);}
inline bool thin(Kind kind) {
    return kind==Kind::Dust || kind==Kind::Lever || kind==Kind::Torch || kind==Kind::Repeater || kind==Kind::Comparator;
}
// Shared defaults for block aliases and item placement. Resolving a block must
// not depend on the item registry (lightweight world targets do not link it).
inline std::optional<NativeBlock> namedBlock(std::string_view name) {
    if(name=="redstone_ore")return NativeBlock::REDSTONE_ORE;
    if(name=="deepslate_redstone_ore")return NativeBlock::DEEPSLATE_REDSTONE_ORE;
    constexpr std::array<std::pair<std::string_view,Kind>,10> names{{
        {"redstone_dust",Kind::Dust},{"lever",Kind::Lever},{"redstone_torch",Kind::Torch},
        {"redstone_repeater",Kind::Repeater},{"redstone_comparator",Kind::Comparator},
        {"observer",Kind::Observer},{"piston",Kind::Piston},{"sticky_piston",Kind::Piston},
        {"redstone_lamp",Kind::Lamp},{"redstone_block",Kind::Block}
    }};
    for(const auto& entry:names)if(name==entry.first) {
        State state;state.kind=entry.second;state.alternate=name=="sticky_piston";
        if(state.kind==Kind::Lever || state.kind==Kind::Torch)state.facing=Face::Up;
        if(state.kind==Kind::Torch || state.kind==Kind::Block)state.power=15;
        return nativeBlock(state);
    }
    return std::nullopt;
}
} // namespace Redstone
