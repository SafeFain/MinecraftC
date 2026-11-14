#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

// Simulation contracts deliberately contain no renderer, platform or chunk types.
namespace Redstone {
struct Position {
    int x = 0, y = 0, z = 0;
    friend bool operator==(Position a, Position b) { return std::tie(a.x,a.y,a.z)==std::tie(b.x,b.y,b.z); }
    friend bool operator!=(Position a, Position b) { return !(a==b); }
    friend bool operator<(Position a, Position b) { return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z); }
    friend Position operator+(Position a, Position b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
};
enum class Face : uint8_t { Up, Down, North, South, East, West };
constexpr std::array<Position,6> offsets{{{0,1,0},{0,-1,0},{0,0,-1},{0,0,1},{1,0,0},{-1,0,0}}};
inline Position offset(Face face) { return offsets.at(static_cast<size_t>(face)); }
inline Face opposite(Face face) { return static_cast<Face>(static_cast<uint8_t>(face)^1u); }
struct Signal {
    uint8_t weak = 0, strong = 0;
    bool valid() const { return weak<=15 && strong<=weak; }
};
enum class Kind : uint8_t { Dust, Lever, Torch, Repeater, Comparator, Observer, Piston, Lamp, Block };
struct State {
    Kind kind = Kind::Dust;
    Face facing = Face::North;
    uint8_t power = 0;
    uint8_t delay = 1;
    bool alternate = false; // dot, subtraction, sticky, or lever on
    bool locked = false;
    bool valid() const {
        return kind<=Kind::Block && facing<=Face::West && power<=15 && delay>=1 && delay<=4 &&
            ((kind!=Kind::Repeater && kind!=Kind::Comparator) || facing>=Face::North);
    }
};
// Compact values are independent of serialized BlockId allocation. Reject rather
// than mask invalid input so a corrupt state cannot turn into another component.
inline uint16_t encode(State state) {
    if(!state.valid()) throw std::invalid_argument("Invalid redstone state");
    return static_cast<uint16_t>(static_cast<uint16_t>(state.kind) |
        (static_cast<uint16_t>(state.facing)<<4) | (uint16_t(state.power)<<7) |
        (uint16_t(state.delay-1)<<11) | (state.alternate?8192:0) | (state.locked?16384:0));
}
inline std::optional<State> decode(uint16_t bits) {
    if(bits&32768) return std::nullopt;
    State s{static_cast<Kind>(bits&15),static_cast<Face>((bits>>4)&7),
        static_cast<uint8_t>((bits>>7)&15),static_cast<uint8_t>(((bits>>11)&3)+1),
        bool(bits&8192),bool(bits&16384)};
    return s.valid()?std::optional<State>(s):std::nullopt;
}
inline uint8_t comparator(uint8_t rear, uint8_t side, bool subtract) {
    rear=std::min<uint8_t>(rear,15); side=std::min<uint8_t>(side,15);
    return subtract ? static_cast<uint8_t>(rear>side?rear-side:0) : static_cast<uint8_t>(rear>=side?rear:0);
}
struct Slot { uint8_t count = 0, capacity = 64; };
inline uint8_t fullness(const std::vector<Slot>& slots) {
    if(slots.empty()) return 0;
    // All legal stack capacities divide 64; integer arithmetic avoids rounding
    // differences at the comparator's exact signal thresholds.
    uint64_t units=0;
    for(const auto slot:slots) {
        if(!slot.capacity || 64%slot.capacity || slot.count>slot.capacity)
            throw std::invalid_argument("Invalid comparator slot");
        units+=uint64_t(slot.count)*(64/slot.capacity);
    }
    return units?static_cast<uint8_t>(1+units*14/(slots.size()*64)):0;
}
// Ages, rather than absolute world ticks, are persisted so unloaded components
// do not consume their burnout window or cooldown. Advance once per completed
// active game tick, never once per queue-budget continuation.
struct TorchBurnout {
    std::array<uint8_t,8> ages{};
    uint8_t count=0;
    uint16_t cooldown=0;
    bool valid() const {
        if(count>ages.size() || cooldown>160) return false;
        for(size_t i=0;i<count;++i) if(ages[i]>60) return false;
        return true;
    }
    void advance() {
        if(cooldown) --cooldown;
        uint8_t kept=0;
        for(uint8_t i=0;i<count;++i)
            if(ages[i]<60) ages[kept++]=static_cast<uint8_t>(ages[i]+1);
        count=kept;
    }
    bool recordOff() {
        if(cooldown) return true;
        if(count<ages.size()) ages[count++]=0;
        if(count==ages.size()) {cooldown=160;return true;}
        return false;
    }
};
inline Signal output(State s, Face face, Face attachment = Face::Down) {
    switch(s.kind) {
        case Kind::Block: return {15,0};
        case Kind::Lever: return s.alternate?Signal{15,static_cast<uint8_t>(face==attachment?15:0)}:Signal{};
        case Kind::Torch:
            return face==attachment?Signal{}:Signal{s.power,static_cast<uint8_t>(face==Face::Up?s.power:0)};
        case Kind::Repeater: case Kind::Comparator: case Kind::Observer:
            return face==s.facing?Signal{s.power,s.power}:Signal{};
        default: return {};
    }
}
enum class PushReaction : uint8_t { Move, Destroy, Block };
struct PushCell { uint16_t block = 0; PushReaction reaction = PushReaction::Block; };
struct Move { Position from, to; uint16_t block = 0; };
struct PushPlan { std::vector<Move> moves; std::optional<Position> destroy; };
// Query returns unknown for unavailable chunks or out-of-dimension coordinates.
// Planning has no side effects; callers must revalidate before atomic commit.
template<class Query>
std::optional<PushPlan> planPush(Position first, Face direction, Query&& query) {
    PushPlan plan;
    Position p=first;
    for(size_t count=0;;++count,p=p+offset(direction)) {
        const auto cell=query(p);
        if(!cell) return std::nullopt;
        if(cell->block==0) break;
        if(cell->reaction==PushReaction::Block) return std::nullopt;
        if(cell->reaction==PushReaction::Destroy) { plan.destroy=p; break; }
        if(count==12) return std::nullopt;
        plan.moves.push_back({p,p+offset(direction),cell->block});
    }
    std::reverse(plan.moves.begin(),plan.moves.end());
    return plan;
}
} // namespace Redstone
