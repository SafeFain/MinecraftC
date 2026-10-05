#include "plugins/ContentRegistry.h"
#include "game/SaveStore.h"
#include "Config.h"
#include "core/Platform.h"
#include "entity/EntityLogic.h"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>
#include <set>

namespace {

using Bytes = std::vector<uint8_t>;
constexpr std::array<char, 8> MAGIC = {'M', 'C', 'C', 'S', 'A', 'V', 'E', '\0'};
constexpr uint32_t MAX_PAYLOAD = 16 * 1024 * 1024;

template<typename T>
struct AlwaysFalse : std::false_type {};

template<typename UInt>
void appendUnsigned(Bytes& bytes, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (size_t byte = 0; byte < sizeof(UInt); ++byte) {
        bytes.push_back(static_cast<uint8_t>(value & static_cast<UInt>(0xff)));
        value >>= 8;
    }
}

template<typename T>
void append(Bytes& bytes, const T& value) {
    if constexpr (std::is_same_v<T, uint8_t>) {
        bytes.push_back(value);
    } else if constexpr (std::is_same_v<T, uint16_t> ||
                         std::is_same_v<T, uint32_t> ||
                         std::is_same_v<T, uint64_t>) {
        appendUnsigned(bytes, value);
    } else if constexpr (std::is_same_v<T, int32_t>) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendUnsigned(bytes, bits);
    } else if constexpr (std::is_same_v<T, float>) {
        static_assert(sizeof(float) == sizeof(uint32_t));
        static_assert(std::numeric_limits<float>::is_iec559);
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendUnsigned(bytes, bits);
    } else if constexpr (std::is_same_v<T, double>) {
        static_assert(sizeof(double) == sizeof(uint64_t));
        static_assert(std::numeric_limits<double>::is_iec559);
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendUnsigned(bytes, bits);
    } else if constexpr (std::is_same_v<T, glm::vec3> ||
                         std::is_same_v<T, glm::dvec3> ||
                         std::is_same_v<T, glm::ivec3>) {
        append(bytes, value.x);
        append(bytes, value.y);
        append(bytes, value.z);
    } else {
        static_assert(AlwaysFalse<T>::value, "Unsupported save field type");
    }
}

void appendString(Bytes& bytes, const std::string& value) {
    if (value.size() > std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("World name is too long");
    const auto length = static_cast<uint16_t>(value.size());
    append(bytes, length);
    bytes.insert(bytes.end(), value.begin(), value.end());
}

class Reader {
public:
    explicit Reader(Bytes bytes, uint32_t version=0, bool inspection=false) : inspectOnly(inspection), m_bytes(std::move(bytes)) {
        if (version < 16) return;
        const uint32_t requirementCount=read<uint32_t>();
        if (requirementCount>4096) throw std::runtime_error("Too many plugin requirements");
        std::set<std::string> pluginIds;
        for (uint32_t i=0;i<requirementCount;++i) {
            const auto id=readString(), fingerprint=readString();
            if (!Plugins::validKey(id+":test") || !pluginIds.insert(id).second) throw std::runtime_error("Invalid plugin requirement");
            requirements.emplace_back(id,fingerprint);
        }
        auto expected=Plugins::content().requirements, saved=requirements;
        std::sort(expected.begin(),expected.end());std::sort(saved.begin(),saved.end());
        if (expected!=saved) {
            compatibilityError="Required content/behavior plugin set differs";
            for (const auto& plugin:saved) if(std::find(expected.begin(),expected.end(),plugin)==expected.end()) compatibilityError+="; "+plugin.first+" ("+plugin.second+")";
            if(!inspection) throw std::runtime_error(compatibilityError);
        }
        auto palette=[&](bool blocks) {
            const uint32_t count=read<uint32_t>();
            if(count>65535) throw std::runtime_error("Plugin palette is too large");
            auto& map=blocks?blockMap:itemMap;std::set<std::string> keys;
            for(uint32_t i=0;i<count;++i) {
                const uint16_t raw=read<uint16_t>();const std::string key=readString();
                if(raw<Plugins::FIRST_CONTENT_ID||raw==Plugins::INVALID_CONTENT_ID||!Plugins::validKey(key)||map.count(raw)||!keys.insert(key).second) throw std::runtime_error("Invalid plugin palette");
                uint16_t resolved=Plugins::INVALID_CONTENT_ID;
                try {resolved=blocks?static_cast<uint16_t>(Plugins::resolveBlock(key)):static_cast<uint16_t>(Plugins::resolveItem(key));}
                catch(const std::exception&) {if(!inspection)throw;compatibilityError+="; missing content "+key;}
                map.emplace(raw,resolved);
            }
        };
        palette(true);palette(false);
    }
    bool inspectOnly=false;
    std::vector<std::pair<std::string,std::string>> requirements;
    std::string compatibilityError;
    std::map<uint16_t,uint16_t> blockMap,itemMap;
    uint16_t contentId(uint16_t raw, bool block) const {
        const uint16_t count=block?static_cast<uint16_t>(BlockId::COUNT):static_cast<uint16_t>(ItemId::COUNT);
        if(raw<count)return raw;
        const auto& map=block?blockMap:itemMap;const auto it=map.find(raw);
        if(it==map.end())throw std::runtime_error("Unmapped serialized content ID");
        return it->second;
    }

    template<typename T>
    T read() {
        if constexpr (std::is_same_v<T, uint8_t>) {
            return readUnsigned<uint8_t>();
        } else if constexpr (std::is_same_v<T, uint16_t> ||
                             std::is_same_v<T, uint32_t> ||
                             std::is_same_v<T, uint64_t>) {
            return readUnsigned<T>();
        } else if constexpr (std::is_same_v<T, int32_t>) {
            const uint32_t bits = readUnsigned<uint32_t>();
            int32_t value = 0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        } else if constexpr (std::is_same_v<T, float>) {
            const uint32_t bits = readUnsigned<uint32_t>();
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        } else if constexpr (std::is_same_v<T, double>) {
            const uint64_t bits = readUnsigned<uint64_t>();
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            return {read<float>(), read<float>(), read<float>()};
        } else if constexpr (std::is_same_v<T, glm::dvec3>) {
            return {read<double>(), read<double>(), read<double>()};
        } else if constexpr (std::is_same_v<T, glm::ivec3>) {
            return {read<int32_t>(), read<int32_t>(), read<int32_t>()};
        } else {
            static_assert(AlwaysFalse<T>::value, "Unsupported save field type");
        }
    }

    std::string readString() {
        const uint16_t length = read<uint16_t>();
        if (m_offset + length > m_bytes.size())
            throw std::runtime_error("Truncated save string");
        std::string value(reinterpret_cast<const char*>(m_bytes.data() + m_offset), length);
        m_offset += length;
        return value;
    }

    bool finished() const { return m_offset == m_bytes.size(); }
    size_t remaining() const { return m_bytes.size() - m_offset; }

    Bytes readBytes(size_t count) {
        if (count > remaining()) throw std::runtime_error("Truncated save payload");
        Bytes result(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                     m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + count));
        m_offset += count;
        return result;
    }

private:
    template<typename UInt>
    UInt readUnsigned() {
        static_assert(std::is_unsigned_v<UInt>);
        if (m_offset + sizeof(UInt) > m_bytes.size())
            throw std::runtime_error("Truncated save payload");
        UInt value = 0;
        for (size_t byte = 0; byte < sizeof(UInt); ++byte) {
            value |= static_cast<UInt>(m_bytes[m_offset++]) << (byte * 8);
        }
        return value;
    }

    Bytes m_bytes;
    size_t m_offset = 0;
};

uint64_t checksum(const Bytes& bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Generated chunks are predominantly long vertical runs of air, stone and
// sediment.  A tiny deterministic RLE codec keeps the cache self-contained
// and avoids adding a platform compression dependency.  The encoded stream is
// a sequence of (little-endian uint16 run length, uint16 value) pairs.
constexpr uint8_t GENERATED_CACHE_RAW = 0;
constexpr uint8_t GENERATED_CACHE_RLE = 1;

Bytes encodeChunkRle(const std::vector<uint16_t>& blocks) {
    Bytes encoded;
    encoded.reserve(blocks.size() / 2);
    size_t offset = 0;
    while (offset < blocks.size()) {
        const uint16_t value = blocks[offset];
        size_t run = 1;
        while (offset + run < blocks.size() && blocks[offset + run] == value &&
               run < std::numeric_limits<uint16_t>::max()) {
            ++run;
        }
        append(encoded, static_cast<uint16_t>(run));
        append(encoded, value);
        offset += run;
    }
    return encoded;
}

std::vector<uint16_t> decodeChunkRle(const Bytes& encoded, size_t expectedSize,
                                      bool wide) {
    const size_t stride = wide ? 4 : 3;
    if (encoded.size() % stride != 0)
        throw std::runtime_error("Invalid generated chunk RLE payload");
    std::vector<uint16_t> decoded;
    decoded.reserve(expectedSize);
    for (size_t offset = 0; offset < encoded.size(); offset += stride) {
        const uint16_t run = static_cast<uint16_t>(encoded[offset]) |
            static_cast<uint16_t>(encoded[offset + 1]) << 8;
        const uint16_t value = static_cast<uint16_t>(encoded[offset + 2]) |
            (wide ? static_cast<uint16_t>(encoded[offset + 3]) << 8 : 0);
        if (run == 0 || decoded.size() + run > expectedSize)
            throw std::runtime_error("Generated chunk RLE size mismatch");
        decoded.insert(decoded.end(), run, value);
    }
    if (decoded.size() != expectedSize)
        throw std::runtime_error("Generated chunk RLE is truncated");
    return decoded;
}

void writeAtomic(const std::filesystem::path& path, const Bytes& body) {
    Bytes payload;
    append(payload, static_cast<uint32_t>(Plugins::content().requirements.size()));
    for(const auto& p:Plugins::content().requirements){appendString(payload,p.first);appendString(payload,p.second);}
    append(payload,static_cast<uint32_t>(Plugins::content().blocks.size()));
    for(const auto& p:Plugins::content().blocks){append(payload,p.first);appendString(payload,p.second.key);}
    append(payload,static_cast<uint32_t>(Plugins::content().items.size()));
    for(const auto& p:Plugins::content().items){append(payload,p.first);appendString(payload,p.second.key);}
    payload.insert(payload.end(),body.begin(),body.end());
    if(payload.size()>MAX_PAYLOAD)throw std::runtime_error("Save payload exceeds limit");
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot open temporary save file");
        Bytes header;
        header.insert(header.end(), MAGIC.begin(), MAGIC.end());
        const uint32_t version = SAVE_FORMAT_VERSION;
        const uint32_t size = static_cast<uint32_t>(payload.size());
        const uint64_t hash = checksum(payload);
        append(header, version);
        append(header, size);
        append(header, hash);
        output.write(reinterpret_cast<const char*>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        output.write(reinterpret_cast<const char*>(payload.data()),
                     static_cast<std::streamsize>(payload.size()));
        output.flush();
        if (!output) throw std::runtime_error("Failed while writing save file");
    }
    std::error_code error;
    if (!Platform::replaceFileAtomically(temporary, path, error)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot atomically replace save file: " + error.message());
    }
}

struct CheckedBytes {
    Bytes payload;
    uint32_t version = 0;
};

CheckedBytes readChecked(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open save file: " + path.string());
    std::array<char, 8> magic{};
    input.read(magic.data(), magic.size());
    Bytes encodedHeader(sizeof(uint32_t) * 2 + sizeof(uint64_t));
    input.read(reinterpret_cast<char*>(encodedHeader.data()),
               static_cast<std::streamsize>(encodedHeader.size()));
    if (!input || magic != MAGIC) throw std::runtime_error("Invalid save header");
    Reader header(std::move(encodedHeader));
    const uint32_t version = header.read<uint32_t>();
    const uint32_t size = header.read<uint32_t>();
    const uint64_t expectedHash = header.read<uint64_t>();
    if (version > SAVE_FORMAT_VERSION) throw std::runtime_error("Save was made by a newer version");
    if (version < 2) throw std::runtime_error("Unsupported save version");
    if (size > MAX_PAYLOAD) throw std::runtime_error("Save payload exceeds safety limit");
    Bytes payload(size);
    input.read(reinterpret_cast<char*>(payload.data()), size);
    if (!input || input.peek() != std::ifstream::traits_type::eof())
        throw std::runtime_error("Invalid save payload length");
    if (checksum(payload) != expectedHash) throw std::runtime_error("Save checksum mismatch");
    return {std::move(payload), version};
}

void appendStack(Bytes& payload, const ItemStack& stack) {
    append(payload, static_cast<uint16_t>(stack.id));
    append(payload, stack.count);
    append(payload, stack.damage);
}

ItemStack readStack(Reader& reader) {
    ItemStack stack;
    stack.id = static_cast<ItemId>(reader.contentId(reader.read<uint16_t>(), false));
    stack.count = reader.read<uint8_t>();
    stack.damage = reader.read<uint16_t>();
    if (reader.inspectOnly && !isValidItemId(stack.id)) return stack;
    if (!isValidItemId(stack.id)) throw std::runtime_error("Save contains invalid item id");
    if (stack.empty()) {
        stack.clear();
    } else {
        const auto& props = getItemProps(stack.id);
        if (stack.count > props.maxStack || stack.damage > props.maxDurability)
            throw std::runtime_error("Save contains invalid item stack");
    }
    return stack;
}

void appendEntity(Bytes& payload, const WorldMetadata::PersistedEntity& entity) {
    append(payload, entity.type);
    append(payload, entity.position);
    append(payload, entity.velocity);
    append(payload, entity.health);
    append(payload, entity.ageSeconds);
    appendStack(payload, entity.item);
    append(payload, entity.behaviorSeed);
    append(payload, entity.flags);
    append(payload, entity.projectileDamage);
    append(payload, static_cast<uint8_t>(entity.villager.profession));
    append(payload, entity.villager.level);
    append(payload, entity.villager.experience);
    append(payload, entity.villager.offerSeed);
    for (const uint8_t uses : entity.villager.uses) append(payload, uses);
    append(payload, entity.villager.claimedBed);
    append(payload, entity.villager.claimedWorkstation);
    append(payload, static_cast<uint8_t>(
        (entity.villager.hasBed ? 1 : 0) |
        (entity.villager.hasWorkstation ? 2 : 0) |
        (entity.villager.professionLocked ? 4 : 0)));
    append(payload, entity.villager.lastRestockDay);
    append(payload, entity.villager.restocksToday);
    for(const auto& stack:entity.villager.food)appendStack(payload,stack);
    append(payload,entity.villager.growthSeconds);
    append(payload,entity.villager.breedingCooldown);
    append(payload,entity.villager.defenseCooldown);
    append(payload,entity.villager.reputation);
    for(auto value:entity.villager.demand)append(payload,value);
    append(payload,entity.villager.reputationDay);
    append(payload,entity.villager.tradesToday);
}

WorldMetadata::PersistedEntity readEntity(Reader& reader, uint32_t version) {
    WorldMetadata::PersistedEntity entity;
    entity.type = reader.read<uint8_t>();
    entity.position = version >= 5 ? reader.read<glm::dvec3>()
                                   : glm::dvec3(reader.read<glm::vec3>());
    entity.velocity = reader.read<glm::vec3>();
    entity.health = reader.read<float>();
    entity.ageSeconds = reader.read<float>();
    entity.item = readStack(reader);
    entity.behaviorSeed = reader.read<uint32_t>();
    if (version >= 5) {
        entity.flags = reader.read<uint8_t>();
        entity.projectileDamage = reader.read<float>();
    }
    if (version >= 12) {
        entity.villager.profession = static_cast<VillagerProfession>(
            reader.read<uint8_t>());
        entity.villager.level = reader.read<uint8_t>();
        entity.villager.experience = reader.read<uint16_t>();
        entity.villager.offerSeed = reader.read<uint32_t>();
        for (uint8_t& uses : entity.villager.uses) uses = reader.read<uint8_t>();
        entity.villager.claimedBed = reader.read<glm::ivec3>();
        entity.villager.claimedWorkstation = reader.read<glm::ivec3>();
        const uint8_t villagerFlags = reader.read<uint8_t>();
        entity.villager.hasBed = (villagerFlags & 1) != 0;
        entity.villager.hasWorkstation = (villagerFlags & 2) != 0;
        entity.villager.professionLocked = (villagerFlags & 4) != 0;
        entity.villager.lastRestockDay = reader.read<uint32_t>();
        entity.villager.restocksToday = reader.read<uint8_t>();
        if (entity.villager.profession >= VillagerProfession::Count ||
            (version<17 && entity.villager.profession>=VillagerProfession::Fisherman) ||
            entity.villager.level < 1 || entity.villager.level > 5)
            throw std::runtime_error("Save contains invalid villager data");
    }
    if(version>=17) {
        for(auto& stack:entity.villager.food)stack=readStack(reader);
        entity.villager.growthSeconds=reader.read<float>();
        entity.villager.breedingCooldown=reader.read<float>();
        entity.villager.defenseCooldown=reader.read<float>();
        entity.villager.reputation=reader.read<int32_t>();
        for(auto& value:entity.villager.demand)value=reader.read<uint8_t>();
        entity.villager.reputationDay=reader.read<uint32_t>();
        entity.villager.tradesToday=reader.read<uint8_t>();
        const auto timerValid=[](float v,float maximum){return std::isfinite(v)&&v>=0&&v<=maximum;};
        const auto& v=entity.villager;
        if(!timerValid(v.growthSeconds,1200) || !timerValid(v.breedingCooldown,300) ||
           !timerValid(v.defenseCooldown,600) || v.reputation < -100 || v.reputation>100 ||
           v.tradesToday>5 || std::any_of(v.demand.begin(),v.demand.end(),[](uint8_t d){return d>25;}))
            throw std::runtime_error("Save contains invalid village lifecycle data");
        for(const auto& stack:v.food)if(!stack.empty() && stack.id!=ItemId::BREAD &&
            stack.id!=ItemId::WHEAT && stack.id!=ItemId::WHEAT_SEEDS)
            throw std::runtime_error("Save contains invalid villager food");
    }
    // Appended entity types are legal only in the versions that introduced them.
    if (entity.type > static_cast<uint8_t>(version>=17 ? EntityType::IronGolem : EntityType::ZombieVillager))
        throw std::runtime_error("Save contains invalid entity type");
    return entity;
}

void appendBlockEntity(Bytes& payload, const PersistedBlockEntity& entity) {
    append(payload, entity.localIndex);
    append(payload, static_cast<uint8_t>(entity.value.type));
    if (entity.value.type == BlockEntityType::Chest) {
        for (const auto& stack : entity.value.chest) appendStack(payload, stack);
    } else if (entity.value.type == BlockEntityType::Button) {
        append(payload, entity.value.buttonRemaining);
    } else {
        appendStack(payload, entity.value.input);
        appendStack(payload, entity.value.fuel);
        appendStack(payload, entity.value.output);
        append(payload, entity.value.burnRemaining);
        append(payload, entity.value.burnTotal);
        append(payload, entity.value.cookProgress);
        append(payload, entity.value.cookTotal);
    }
}

PersistedBlockEntity readBlockEntity(Reader& reader, uint32_t version) {
    PersistedBlockEntity entity;
    entity.localIndex = version >= 6 ? reader.read<uint32_t>() : reader.read<uint16_t>();
    if (entity.localIndex >= static_cast<uint32_t>(Config::CHUNK_VOLUME))
        throw std::runtime_error("Invalid block entity position");
    const uint8_t type = reader.read<uint8_t>();
    if (type > static_cast<uint8_t>(version >= 18 ? BlockEntityType::Button : BlockEntityType::Furnace))
        throw std::runtime_error("Invalid block entity type");
    entity.value.type = static_cast<BlockEntityType>(type);
    if (entity.value.type == BlockEntityType::Chest) {
        for (auto& stack : entity.value.chest) stack = readStack(reader);
    } else if (entity.value.type == BlockEntityType::Button) {
        entity.value.buttonRemaining = reader.read<uint16_t>();
        if (entity.value.buttonRemaining > 30) throw std::runtime_error("Invalid button timer");
    } else {
        entity.value.input = readStack(reader);
        entity.value.fuel = readStack(reader);
        entity.value.output = readStack(reader);
        entity.value.burnRemaining = reader.read<uint16_t>();
        entity.value.burnTotal = reader.read<uint16_t>();
        entity.value.cookProgress = reader.read<uint16_t>();
        entity.value.cookTotal = reader.read<uint16_t>();
        if (entity.value.cookTotal == 0) entity.value.cookTotal = 200;
    }
    return entity;
}

} // namespace

SaveStore::SaveStore(std::filesystem::path worldDirectory)
    : m_worldDirectory(std::move(worldDirectory)) {}

bool SaveStore::exists() const {
    return std::filesystem::is_regular_file(m_worldDirectory / "level.bin");
}

void SaveStore::saveMetadata(const WorldMetadata& metadata) const {
    Bytes payload;
    appendString(payload, metadata.displayName);
    append(payload, metadata.seed);
    append(payload, metadata.generationVersion);
    append(payload, metadata.rulesetVersion);
    append(payload, static_cast<uint8_t>(metadata.gameMode));
    append(payload, static_cast<uint8_t>(metadata.difficulty));
    append(payload, static_cast<uint8_t>(metadata.cheatsEnabled));
    append(payload, metadata.worldTicks);
    append(payload, static_cast<uint8_t>(metadata.weather.raining));
    append(payload, static_cast<uint8_t>(metadata.weather.thundering));
    append(payload, metadata.weather.rainTicks);
    append(payload, metadata.weather.thunderTicks);
    append(payload, metadata.weather.sequence);
    append(payload, metadata.playerPosition);
    append(payload, metadata.worldSpawn);
    append(payload, static_cast<uint8_t>(metadata.bedSpawn.has_value()));
    if (metadata.bedSpawn) append(payload, *metadata.bedSpawn);
    append(payload, metadata.health);
    append(payload, metadata.hunger);
    append(payload, metadata.saturation);
    append(payload, metadata.exhaustion);
    for (const auto& stack : metadata.inventory.storage()) appendStack(payload, stack);
    for (const auto& stack : metadata.inventory.armor()) appendStack(payload, stack);
    appendStack(payload, metadata.inventory.offhand());
    append(payload, static_cast<uint32_t>(metadata.entities.size()));
    for (const auto& entity : metadata.entities) appendEntity(payload, entity);
    // Keep the new field at the payload tail so v2-v9 field offsets remain
    // readable without a migration pass.
    append(payload, static_cast<uint8_t>(metadata.worldType));
    // Dimension state is appended in v10.  This keeps every pre-v10 field at
    // its historical offset and lets older saves continue to load normally.
    append(payload, static_cast<uint8_t>(metadata.activeDimension));
    append(payload, metadata.overworldDayPhase);
    append(payload, metadata.heaven.playerPosition);
    append(payload, metadata.heaven.safePosition);
    append(payload, static_cast<uint8_t>(metadata.heaven.hasSafePosition));
    append(payload, metadata.heaven.worldTicks);
    append(payload, metadata.heaven.dayPhase);
    // v11 persists the Java-style food timer at the payload tail.
    append(payload, metadata.foodTickTimer);
    // v14 stores the world-wide day/night duration in seconds.
    append(payload, metadata.dayNightDurationSeconds);
    const size_t ruleCount = static_cast<size_t>(GameRuleId::DayNightDuration) + metadata.gameRules.unknown.size();
    if (ruleCount > 1024) throw std::runtime_error("Too many saved game rules");
    append(payload, static_cast<uint32_t>(ruleCount));
    std::set<std::string> ruleNames;
    auto appendRule = [&](const std::string& name, GameRuleValue value) {
        if (name.empty() || name.size() > 256 || !ruleNames.insert(name).second ||
            (value.type != GameRuleType::Boolean && value.type != GameRuleType::Integer) ||
            (value.type == GameRuleType::Boolean && value.number != 0 && value.number != 1) ||
            (value.type == GameRuleType::Integer && (value.number < INT32_MIN || value.number > INT32_MAX)))
            throw std::runtime_error("Invalid saved game rule");
        appendString(payload, name);
        append(payload, static_cast<uint8_t>(value.type));
        append(payload, static_cast<int32_t>(value.number));
    };
    for (const auto& rule : GAME_RULES) {
        if (rule.id == GameRuleId::DayNightDuration) continue;
        const auto value = metadata.gameRules.get(rule.id);
        if (!validGameRuleValue(rule.id, value)) throw std::runtime_error("Invalid saved game rule value");
        appendRule(std::string(rule.fullName), value);
    }
    for (const auto& rule : metadata.gameRules.unknown) {
        if (findGameRule(rule.name)) throw std::runtime_error("Unknown game rule conflicts with registry");
        appendRule(rule.name, rule.value);
    }
    writeAtomic(m_worldDirectory / "level.bin", payload);
}

WorldMetadata SaveStore::loadMetadata(bool inspection) const {
    CheckedBytes checked = readChecked(m_worldDirectory / "level.bin");
    Reader reader(std::move(checked.payload), checked.version, inspection);
    WorldMetadata metadata;
    metadata.pluginRequirements = reader.requirements;
    metadata.pluginCompatibilityError = reader.compatibilityError;
    if (checked.version < 16 && !Plugins::content().requirements.empty()) {
        metadata.pluginCompatibilityError = "Legacy world has no content/behavior plugins";
        if (!inspection) throw std::runtime_error(metadata.pluginCompatibilityError);
    }
    metadata.displayName = reader.readString();
    metadata.seed = reader.read<uint64_t>();
    metadata.generationVersion = reader.read<uint32_t>();
    metadata.rulesetVersion = reader.read<uint32_t>();
    metadata.gameMode = static_cast<GameMode>(reader.read<uint8_t>());
    metadata.difficulty = static_cast<Difficulty>(reader.read<uint8_t>());
    if (checked.version >= 3) metadata.cheatsEnabled = reader.read<uint8_t>() != 0;
    metadata.worldTicks = reader.read<uint64_t>();
    if (checked.version >= 7) {
        metadata.weather.raining = reader.read<uint8_t>() != 0;
        metadata.weather.thundering = reader.read<uint8_t>() != 0;
        metadata.weather.rainTicks = reader.read<uint32_t>();
        metadata.weather.thunderTicks = reader.read<uint32_t>();
        metadata.weather.sequence = reader.read<uint64_t>();
    }
    metadata.playerPosition = checked.version >= 4
        ? reader.read<glm::dvec3>()
        : glm::dvec3(reader.read<glm::vec3>());
    metadata.worldSpawn = reader.read<glm::ivec3>();
    if (reader.read<uint8_t>() != 0) metadata.bedSpawn = reader.read<glm::ivec3>();
    metadata.health = reader.read<float>();
    metadata.hunger = reader.read<uint8_t>();
    metadata.saturation = reader.read<float>();
    metadata.exhaustion = reader.read<float>();
    if (static_cast<uint8_t>(metadata.gameMode) > static_cast<uint8_t>(GameMode::Spectator) ||
        static_cast<uint8_t>(metadata.difficulty) > static_cast<uint8_t>(Difficulty::Hard))
        throw std::runtime_error("Save contains invalid game rules");
    for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i)
        metadata.inventory.slot(i) = readStack(reader);
    for (auto& stack : metadata.inventory.armor()) stack = readStack(reader);
    metadata.inventory.offhand() = readStack(reader);
    const uint32_t entityCount = reader.read<uint32_t>();
    if (entityCount > 4096) throw std::runtime_error("Save contains too many entities");
    metadata.entities.reserve(entityCount);
    for (uint32_t i = 0; i < entityCount; ++i) {
        metadata.entities.push_back(readEntity(reader, checked.version));
    }
    if (checked.version >= 9) {
        // World type was appended in v9; older saves intentionally retain the
        // Normal default initialized in WorldMetadata.
        const uint8_t rawWorldType = reader.read<uint8_t>();
        if (rawWorldType > static_cast<uint8_t>(WorldType::Superflat))
            throw std::runtime_error("Save contains invalid world type");
        metadata.worldType = static_cast<WorldType>(rawWorldType);
    }
    if (checked.version >= 10) {
        const uint8_t rawDimension = reader.read<uint8_t>();
        if (rawDimension > static_cast<uint8_t>(DimensionId::Heaven))
            throw std::runtime_error("Save contains invalid dimension");
        metadata.activeDimension = static_cast<DimensionId>(rawDimension);
        metadata.overworldDayPhase = reader.read<float>();
        metadata.heaven.playerPosition = reader.read<glm::dvec3>();
        metadata.heaven.safePosition = reader.read<glm::ivec3>();
        metadata.heaven.hasSafePosition = reader.read<uint8_t>() != 0;
        metadata.heaven.worldTicks = reader.read<uint64_t>();
        metadata.heaven.dayPhase = reader.read<float>();
        if (!std::isfinite(metadata.overworldDayPhase) ||
            !std::isfinite(metadata.heaven.dayPhase) ||
            metadata.overworldDayPhase < 0.0f || metadata.overworldDayPhase >= 1.0f ||
            metadata.heaven.dayPhase < 0.0f || metadata.heaven.dayPhase >= 1.0f)
            throw std::runtime_error("Save contains invalid day phase");
    }
    if (checked.version >= 11)
        metadata.foodTickTimer = reader.read<uint32_t>();
    if (checked.version >= 14) {
        metadata.dayNightDurationSeconds = reader.read<uint32_t>();
        if (metadata.dayNightDurationSeconds == 0)
            throw std::runtime_error("Save contains invalid day/night duration");
    }
    if (checked.version >= 15) {
        const uint32_t count = reader.read<uint32_t>();
        if (count > 1024) throw std::runtime_error("Too many saved game rules");
        std::set<std::string> names;
        for (uint32_t i = 0; i < count; ++i) {
            const std::string name = reader.readString();
            const auto type = static_cast<GameRuleType>(reader.read<uint8_t>());
            const int32_t number = reader.read<int32_t>();
            if (name.empty() || name.size() > 256 || !names.insert(name).second ||
                (type != GameRuleType::Boolean && type != GameRuleType::Integer) ||
                (type == GameRuleType::Boolean && number != 0 && number != 1))
                throw std::runtime_error("Invalid saved game rule");
            const GameRuleValue value{type, number};
            const auto rule = findGameRule(name);
            if (rule) {
                if (name != gameRuleDefinition(rule->id).fullName ||
                    !metadata.gameRules.set(rule->id, value))
                    throw std::runtime_error("Invalid known game rule");
            } else metadata.gameRules.unknown.push_back({name, value});
        }
    }
    // Pre-v10 migration fixtures may carry fields appended by a newer writer
    // while retaining their legacy version marker.  Older fields are already
    // fully decoded above, so safely ignore that tail; v10 remains strict.
    if (checked.version >= 10 && !reader.finished())
        throw std::runtime_error("Unexpected trailing save data");
    return metadata;
}

std::filesystem::path SaveStore::chunkPath(int chunkX, int chunkZ) const {
    return m_worldDirectory / "chunks" /
        ("c." + std::to_string(chunkX) + "." + std::to_string(chunkZ) + ".bin");
}

std::filesystem::path SaveStore::generatedChunkPath(int chunkX, int chunkZ) const {
    return m_worldDirectory / "generated" /
        ("g." + std::to_string(chunkX) + "." + std::to_string(chunkZ) + ".bin");
}

std::filesystem::path SaveStore::blockEntityPath(int chunkX, int chunkZ) const {
    return m_worldDirectory / "block_entities" /
        ("b." + std::to_string(chunkX) + "." + std::to_string(chunkZ) + ".bin");
}

std::filesystem::path SaveStore::entityPath(int chunkX, int chunkZ) const {
    return m_worldDirectory / "entities" /
        ("e." + std::to_string(chunkX) + "." + std::to_string(chunkZ) + ".bin");
}

std::filesystem::path SaveStore::entityPopulationPath(
    int chunkX, int chunkZ) const {
    return m_worldDirectory / "entities" /
        ("p." + std::to_string(chunkX) + "." + std::to_string(chunkZ) + ".bin");
}

void SaveStore::saveChunkOverrides(
    int chunkX, int chunkZ, const std::vector<BlockOverride>& overrides) const {
    Bytes payload;
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, static_cast<uint32_t>(overrides.size()));
    for (const auto& entry : overrides) {
        if (entry.localIndex >= static_cast<uint32_t>(Config::CHUNK_VOLUME) ||
            !isValidBlockId(entry.block))
            throw std::runtime_error("Invalid block override");
        append(payload, entry.localIndex);
        append(payload, static_cast<uint16_t>(entry.block));
    }
    writeAtomic(chunkPath(chunkX, chunkZ), payload);
}

std::vector<BlockOverride> SaveStore::loadChunkOverrides(int chunkX, int chunkZ) const {
    const auto path = chunkPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return {};
    CheckedBytes checked = readChecked(path);
    Reader reader(std::move(checked.payload), checked.version);
    if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ)
        throw std::runtime_error("Chunk save coordinate mismatch");
    const uint32_t count = reader.read<uint32_t>();
    if (count > static_cast<uint32_t>(Config::CHUNK_VOLUME))
        throw std::runtime_error("Too many block overrides");
    std::vector<BlockOverride> overrides;
    overrides.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        BlockOverride entry;
        entry.localIndex = checked.version >= 6
            ? reader.read<uint32_t>() : reader.read<uint16_t>();
        entry.block = static_cast<BlockId>(reader.contentId(checked.version >= 13
            ? reader.read<uint16_t>() : reader.read<uint8_t>(), true));
        const uint32_t legacyLimit = 16u * 128u * 16u;
        const uint32_t limit = checked.version >= 6
            ? static_cast<uint32_t>(Config::CHUNK_VOLUME) : legacyLimit;
        if (entry.localIndex >= limit ||
            !isValidBlockId(entry.block))
            throw std::runtime_error("Invalid block override");
        overrides.push_back(entry);
    }
    if (!reader.finished()) throw std::runtime_error("Unexpected trailing chunk data");
    return overrides;
}

void SaveStore::saveGeneratedChunk(
    int chunkX, int chunkZ, const std::vector<uint16_t>& blocks,
    uint32_t generationVersion) const {
    if (blocks.size() != static_cast<size_t>(Config::CHUNK_VOLUME))
        throw std::runtime_error("Invalid generated chunk size");
    for (const uint16_t block : blocks)
        if (!isValidBlockId(static_cast<BlockId>(block)))
            throw std::runtime_error("Invalid generated block ID");
    Bytes payload;
    payload.reserve(sizeof(int32_t) * 2 + sizeof(uint32_t) * 2 + blocks.size() * sizeof(uint16_t));
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, generationVersion);
    append(payload, static_cast<uint32_t>(blocks.size()));
    const Bytes compressed = encodeChunkRle(blocks);
    if (compressed.size() + sizeof(uint8_t) + sizeof(uint32_t) < blocks.size() * sizeof(uint16_t)) {
        append(payload, GENERATED_CACHE_RLE);
        append(payload, static_cast<uint32_t>(compressed.size()));
        payload.insert(payload.end(), compressed.begin(), compressed.end());
    } else {
        // Retaining a raw fallback prevents incompressible chunks from
        // growing and keeps the on-disk format cheap for pathological data.
        append(payload, GENERATED_CACHE_RAW);
        append(payload, static_cast<uint32_t>(blocks.size() * sizeof(uint16_t)));
        for (const uint16_t block : blocks) append(payload, block);
    }
    writeAtomic(generatedChunkPath(chunkX, chunkZ), payload);
}

std::optional<std::vector<uint16_t>> SaveStore::loadGeneratedChunk(
    int chunkX, int chunkZ, uint32_t generationVersion) const {
    const auto path = generatedChunkPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return std::nullopt;
    try {
        CheckedBytes checked = readChecked(path);
        Reader reader(std::move(checked.payload), checked.version);
        if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ ||
            reader.read<uint32_t>() != generationVersion)
            return std::nullopt;
        const uint32_t size = reader.read<uint32_t>();
        if (size != static_cast<uint32_t>(Config::CHUNK_VOLUME)) return std::nullopt;
        std::vector<uint16_t> blocks;
        const bool wide = checked.version >= 13;
        const auto decodeRaw = [&](const Bytes& encoded) {
            const size_t stride = wide ? 2 : 1;
            if (encoded.size() != size * stride)
                throw std::runtime_error("Invalid generated raw chunk size");
            blocks.reserve(size);
            for (size_t i = 0; i < encoded.size(); i += stride)
                blocks.push_back(static_cast<uint16_t>(encoded[i]) |
                    (wide ? static_cast<uint16_t>(encoded[i + 1]) << 8 : 0));
        };
        if (!wide && reader.remaining() == size) {
            decodeRaw(reader.readBytes(size));
        } else {
            const uint8_t codec = reader.read<uint8_t>();
            const uint32_t encodedSize = reader.read<uint32_t>();
            if (encodedSize > reader.remaining()) return std::nullopt;
            const Bytes encoded = reader.readBytes(encodedSize);
            if (codec == GENERATED_CACHE_RAW) decodeRaw(encoded);
            else if (codec == GENERATED_CACHE_RLE)
                blocks = decodeChunkRle(encoded, size, wide);
            else return std::nullopt;
        }
        for (uint16_t& block : blocks) {
            block = reader.contentId(block, true);
            if (!isValidBlockId(static_cast<BlockId>(block))) return std::nullopt;
        }
        if (!reader.finished()) return std::nullopt;
        return blocks;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

ChunkLoadBundle SaveStore::loadChunkLoadBundle(
    int chunkX, int chunkZ, uint32_t generationVersion) const {
    ChunkLoadBundle bundle;
    bundle.generated = loadGeneratedChunk(chunkX, chunkZ, generationVersion);
    try { bundle.overrides = loadChunkOverrides(chunkX, chunkZ); }
    catch (...) { bundle.overrides.clear(); }
    try { bundle.blockEntities = loadBlockEntities(chunkX, chunkZ); }
    catch (...) { bundle.blockEntities.clear(); }
    try { bundle.entities = loadChunkEntities(chunkX, chunkZ); }
    catch (...) { bundle.entities.clear(); }
    try {
        bundle.entityPopulationVersion =
            loadChunkEntityPopulationVersion(chunkX, chunkZ);
    } catch (...) {
        bundle.entityPopulationVersion = 0;
    }
    return bundle;
}

void SaveStore::saveBlockEntities(
    int chunkX, int chunkZ, const std::vector<PersistedBlockEntity>& entities) const {
    Bytes payload;
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, static_cast<uint32_t>(entities.size()));
    for (const auto& entity : entities) appendBlockEntity(payload, entity);
    writeAtomic(blockEntityPath(chunkX, chunkZ), payload);
}

std::vector<PersistedBlockEntity> SaveStore::loadBlockEntities(
    int chunkX, int chunkZ) const {
    const auto path = blockEntityPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return {};
    CheckedBytes checked = readChecked(path);
    Reader reader(std::move(checked.payload), checked.version);
    if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ)
        throw std::runtime_error("Block entity coordinate mismatch");
    const uint32_t count = reader.read<uint32_t>();
    if (count > 4096) throw std::runtime_error("Too many block entities");
    std::vector<PersistedBlockEntity> result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        result.push_back(readBlockEntity(reader, checked.version));
    if (!reader.finished()) throw std::runtime_error("Unexpected block entity data");
    return result;
}

void SaveStore::saveChunkEntities(
    int chunkX, int chunkZ,
    const std::vector<WorldMetadata::PersistedEntity>& entities) const {
    Bytes payload;
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, static_cast<uint32_t>(entities.size()));
    for (const auto& entity : entities) appendEntity(payload, entity);
    writeAtomic(entityPath(chunkX, chunkZ), payload);
}

std::vector<WorldMetadata::PersistedEntity> SaveStore::loadChunkEntities(
    int chunkX, int chunkZ) const {
    const auto path = entityPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return {};
    CheckedBytes checked = readChecked(path);
    Reader reader(std::move(checked.payload), checked.version);
    if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ)
        throw std::runtime_error("Entity chunk coordinate mismatch");
    const uint32_t count = reader.read<uint32_t>();
    if (count > 4096) throw std::runtime_error("Too many chunk entities");
    std::vector<WorldMetadata::PersistedEntity> result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) result.push_back(readEntity(reader, checked.version));
    if (!reader.finished()) throw std::runtime_error("Unexpected chunk entity data");
    return result;
}

void SaveStore::saveChunkEntityPopulationVersion(
    int chunkX, int chunkZ, uint32_t version) const {
    Bytes payload;
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, version);
    writeAtomic(entityPopulationPath(chunkX, chunkZ), payload);
}

uint32_t SaveStore::loadChunkEntityPopulationVersion(
    int chunkX, int chunkZ) const {
    const auto path = entityPopulationPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return 0;
    CheckedBytes checked = readChecked(path);
    Reader reader(std::move(checked.payload), checked.version);
    if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ)
        throw std::runtime_error("Entity population coordinate mismatch");
    const uint32_t version = reader.read<uint32_t>();
    if (!reader.finished())
        throw std::runtime_error("Unexpected entity population data");
    return version;
}

void SaveStore::validatePluginFiles() const {
    for (const auto& file : std::filesystem::recursive_directory_iterator(m_worldDirectory)) {
        if (!file.is_regular_file() || file.path().extension() != ".bin") continue;
        // Generated base caches are disposable; durable partitions must retain
        // resolvable names before a session can attach a writable store.
        if (file.path().parent_path().filename() == "generated") continue;
        auto bytes=readChecked(file.path());
        Reader reader(std::move(bytes.payload),bytes.version);
        (void)reader;
    }
}
