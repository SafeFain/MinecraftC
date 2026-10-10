#include "network/GameplayProtocol.h"
#include <cmath>

namespace Lan {
namespace {
void validate(const PlayerInput& value) {
    if ((value.dimension != DimensionId::Overworld && value.dimension != DimensionId::Heaven) ||
        !std::isfinite(value.forward) || std::abs(value.forward) > 1 ||
        !std::isfinite(value.strafe) || std::abs(value.strafe) > 1 ||
        !std::isfinite(value.yaw) || std::abs(value.yaw) > 360 ||
        !std::isfinite(value.pitch) || std::abs(value.pitch) > 89.9f ||
        value.selected > 8 || (value.buttons & ~InputButtons::All) ||
        value.radius < 2 || value.radius > 16 || value.lodRadius > 4096 || (value.lodRadius && value.lodRadius < 16))
        throw ProtocolError("Invalid player input");
}
}
Bytes encodePlayerInput(const PlayerInput& value) {
    validate(value);
    Writer writer;
    writer.u8(1);
    writer.u8(static_cast<uint8_t>(value.dimension));
    writer.f32(value.forward); writer.f32(value.strafe);
    writer.f32(value.yaw); writer.f32(value.pitch);
    writer.u8(value.selected); writer.u8(value.buttons); writer.u8(value.radius); writer.u16(value.lodRadius);
    return std::move(writer.bytes);
}
PlayerInput decodePlayerInput(const Bytes& bytes) {
    Reader reader(bytes);
    if (reader.u8() != 1) throw ProtocolError("Unknown player input schema");
    PlayerInput value;
    value.dimension = static_cast<DimensionId>(reader.u8());
    value.forward = reader.f32(); value.strafe = reader.f32();
    value.yaw = reader.f32(); value.pitch = reader.f32();
    value.selected = reader.u8(); value.buttons = reader.u8(); value.radius = reader.u8(); value.lodRadius = reader.u16();
    reader.finish(); validate(value);
    return value;
}
}

namespace Lan {
namespace {
void slotValid(InventorySlot slot) {
    const std::array<uint8_t, 6> limits{36, 4, 1, 9, 27, 1};
    const auto area = static_cast<size_t>(slot.area);
    if (area >= limits.size() || slot.index >= limits[area]) throw ProtocolError("Invalid inventory slot");
}
void actionValid(const GameAction& action) {
    if (!action.sequence || !action.epoch || action.kind > ActionKind::Trade ||
        action.windowKind > InventoryWindowKind::Container ||
        action.inventory.operation > InventoryOperation::CreativeClone || action.inventory.targets.size() > 64)
        throw ProtocolError("Invalid game action");
    slotValid(action.inventory.slot);
    for (auto slot : action.inventory.targets) slotValid(slot);
    for (int i = 0; i < 3; ++i)
        if (action.position[i] < -30000000 || action.position[i] > 30000000) throw ProtocolError("Invalid window position");
}
}
Bytes encodeGameAction(const GameAction& action) {
    actionValid(action);
    Writer writer; writer.u8(1); writer.u64(action.sequence); writer.u64(action.epoch); writer.u64(action.window); writer.u64(action.target);
    writer.u8(static_cast<uint8_t>(action.kind)); writer.u8(static_cast<uint8_t>(action.windowKind));
    for (int i = 0; i < 3; ++i) writer.u32(static_cast<uint32_t>(action.position[i]));
    const auto& inventory = action.inventory;
    writer.u64(inventory.revision); writer.u64(inventory.containerRevision);
    writer.u8(static_cast<uint8_t>(inventory.operation)); writer.u8(static_cast<uint8_t>(inventory.slot.area)); writer.u8(inventory.slot.index);
    writer.u16(inventory.argument); writer.u8(inventory.alternate); writer.u8(static_cast<uint8_t>(inventory.targets.size()));
    for (auto target : inventory.targets) { writer.u8(static_cast<uint8_t>(target.area)); writer.u8(target.index); }
    return std::move(writer.bytes);
}
GameAction decodeGameAction(const Bytes& bytes) {
    Reader reader(bytes); GameAction action;
    if (reader.u8() != 1) throw ProtocolError("Unknown game action schema");
    action.sequence = reader.u64(); action.epoch = reader.u64(); action.window = reader.u64(); action.target = reader.u64();
    action.kind = static_cast<ActionKind>(reader.u8()); action.windowKind = static_cast<InventoryWindowKind>(reader.u8());
    for (int i = 0; i < 3; ++i) action.position[i] = static_cast<int32_t>(reader.u32());
    auto& inventory = action.inventory;
    inventory.revision = reader.u64(); inventory.containerRevision = reader.u64();
    inventory.operation = static_cast<InventoryOperation>(reader.u8()); inventory.slot.area = static_cast<InventoryArea>(reader.u8()); inventory.slot.index = reader.u8();
    inventory.argument = reader.u16(); const auto alternate = reader.u8();
    if (alternate > 1) throw ProtocolError("Invalid action flag");
    inventory.alternate = alternate != 0;
    const auto count = reader.u8(); if (count > 64) throw ProtocolError("Too many inventory targets");
    for (uint8_t i = 0; i < count; ++i) inventory.targets.push_back({static_cast<InventoryArea>(reader.u8()), reader.u8()});
    reader.finish(); actionValid(action); return action;
}
}
