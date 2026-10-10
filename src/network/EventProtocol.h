#pragma once
#include "network/Protocol.h"
#include "network/ContentIds.h"
#include "game/CombatRules.h"
#include "game/FishingSystem.h"
#include "world/Block.h"
#include <cmath>

namespace Lan {
enum class EventKind : uint8_t { BlockBreak, Critical, Sweep, Combat, Damage, Defense, Fishing, Explosion, Lightning, Interaction };
struct GameEvent {
    EventKind kind = EventKind::Explosion;
    DimensionId dimension = DimensionId::Overworld;
    uint64_t epoch = 1, owner = 0;
    glm::dvec3 position{0};
    BlockId block = BlockId::AIR;
    float amount = 0;
    uint8_t flags = 0;
};
inline Bytes encodeGameEvent(const GameEvent& event) {
    if (event.kind > EventKind::Interaction || event.dimension > DimensionId::Heaven || !event.epoch ||
        !Plugins::validBlock(event.block) || !std::isfinite(event.amount) || event.amount < 0 || event.amount > 100000 || event.flags > 7)
        throw ProtocolError("Invalid gameplay event");
    for (int i = 0; i < 3; ++i) if (!std::isfinite(event.position[i]) || std::abs(event.position[i]) > 30000000) throw ProtocolError("Invalid event position");
    if ((event.kind == EventKind::Combat && event.flags > static_cast<uint8_t>(AttackKind::Sweep)) ||
        (event.kind == EventKind::Fishing && event.flags > static_cast<uint8_t>(FishingEventKind::Reel))) throw ProtocolError("Invalid event subtype");
    Writer w; w.u8(1); w.u8(static_cast<uint8_t>(event.kind)); w.u8(static_cast<uint8_t>(event.dimension)); w.u64(event.epoch); w.u64(event.owner);
    for (int i = 0; i < 3; ++i) w.f64(event.position[i]);
    w.u16(encodeBlockId(static_cast<uint16_t>(event.block))); w.f32(event.amount); w.u8(event.flags); return std::move(w.bytes);
}
inline GameEvent decodeGameEvent(const Bytes& bytes) {
    Reader r(bytes); GameEvent e;
    if (r.u8() != 1) throw ProtocolError("Unknown event schema");
    e.kind = static_cast<EventKind>(r.u8()); e.dimension = static_cast<DimensionId>(r.u8()); e.epoch = r.u64(); e.owner = r.u64();
    for (int i = 0; i < 3; ++i) e.position[i] = r.f64();
    e.block = static_cast<BlockId>(decodeBlockId(r.u16())); e.amount = r.f32(); e.flags = r.u8(); r.finish(); (void)encodeGameEvent(e); return e;
}
}
