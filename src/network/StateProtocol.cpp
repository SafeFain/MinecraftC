#include "network/StatusEffectProtocol.h"
#include "world/BlockEntityLogic.h"
#include "network/ContentIds.h"
#include "network/StateProtocol.h"
#include "plugins/ContentQueries.h"
#include "plugins/GenerationValidation.h"
#include <cmath>
#include <set>

namespace Lan {
namespace {
bool boolean(Reader& reader) {
    const auto value = reader.u8();
    if (value > 1) throw ProtocolError("Invalid state flag");
    return value != 0;
}
void writeStack(Writer& writer, const ItemStack& stack) {
    if (!isValidItemId(stack.id) ||
        stack.count > getItemProps(stack.id).maxStack || stack.damage > getItemProps(stack.id).maxDurability || ((stack.id == ItemId::EMPTY) != (stack.count == 0)))
        throw ProtocolError("Invalid state item");
    writer.u16(encodeItemId(stack.id)); writer.u8(stack.count); writer.u16(stack.damage);
}
ItemStack readStack(Reader& reader) {
    const ItemStack stack{decodeItemId(reader.u16()), reader.u8(), reader.u16()};
    Writer check; writeStack(check, stack); return stack;
}
void positionValid(const glm::dvec3& position) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(position[i]) || std::abs(position[i]) > 30000000) throw ProtocolError("Invalid player position");
}
void fraction(float value) { if (value < 0 || value > 1 || !std::isfinite(value)) throw ProtocolError("Invalid state fraction"); }
void writePlayer(Writer& writer, const PlayerView& player) {
    positionValid(player.position);
    if (player.selected > 8 || player.mode > GameMode::Spectator || std::abs(player.yaw) > 360 || std::abs(player.pitch) > 89.9f ||
        player.visual.pose > PlayerPhysics::Pose::Crawling || player.fishing.phase > FishingPhase::Stuck)
        throw ProtocolError("Invalid player view");
    fraction(player.visual.swingProgress); fraction(player.visual.sleepProgress);
    fraction(player.visual.attackStrength); fraction(player.visual.bowCharge);
    writer.u64(player.id); writer.text(player.nickname, 64);
    for (int i = 0; i < 3; ++i) writer.f64(player.position[i]);
    writer.f32(player.yaw); writer.f32(player.pitch);
    for (int i = 0; i < 3; ++i) writer.f32(player.sleepFacing[i]);
    writer.u8(static_cast<uint8_t>(player.mode)); writer.u8(player.flying); writer.u8(player.selected);
    const auto& visual = player.visual;
    for (int i = 0; i < 3; ++i) writer.f32(visual.velocity[i]);
    writer.u8(visual.grounded); writer.u8(visual.sprinting); writer.u32(visual.swingSequence); writer.f32(visual.swingProgress);
    writer.u8(visual.sleeping); writer.f32(visual.sleepProgress); writer.f32(visual.attackStrength);
    writer.u8(static_cast<uint8_t>(visual.pose)); writer.u8(visual.bowCharging); writer.f32(visual.bowCharge); writer.u8(visual.blocking);
    writeStack(writer, player.mainhand); writeStack(writer, player.offhand);
    writer.u8(static_cast<uint8_t>(player.fishing.phase)); positionValid(player.fishing.position);
    for (int i = 0; i < 3; ++i) writer.f64(player.fishing.position[i]);
    writer.f32(player.fishing.animationSeconds); writer.f32(player.fishing.biteSeconds);
}
PlayerView readPlayer(Reader& reader) {
    PlayerView player;
    player.id = reader.u64(); player.nickname = reader.text(64);
    for (int i = 0; i < 3; ++i) player.position[i] = reader.f64();
    player.yaw = reader.f32(); player.pitch = reader.f32();
    for (int i = 0; i < 3; ++i) player.sleepFacing[i] = reader.f32();
    player.mode = static_cast<GameMode>(reader.u8()); player.flying = boolean(reader); player.selected = reader.u8();
    auto& visual = player.visual;
    for (int i = 0; i < 3; ++i) visual.velocity[i] = reader.f32();
    visual.grounded = boolean(reader); visual.sprinting = boolean(reader); visual.swingSequence = reader.u32(); visual.swingProgress = reader.f32();
    visual.sleeping = boolean(reader); visual.sleepProgress = reader.f32(); visual.attackStrength = reader.f32();
    visual.pose = static_cast<PlayerPhysics::Pose>(reader.u8()); visual.bowCharging = boolean(reader); visual.bowCharge = reader.f32(); visual.blocking = boolean(reader);
    player.mainhand = readStack(reader); player.offhand = readStack(reader);
    player.fishing.phase = static_cast<FishingPhase>(reader.u8());
    for (int i = 0; i < 3; ++i) player.fishing.position[i] = reader.f64();
    player.fishing.animationSeconds = reader.f32(); player.fishing.biteSeconds = reader.f32();
    Writer check; writePlayer(check, player); return player;
}
void stateValid(const AuthorityState& state) {
    if (state.sleepState > 3 || !Plugins::validDimension(state.dimension) || !state.epoch || state.others.size() > 7 || state.roster.size() > MAX_PLAYERS ||
        state.dayDuration < 1 || state.health < 0 || state.health > 20 || state.hunger > 20 ||
        state.saturation < 0 || state.saturation > 20 || state.exhaustion < 0 || state.exhaustion > 40)
        throw ProtocolError("Invalid authority state");
    if (state.window.kind > InventoryWindowKind::Container || (state.window.container && !isInventoryContainer(state.window.container->type)))
        throw ProtocolError("Invalid inventory window");
    std::set<uint64_t> rosterIds;
    Writer names;
    for (const auto& member : state.roster) {
        if (!Plugins::validDimension(member.dimension) || member.nickname.empty() || !rosterIds.insert(member.id).second)
            throw ProtocolError("Invalid room roster");
        names.text(member.nickname, 64);
    }
    fraction(state.phase); fraction(state.miningProgress);
    if (state.miningTarget) for (int i = 0; i < 3; ++i) if ((*state.miningTarget)[i] < -30000000 || (*state.miningTarget)[i] > 30000000) throw ProtocolError("Invalid mining target");
    std::set<uint64_t> ids{state.self.id};
    for (const auto& player : state.others)
        if (!ids.insert(player.id).second) throw ProtocolError("Duplicate player identity");
}
void writeRules(Writer& writer, const GameRuleSet& rules) {
    writer.u16(static_cast<uint16_t>(GAME_RULES.size() - 1));
    for (const auto& rule : GAME_RULES)
        if (rule.id != GameRuleId::DayNightDuration) writer.u64(static_cast<uint64_t>(rules.get(rule.id).number));
}
GameRuleSet readRules(Reader& reader) {
    GameRuleSet rules;
    if (reader.u16() != GAME_RULES.size() - 1) throw ProtocolError("Incompatible game rules");
    for (const auto& rule : GAME_RULES) {
        if (rule.id == GameRuleId::DayNightDuration) continue;
        const GameRuleValue next{rule.type, static_cast<int64_t>(reader.u64())};
        if (!validGameRuleValue(rule.id, next)) throw ProtocolError("Invalid world rule");
        rules.set(rule.id, next);
    }
    return rules;
}
}
Bytes encodeWorldInfo(const WorldInfo& value) {
    if(value.generation.size()!=Plugins::content().extensions.dimensions.size())throw ProtocolError("Dimension table mismatch");
    if (value.type > WorldType::Superflat || value.difficulty > Difficulty::Hard) throw ProtocolError("Invalid world description");
    Writer writer; writer.u8(1); writer.text(value.name, 256); writer.u64(value.seed); writer.u32(value.generationVersion);
    writer.u8(static_cast<uint8_t>(value.type)); writer.u8(static_cast<uint8_t>(value.difficulty));
    writeRules(writer, value.rules);
    writer.u32(static_cast<uint32_t>(value.generation.size()));
    for(const auto& entry:value.generation){const auto d=entry.first;const auto& g=entry.second;writeDimension(writer,static_cast<DimensionId>(d));writer.text(Plugins::dimensionContent(d).revision);try{Plugins::validateGeneration(g,d);}catch(const std::exception& error){throw ProtocolError(error.what());}
        if(g.modifiers.size()>64)throw ProtocolError("Too many modifiers");
        writer.text(g.generator);writer.text(g.revision);writer.text(g.parameters,4096);writer.u16(static_cast<uint16_t>(g.modifiers.size()));
        for(const auto& m:g.modifiers){writer.text(m.first);writer.text(m.second);}}
    return std::move(writer.bytes);
}
WorldInfo decodeWorldInfo(const Bytes& bytes) {
    Reader reader(bytes); WorldInfo value;
    if (reader.u8() != 1) throw ProtocolError("Unknown world schema");
    value.name = reader.text(256); value.seed = reader.u64(); value.generationVersion = reader.u32();
    value.type = static_cast<WorldType>(reader.u8()); value.difficulty = static_cast<Difficulty>(reader.u8());
    value.rules = readRules(reader);
    const auto dimensionCount=reader.u32();if(dimensionCount!=Plugins::content().extensions.dimensions.size())throw ProtocolError("Dimension table mismatch");
    value.generation.clear();
    for(uint32_t index=0;index<dimensionCount;++index){const auto d=static_cast<uint32_t>(readDimension(reader));if(value.generation.count(d)||reader.text()!=Plugins::dimensionContent(d).revision)throw ProtocolError("Dimension revision mismatch");auto& g=value.generation[d];g.generator=reader.text();g.revision=reader.text();g.parameters=reader.text(4096);
        const auto count=reader.u16();if(count>64)throw ProtocolError("Too many modifiers");
        for(uint16_t i=0;i<count;++i){auto key=reader.text();auto revision=reader.text();g.modifiers.emplace_back(key,revision);}}
    reader.finish(); encodeWorldInfo(value); return value;
}
Bytes encodeAuthorityState(const AuthorityState& state) {
    stateValid(state);
    if (state.difficulty > Difficulty::Hard) throw ProtocolError("Invalid difficulty");
    Writer writer; writer.u8(1); writeDimension(writer,state.dimension);
    writer.u64(state.epoch); writer.u64(state.tick); writer.u64(state.inputAcknowledged); writer.f32(state.phase); writer.u32(state.dayDuration);
    if(state.cycleOverride>86400000||state.advancing>2||state.actionCooldowns.size()>256)throw ProtocolError("Invalid plugin player policy");
    writer.u32(state.cycleOverride);writer.u32(state.advancing);writer.u16(static_cast<uint16_t>(state.actionCooldowns.size()));
    for(const auto& pair:state.actionCooldowns){if(!Plugins::content().extensions.actions.count(pair.first)||!std::isfinite(pair.second)||pair.second<0||pair.second>3600)throw ProtocolError("Invalid action cooldown");writer.text(pair.first);writer.f32(pair.second);}
    writer.u8(static_cast<uint8_t>(state.difficulty)); writeRules(writer, state.rules);
    writer.u8(state.weather.raining); writer.u8(state.weather.thundering); writer.u32(state.weather.rainTicks); writer.u32(state.weather.thunderTicks); writer.u64(state.weather.sequence);
    writer.u8(state.dead); writer.u8(state.loading); writer.u8(state.sleepState);
    for (int i = 0; i < 3; ++i) { writer.u32(static_cast<uint32_t>(state.bed[i])); writer.f32(state.sleepFacing[i]); }
    writer.f32(state.miningProgress); writer.u8(state.miningTarget.has_value());
    if (state.miningTarget) for (int i = 0; i < 3; ++i) writer.u32(static_cast<uint32_t>((*state.miningTarget)[i]));
    writePlayer(writer, state.self);
    writer.u8(static_cast<uint8_t>(state.others.size())); for (const auto& player : state.others) writePlayer(writer, player);
    writer.u8(static_cast<uint8_t>(state.roster.size()));
    for (const auto& member : state.roster) { writer.u64(member.id); writer.text(member.nickname, 64); writeDimension(writer,member.dimension); }
    for (const auto& stack : state.inventory.storage()) writeStack(writer, stack);
    for (const auto& stack : state.inventory.armor()) writeStack(writer, stack);
    writeStack(writer, state.inventory.offhand());
    writer.f32(state.health); writer.u8(state.hunger); writer.f32(state.saturation); writer.f32(state.exhaustion); writer.u32(state.foodTimer);writeStatusEffects(writer,state.effects);
    writer.u64(state.actionAcknowledged); writer.u64(state.window.id); writer.u8(static_cast<uint8_t>(state.window.kind));
    for (int i = 0; i < 3; ++i) writer.u32(static_cast<uint32_t>(state.window.position[i]));
    writer.u64(state.window.containerRevision); writer.u8(state.window.container.has_value());
    if (state.window.container) {
        const auto& container = *state.window.container; writer.u8(static_cast<uint8_t>(container.type));
        for (const auto& stack : container.chest) writeStack(writer, stack);
        writeStack(writer, container.input); writeStack(writer, container.fuel); writeStack(writer, container.output);
        writer.u16(container.burnRemaining); writer.u16(container.burnTotal); writer.u16(container.cookProgress); writer.u16(container.cookTotal);
    }
    writer.u64(state.inventoryRevision); writeStack(writer, state.cursor); for (const auto& stack : state.crafting) writeStack(writer, stack);
    return std::move(writer.bytes);
}
AuthorityState decodeAuthorityState(const Bytes& bytes) {
    Reader reader(bytes); AuthorityState state;
    if (reader.u8() != 1) throw ProtocolError("Unknown authority schema");
    state.dimension = readDimension(reader); state.epoch = reader.u64(); state.tick = reader.u64(); state.inputAcknowledged = reader.u64();
    state.phase = reader.f32(); state.dayDuration = reader.u32();
    state.cycleOverride=reader.u32();state.advancing=reader.u32();const auto cooldowns=reader.u16();if(cooldowns>256)throw ProtocolError("Too many cooldowns");for(uint16_t i=0;i<cooldowns;++i){const auto key=reader.text();const auto remaining=reader.f32();if(!state.actionCooldowns.emplace(key,remaining).second)throw ProtocolError("Duplicate cooldown");}
    state.difficulty = static_cast<Difficulty>(reader.u8()); state.rules = readRules(reader);
    if (state.difficulty > Difficulty::Hard) throw ProtocolError("Invalid difficulty");
    state.weather.raining = boolean(reader); state.weather.thundering = boolean(reader);
    state.weather.rainTicks = reader.u32(); state.weather.thunderTicks = reader.u32(); state.weather.sequence = reader.u64();
    state.dead = boolean(reader); state.loading = boolean(reader); state.sleepState = reader.u8();
    for (int i = 0; i < 3; ++i) { state.bed[i] = static_cast<int32_t>(reader.u32()); state.sleepFacing[i] = reader.f32(); }
    state.miningProgress = reader.f32();
    if (boolean(reader)) { glm::ivec3 target; for (int i = 0; i < 3; ++i) target[i] = static_cast<int32_t>(reader.u32()); state.miningTarget = target; }
    state.self = readPlayer(reader);
    const auto count = reader.u8(); if (count > 7) throw ProtocolError("Too many players");
    for (uint8_t i = 0; i < count; ++i) state.others.push_back(readPlayer(reader));
    const auto rosterCount = reader.u8(); if (rosterCount > MAX_PLAYERS) throw ProtocolError("Room roster too large");
    for (uint8_t i = 0; i < rosterCount; ++i) {
        RoomPlayer member; member.id = reader.u64(); member.nickname = reader.text(64); member.dimension = readDimension(reader); state.roster.push_back(std::move(member));
    }
    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) state.inventory.slot(i) = readStack(reader);
    for (auto& stack : state.inventory.armor()) stack = readStack(reader);
    state.inventory.offhand() = readStack(reader);
    state.health = reader.f32(); state.hunger = reader.u8(); state.saturation = reader.f32(); state.exhaustion = reader.f32(); state.foodTimer = reader.u32();state.effects=readStatusEffects(reader);
    state.actionAcknowledged = reader.u64(); state.window.id = reader.u64(); state.window.kind = static_cast<InventoryWindowKind>(reader.u8());
    for (int i = 0; i < 3; ++i) state.window.position[i] = static_cast<int32_t>(reader.u32());
    state.window.containerRevision = reader.u64();
    if (boolean(reader)) {
        state.window.container.emplace(); auto& container = *state.window.container;
        container.type = static_cast<BlockEntityType>(reader.u8());
        for (auto& stack : container.chest) stack = readStack(reader);
        container.input = readStack(reader); container.fuel = readStack(reader); container.output = readStack(reader);
        container.burnRemaining = reader.u16(); container.burnTotal = reader.u16(); container.cookProgress = reader.u16(); container.cookTotal = reader.u16();
    }
    state.inventoryRevision = reader.u64(); state.cursor = readStack(reader); for (auto& stack : state.crafting) stack = readStack(reader);
    reader.finish(); stateValid(state); return state;
}
}
