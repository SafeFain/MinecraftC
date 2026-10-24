#include "network/ContentIds.h"
#include "network/EntityProtocol.h"
#include "plugins/Json.h"
#include "network/StatusEffectProtocol.h"
#include <cmath>
#include <set>

namespace Lan {
namespace {
uint16_t entityType(const std::string& key) {
    try{return Plugins::resolveEntity(key);}catch(const std::exception& error){throw ProtocolError(error.what());}
}
void entityStateValid(uint16_t type,const std::string& state,uint32_t revision) {
    const auto* definition=Plugins::entityContent(type);
    if(state.size()>MC_ENTITY_STATE_LIMIT||revision!=(definition?definition->stateVersion:0))throw ProtocolError("Invalid entity state revision/size");
    try{Plugins::Json::parse(state);}catch(const std::exception& error){throw ProtocolError(error.what());}
}
bool flag(Reader& reader) { const auto value = reader.u8(); if (value > 1) throw ProtocolError("Invalid entity flag"); return value != 0; }
void item(Writer& writer, ItemStack value) {
    if (!isValidItemId(value.id) ||
        value.count > getItemProps(value.id).maxStack || value.damage > getItemProps(value.id).maxDurability || ((value.id == ItemId::EMPTY) != (value.count == 0))) throw ProtocolError("Invalid entity item");
    writer.u16(encodeItemId(value.id)); writer.u8(value.count); writer.u16(value.damage);
}
ItemStack item(Reader& reader) { ItemStack result{decodeItemId(reader.u16()), reader.u8(), reader.u16()}; Writer check; item(check, result); return result; }
void position(Writer& writer, glm::dvec3 value) {
    for (int i = 0; i < 3; ++i) { if (!std::isfinite(value[i]) || std::abs(value[i]) > 30000000) throw ProtocolError("Invalid entity position"); writer.f64(value[i]); }
}
glm::dvec3 position(Reader& reader) { glm::dvec3 result; for (int i = 0; i < 3; ++i) result[i] = reader.f64(); Writer check; position(check, result); return result; }
void vector(Writer& writer, glm::vec3 value) { for (int i = 0; i < 3; ++i) writer.f32(value[i]); }
glm::vec3 vector(Reader& reader) { glm::vec3 result; for (int i = 0; i < 3; ++i) result[i] = reader.f32(); return result; }
void villager(Writer& writer, const VillagerData& value) {
    if (value.profession >= VillagerProfession::Count || value.level < 1 || value.level > 5 ||
        !std::isfinite(value.growthSeconds) || value.growthSeconds < 0) throw ProtocolError("Invalid villager appearance");
    writer.u8(static_cast<uint8_t>(value.profession)); writer.u8(value.level); writer.u16(value.experience); writer.u32(value.offerSeed);
    for (auto uses : value.uses) writer.u8(uses);
    writer.f32(value.growthSeconds); writer.u32(static_cast<uint32_t>(value.reputation));
    for (auto demand : value.demand) writer.u8(demand);
    writer.u8(value.professionLocked);
}
VillagerData villager(Reader& reader) {
    VillagerData result; result.profession = static_cast<VillagerProfession>(reader.u8()); result.level = reader.u8();
    result.experience = reader.u16(); result.offerSeed = reader.u32(); for (auto& uses : result.uses) uses = reader.u8();
    result.growthSeconds = reader.f32(); result.reputation = static_cast<int32_t>(reader.u32());
    for (auto& demand : result.demand) demand = reader.u8();
    result.professionLocked = flag(reader);
    Writer check; villager(check, result); return result;
}
void entity(Writer& writer, const EntitySnapshot& value) {
    if (!value.id || !Plugins::validEntity(static_cast<uint16_t>(value.type)) || value.health <= 0 || value.health > 10000 || value.age < 0 || value.hurt < 0 || value.burning < 0)
        throw ProtocolError("Invalid entity snapshot");
    writer.u64(value.id); writer.text(Plugins::entityKey(static_cast<uint16_t>(value.type))); position(writer, value.position);
    vector(writer, value.velocity); vector(writer, value.locomotion); vector(writer, value.facing);
    writer.f32(value.health); writer.f32(value.age); writer.f32(value.hurt); writer.f32(value.burning); item(writer, value.item); writer.u32(value.seed);
    writer.u8(value.inGround); writer.u8(value.playerOwned); writer.u8(value.sleeping); writer.u8(value.attacking);
    if (value.type == EntityType::Villager || value.type == EntityType::ZombieVillager) villager(writer, value.villager);
    entityStateValid(static_cast<uint16_t>(value.type),value.pluginState,value.pluginStateVersion);writer.text(value.pluginState,MC_ENTITY_STATE_LIMIT);writer.u32(value.pluginStateVersion);
    if(!validNetherMobState(value.nether))throw ProtocolError("Invalid native entity state");
#define NETHER_WIRE_uint8_t u8
#define NETHER_WIRE_uint16_t u16
#define NETHER_WIRE_uint32_t u32
#define NETHER_WRITE(type,name,initial) writer.NETHER_WIRE_##type(value.nether.name);
    MC_NETHER_STATE_FIELDS(NETHER_WRITE)
#undef NETHER_WRITE
    writeStatusEffects(writer,value.effects);writer.u64(value.riderId);
}
EntitySnapshot entity(Reader& reader) {
    EntitySnapshot result; result.id = reader.u64(); result.type = static_cast<EntityType>(entityType(reader.text())); result.position = position(reader);
    result.velocity = vector(reader); result.locomotion = vector(reader); result.facing = vector(reader);
    result.health = reader.f32(); result.age = reader.f32(); result.hurt = reader.f32(); result.burning = reader.f32(); result.item = item(reader); result.seed = reader.u32();
    result.inGround = flag(reader); result.playerOwned = flag(reader); result.sleeping = flag(reader); result.attacking = flag(reader);
    if (result.type == EntityType::Villager || result.type == EntityType::ZombieVillager) result.villager = villager(reader);
    result.pluginState=reader.text(MC_ENTITY_STATE_LIMIT);result.pluginStateVersion=reader.u32();
#define NETHER_READ(type,name,initial) result.nether.name=reader.NETHER_WIRE_##type();
    MC_NETHER_STATE_FIELDS(NETHER_READ)
#undef NETHER_READ
#undef NETHER_WIRE_uint8_t
#undef NETHER_WIRE_uint16_t
#undef NETHER_WIRE_uint32_t
    result.effects=readStatusEffects(reader);result.riderId=reader.u64();
    const auto* definition=Plugins::entityContent(static_cast<uint16_t>(result.type));
    if(definition&&definition->stateVersion!=result.pluginStateVersion)throw ProtocolError("Entity state revision mismatch");
    Writer check; entity(check, result); return result;
}
void death(Writer& writer, const EntityDeathSnapshot& value) {
    if (!value.id || !Plugins::validEntity(static_cast<uint16_t>(value.type)) || !deathPresentationVisible(value.elapsed)) throw ProtocolError("Invalid entity death snapshot");
    writer.u64(value.id); writer.text(Plugins::entityKey(static_cast<uint16_t>(value.type))); position(writer, value.position);
    vector(writer, value.velocity); vector(writer, value.facing); writer.u32(value.seed); writer.f32(value.elapsed);
}
EntityDeathSnapshot death(Reader& reader) {
    EntityDeathSnapshot result; result.id = reader.u64(); result.type = static_cast<EntityType>(entityType(reader.text())); result.position = position(reader);
    result.velocity = vector(reader); result.facing = vector(reader); result.seed = reader.u32(); result.elapsed = reader.f32();
    Writer check; death(check, result); return result;
}
}
Bytes encodeEntityBatch(const EntityBatch& batch) {
    if (!Plugins::validDimension(batch.dimension) || !batch.epoch || batch.entities.size() > MAX_VISIBLE_ENTITIES || batch.deaths.size() > MAX_VISIBLE_DEATHS)
        throw ProtocolError("Invalid entity batch");
    Writer writer; writer.u8(1); writeDimension(writer,batch.dimension); writer.u64(batch.epoch); writer.u64(batch.tick);
    std::set<uint64_t> ids; writer.u16(static_cast<uint16_t>(batch.entities.size()));
    for (const auto& value : batch.entities) { if (!ids.insert(value.id).second) throw ProtocolError("Duplicate entity ID"); entity(writer, value); }
    writer.u16(static_cast<uint16_t>(batch.deaths.size()));
    for (const auto& value : batch.deaths) { if (!ids.insert(value.id).second) throw ProtocolError("Duplicate entity ID"); death(writer, value); }
    return std::move(writer.bytes);
}
EntityBatch decodeEntityBatch(const Bytes& bytes) {
    Reader reader(bytes); EntityBatch result;
    if (reader.u8() != 1) throw ProtocolError("Unknown entity schema");
    result.dimension = readDimension(reader); result.epoch = reader.u64(); result.tick = reader.u64();
    const auto count = reader.u16(); if (count > MAX_VISIBLE_ENTITIES) throw ProtocolError("Too many entities");
    for (uint16_t i = 0; i < count; ++i) result.entities.push_back(entity(reader));
    const auto deaths = reader.u16(); if (deaths > MAX_VISIBLE_DEATHS) throw ProtocolError("Too many entity deaths");
    for (uint16_t i = 0; i < deaths; ++i) result.deaths.push_back(death(reader));
    reader.finish(); encodeEntityBatch(result); return result;
}
}
