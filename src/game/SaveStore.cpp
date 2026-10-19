#include "plugins/ContentRegistry.h"
#include "plugins/Json.h"
#include "game/SaveStore.h"
#include "Config.h"
#include "core/Platform.h"
#include "entity/EntityLogic.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>
#include <set>
#include <chrono>
#include <sstream>
#include <regex>
#include "world/WorldGenContext.h"

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
    explicit Reader(Bytes bytes, uint32_t version=0, bool inspection=false,
                    bool recovery=false, std::vector<PluginCompatibilityIssue>* issues=nullptr)
        : inspectOnly(inspection), recoverPlugins(recovery), report(issues), m_bytes(std::move(bytes)) {
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
        for (const auto& plugin:saved) {
            const auto it=std::find_if(expected.begin(),expected.end(),[&](const auto& p){return p.first==plugin.first;});
            if(it==expected.end())issue("missing_plugin",plugin.first,plugin.second,"");
            else if(it->second!=plugin.second)issue("changed_plugin",plugin.first,plugin.second,it->second);
        }
        for (const auto& plugin:expected)
            if(std::none_of(saved.begin(),saved.end(),[&](const auto& p){return p.first==plugin.first;}))
                issue("extra_plugin",plugin.first,"",plugin.second);
        if (expected!=saved && !inspection && !recovery) throw std::runtime_error(compatibilityError);
        auto palette=[&](bool blocks) {
            const uint32_t count=read<uint32_t>();
            if(count>65535) throw std::runtime_error("Plugin palette is too large");
            auto& map=blocks?blockMap:itemMap;std::set<std::string> keys;
            for(uint32_t i=0;i<count;++i) {
                const uint16_t raw=read<uint16_t>();const std::string key=readString();
                if(raw<Plugins::FIRST_CONTENT_ID||raw==Plugins::INVALID_CONTENT_ID||!Plugins::validKey(key)||map.count(raw)||!keys.insert(key).second) throw std::runtime_error("Invalid plugin palette");
                uint16_t resolved=Plugins::INVALID_CONTENT_ID;
                try {resolved=blocks?static_cast<uint16_t>(Plugins::resolveBlock(key)):static_cast<uint16_t>(Plugins::resolveItem(key));}
                catch(const std::exception&) {if(!inspection&&!recovery)throw;issue("missing_content",key,key,"");}
                map.emplace(raw,resolved);
            }
        };
        palette(true);palette(false);
    }
    std::set<PluginCompatibilityIssue> seenIssues;
    bool inspectOnly=false;
    bool recoverPlugins=false;
    std::vector<PluginCompatibilityIssue>* report=nullptr;
    void issue(const std::string& kind,const std::string& key,const std::string& saved,const std::string& current) {
        const PluginCompatibilityIssue value{kind,key,saved,current};
        if(!seenIssues.insert(value).second)return;
        if(!compatibilityError.empty())compatibilityError+="; ";
        compatibilityError+=kind+": "+key+" ["+saved+" -> "+current+"]";
        if(report && std::find(report->begin(),report->end(),value)==report->end())report->push_back(value);
    }
    std::vector<std::pair<std::string,std::string>> requirements;
    std::string compatibilityError;
    std::map<uint16_t,uint16_t> blockMap,itemMap;
    uint16_t contentId(uint16_t raw, bool block) {
        const uint16_t count=block?static_cast<uint16_t>(BlockId::COUNT):static_cast<uint16_t>(ItemId::COUNT);
        uint16_t resolved=raw;
        if(raw>=count) {
            const auto& map=block?blockMap:itemMap;const auto it=map.find(raw);
            if(it==map.end())throw std::runtime_error("Unmapped serialized content ID");
            resolved=it->second;
        }
        const bool available=resolved!=Plugins::INVALID_CONTENT_ID &&
            (block?Plugins::availableBlock(static_cast<BlockId>(resolved)):Plugins::availableItem(static_cast<ItemId>(resolved)));
        if(recoverPlugins && !available) {
            // Includes fixed reserved slots whose optional provider is absent.
            if(resolved!=Plugins::INVALID_CONTENT_ID)issue("missing_content",std::string(block?"minecraftc:block_":"minecraftc:item_")+std::to_string(raw),std::to_string(raw),"");
            return 0;
        }
        return resolved;
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
    // Reserved serialized slots retain their historical implicit identity.
    // Only launch-local allocations belong in the v21 remapping palettes.
    const auto palette=[&](const auto& entries) {
        const auto first=entries.lower_bound(Plugins::FIRST_CONTENT_ID);
        append(payload,static_cast<uint32_t>(std::distance(first,entries.end())));
        for(auto entry=first;entry!=entries.end();++entry){append(payload,entry->first);appendString(payload,entry->second.key);}
    };
    palette(Plugins::content().blocks);palette(Plugins::content().items);
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
    if(!Plugins::availableItem(stack.id))throw std::runtime_error("Cannot save unavailable item");
    append(payload, static_cast<uint16_t>(stack.id));
    append(payload, stack.count);
    append(payload, stack.damage);
}

ItemStack readStack(Reader& reader) {
    ItemStack stack;
    stack.id = static_cast<ItemId>(reader.contentId(reader.read<uint16_t>(), false));
    stack.count = reader.read<uint8_t>();
    stack.damage = reader.read<uint16_t>();
    if(stack.count>64)throw std::runtime_error("Save contains invalid item count");
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
    append(payload,static_cast<uint8_t>(entity.type<MC_BUILTIN_ENTITY_COUNT?entity.type:255));
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
    appendString(payload,Plugins::entityKey(entity.type));
    if(entity.pluginState.size()>MC_ENTITY_STATE_LIMIT)throw std::runtime_error("Entity state too large");
    Plugins::Json::parse(entity.pluginState);
    appendString(payload,entity.pluginState);append(payload,entity.pluginStateVersion);
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
    if(version>=19) {
        const auto key=reader.readString();if(!Plugins::validKey(key))throw std::runtime_error("Invalid entity key");entity.pluginState=reader.readString();entity.pluginStateVersion=reader.read<uint32_t>();
        if(entity.pluginState.size()>MC_ENTITY_STATE_LIMIT)throw std::runtime_error("Entity state too large");
        Plugins::Json::parse(entity.pluginState);
        const auto old=entity.type;
        if(!reader.inspectOnly) {
            try {entity.type=Plugins::resolveEntity(key);}
            catch(const std::exception&) {if(!reader.recoverPlugins)throw;entity.type=Plugins::INVALID_CONTENT_ID;reader.issue("missing_entity",key,key,"");}
        }
        if(old!=255 && key!="minecraftc:entity_"+std::to_string(old))throw std::runtime_error("Entity key mismatch");
        if(!reader.inspectOnly) {
            const auto* e=Plugins::entityContent(entity.type);
            if(e && entity.pluginStateVersion!=e->stateVersion) {
                if(!reader.recoverPlugins)throw std::runtime_error("Entity state version mismatch");
                reader.issue("entity_revision",key,std::to_string(entity.pluginStateVersion),std::to_string(e->stateVersion));
                entity.type=Plugins::INVALID_CONTENT_ID;
            }
        }
    }
    // Appended entity types are legal only in the versions that introduced them.
    if ((version<19 && entity.type > static_cast<uint8_t>(version>=17 ? EntityType::IronGolem : EntityType::ZombieVillager)) ||
        (version>=19&&!reader.inspectOnly&&!reader.recoverPlugins&&!Plugins::validEntity(entity.type)))
        throw std::runtime_error("Save contains invalid entity type");
    return entity;
}

void appendBlockEntity(Bytes& payload, const PersistedBlockEntity& entity) {
    append(payload, entity.localIndex);
    append(payload, static_cast<uint8_t>(entity.value.type));
    if (entity.value.type == BlockEntityType::Chest) {
        for (const auto& stack : entity.value.chest) appendStack(payload, stack);
        appendString(payload,entity.value.lootKey);appendString(payload,entity.value.lootRevision);
        appendString(payload,entity.value.structureKey);appendString(payload,entity.value.structureRevision);
        append(payload,entity.value.lootSeed);append(payload,static_cast<uint8_t>(entity.value.lootInitialized));
    } else if (entity.value.type == BlockEntityType::Resource) {
        append(payload,entity.value.resourceRemaining);appendString(payload,Plugins::blockKey(static_cast<BlockId>(entity.value.resourceBlock)));
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
    if (type > static_cast<uint8_t>(version >= 21 ? BlockEntityType::Resource : version >= 18 ? BlockEntityType::Button : BlockEntityType::Furnace))
        throw std::runtime_error("Invalid block entity type");
    entity.value.type = static_cast<BlockEntityType>(type);
    if (entity.value.type == BlockEntityType::Chest) {
        for (auto& stack : entity.value.chest) stack = readStack(reader);
        if(version>=21){auto& e=entity.value;e.lootKey=reader.readString();e.lootRevision=reader.readString();e.structureKey=reader.readString();e.structureRevision=reader.readString();e.lootSeed=reader.read<uint64_t>();const auto flag=reader.read<uint8_t>();if(flag>1)throw std::runtime_error("Invalid loot flag");e.lootInitialized=flag;
            if((!e.lootKey.empty()&&!Plugins::validKey(e.lootKey))||(!e.structureKey.empty()&&!Plugins::validKey(e.structureKey)))throw std::runtime_error("Invalid loot/structure key");
            if(!e.lootKey.empty()){const auto& tables=Plugins::content().extensions.loot;const auto found=tables.find(e.lootKey);if(!e.lootInitialized)throw std::runtime_error("Incompatible chest loot metadata");
                if(found==tables.end()||found->second.revision!=e.lootRevision){if(!reader.recoverPlugins)throw std::runtime_error("Incompatible chest loot metadata");reader.issue("loot_revision",e.lootKey,e.lootRevision,found==tables.end()?"":found->second.revision);e.lootKey.clear();e.lootRevision.clear();}}
            if(!e.structureKey.empty()){const auto& structures=Plugins::content().extensions.structures;const auto found=structures.find(e.structureKey);if(found==structures.end()||found->second.revision!=e.structureRevision){if(!reader.recoverPlugins)throw std::runtime_error("Incompatible chest structure metadata");reader.issue("structure_revision",e.structureKey,e.structureRevision,found==structures.end()?"":found->second.revision);e.structureKey.clear();e.structureRevision.clear();}}}
    } else if(entity.value.type==BlockEntityType::Resource){entity.value.resourceRemaining=reader.read<uint32_t>();const auto resourceKey=reader.readString();if(!Plugins::validKey(resourceKey))throw std::runtime_error("Invalid resource block key");try{entity.value.resourceBlock=static_cast<uint16_t>(Plugins::resolveBlock(resourceKey));}catch(const std::exception&){if(!reader.recoverPlugins)throw;entity.value.resourceBlock=0;reader.issue("missing_content",resourceKey,resourceKey,"");}if(entity.value.resourceRemaining>72000)throw std::runtime_error("Invalid resource timer");
    } else if (entity.value.type == BlockEntityType::Button) {
        entity.value.buttonRemaining = reader.read<uint16_t>();
        if (version < 20 && entity.value.buttonRemaining > 30) throw std::runtime_error("Invalid button timer");
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

SaveStore::SaveStore(std::filesystem::path worldDirectory,SaveReadPolicy policy,
                     std::vector<PluginCompatibilityIssue>* issues)
    : m_worldDirectory(std::move(worldDirectory)),m_policy(policy),m_issues(issues) {}

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
    append(payload, static_cast<uint8_t>(metadata.activeDimension==DimensionId::Heaven?1:0));
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
    // Historical inspection slots may remain implicit; keyed records below
    // are authoritative and always validate registered generator selections.
    for(uint32_t dimension=0;dimension<2;++dimension) {
        const auto found=metadata.generation.find(dimension);const auto g=found==metadata.generation.end()?Plugins::GenerationConfig{}:found->second;
        if(Plugins::validDimension(dimension)){if(!(g==Plugins::GenerationConfig{}))Plugins::validateGeneration(g,dimension);}
        else if(!(g==Plugins::GenerationConfig{}))throw std::runtime_error("Generation for disabled dimension");
        if(g.parameters.size()>4096||g.modifiers.size()>64)throw std::runtime_error("Generation configuration too large");
        Plugins::Json::parse(g.parameters);
        appendString(payload,g.generator);appendString(payload,g.revision);appendString(payload,g.parameters);
        append(payload,static_cast<uint16_t>(g.modifiers.size()));
        for(const auto& m:g.modifiers){appendString(payload,m.first);appendString(payload,m.second);}
    }
    // v20 keeps the legacy inspection prefix, then stores authoritative keyed states.
    appendString(payload,Plugins::dimensionKey(metadata.activeDimension));
    std::map<uint32_t,Plugins::GenerationConfig> generations;
    for(const auto& entry:metadata.generation)if(Plugins::validDimension(entry.first))generations.emplace(entry);else if(!(entry.second==Plugins::GenerationConfig{}))throw std::runtime_error("Unknown saved dimension");
    for(const auto& entry:Plugins::content().extensions.dimensions)if(!generations.count(entry.first))generations[entry.first]=entry.second.generation;
    append(payload,static_cast<uint32_t>(generations.size()));
    for(const auto& entry:generations) {
        const auto id=entry.first;const auto& definition=Plugins::dimensionContent(id);const auto& g=entry.second;
        Plugins::validateGeneration(g,id);appendString(payload,definition.key);appendString(payload,definition.revision);
        appendString(payload,g.generator);appendString(payload,g.revision);appendString(payload,g.parameters);
        append(payload,static_cast<uint16_t>(g.modifiers.size()));for(const auto& modifier:g.modifiers){appendString(payload,modifier.first);appendString(payload,modifier.second);}
        WorldMetadata::DimensionState state;
        auto found=metadata.dimensions.find(id);if(found!=metadata.dimensions.end())state=found->second;
        if(id==0){state.playerPosition=metadata.playerPosition;state.worldTicks=metadata.worldTicks;state.dayPhase=metadata.overworldDayPhase;state.weather=metadata.weather;state.bedSpawn=metadata.bedSpawn;}
        if(id==1){auto bed=state.bedSpawn;auto weather=state.weather;auto positioned=state.positioned;auto safe=state.safePlayerPosition;state=metadata.heaven;state.bedSpawn=bed;state.weather=weather;state.positioned=positioned;state.safePlayerPosition=found==metadata.dimensions.end()?glm::dvec3(state.safePosition)+glm::dvec3(.5,.01,.5):safe;}
        append(payload,state.playerPosition);append(payload,state.safePlayerPosition);append(payload,static_cast<uint8_t>(state.hasSafePosition));append(payload,static_cast<uint8_t>(state.positioned));
        append(payload,state.worldTicks);append(payload,state.dayPhase);
        append(payload,static_cast<uint8_t>(state.weather.raining));append(payload,static_cast<uint8_t>(state.weather.thundering));append(payload,state.weather.rainTicks);append(payload,state.weather.thunderTicks);append(payload,state.weather.sequence);
        append(payload,static_cast<uint8_t>(state.bedSpawn.has_value()));if(state.bedSpawn)append(payload,*state.bedSpawn);
    }
    append(payload,static_cast<uint32_t>(metadata.timePolicies.size()));
    for(const auto& entry:metadata.timePolicies){appendString(payload,Plugins::dimensionContent(entry.first).key);append(payload,entry.second.first);append(payload,entry.second.second);}
    writeAtomic(m_worldDirectory / "level.bin", payload);
}

WorldMetadata SaveStore::loadMetadata(bool inspection) const {
    CheckedBytes checked = readChecked(m_worldDirectory / "level.bin");
    if(!inspection&&checked.version!=SAVE_FORMAT_VERSION)throw std::runtime_error("World requires save format v21; legacy worlds are incompatible");
    const bool recovery=m_policy==SaveReadPolicy::RecoverPlugins;
    Reader reader(std::move(checked.payload), checked.version, inspection, recovery, m_issues);
    WorldMetadata metadata;
    metadata.pluginRequirements = reader.requirements;
    metadata.pluginCompatibilityError = reader.compatibilityError;
    if(checked.version!=SAVE_FORMAT_VERSION)metadata.pluginCompatibilityError="World requires save format v21; legacy worlds are incompatible";
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
        auto entity=readEntity(reader, checked.version);
        if(!recovery || (Plugins::validEntity(entity.type) && !(entity.type==static_cast<uint16_t>(EntityType::Item) && entity.item.empty())))metadata.entities.push_back(std::move(entity));
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
    auto validateGenerationShape=[](const Plugins::GenerationConfig& g) {
        if(g.generator.empty()&&!g.revision.empty())throw std::runtime_error("Builtin generator has no plugin revision");
        if(!g.generator.empty()&&!Plugins::validKey(g.generator))throw std::runtime_error("Invalid generator key");
        if(Plugins::Json::parse(g.parameters).type!=Plugins::Json::Type::Object)throw std::runtime_error("Generator parameters must be a JSON object");
        std::set<std::string> selected;
        for(const auto& modifier:g.modifiers)if(!Plugins::validKey(modifier.first)||!selected.insert(modifier.first).second)throw std::runtime_error("Invalid saved generator modifier");
    };
    auto repairGeneration=[&](Plugins::GenerationConfig& g,uint32_t dimension) {
        if(!recovery)return;
        validateGenerationShape(g);
        if(!g.generator.empty()) {
            const auto it=Plugins::content().extensions.generators.find(g.generator);
            if(it==Plugins::content().extensions.generators.end()||it->second.dimension!=dimension) {
                reader.issue("missing_generator",g.generator,g.revision,"");
                g=Plugins::dimensionContent(dimension).generation;
            } else if(g.revision!=it->second.revision) {
                reader.issue("generator_revision",g.generator,g.revision,it->second.revision);g.revision=it->second.revision;
            }
        }
        for(auto it=g.modifiers.begin();it!=g.modifiers.end();) {
            const auto found=Plugins::content().extensions.modifiers.find(it->first);
            if(found==Plugins::content().extensions.modifiers.end()||found->second.dimension!=dimension) {
                reader.issue("missing_modifier",it->first,it->second,"");it=g.modifiers.erase(it);
            } else {if(it->second!=found->second.revision){reader.issue("modifier_revision",it->first,it->second,found->second.revision);it->second=found->second.revision;}++it;}
        }
        Plugins::validateGeneration(g,dimension);
    };
    if(checked.version>=19)for(uint32_t dimension=0;dimension<2;++dimension) {
        auto& g=metadata.generation[dimension];g.generator=reader.readString();g.revision=reader.readString();g.parameters=reader.readString();
        if(g.parameters.size()>4096)throw std::runtime_error("Generation parameters too large");
        Plugins::Json::parse(g.parameters);
        const auto count=reader.read<uint16_t>();if(count>64)throw std::runtime_error("Too many generation modifiers");
        for(uint16_t i=0;i<count;++i){auto key=reader.readString();auto revision=reader.readString();g.modifiers.emplace_back(key,revision);}
        validateGenerationShape(g);
        if(!inspection&&!recovery&&Plugins::validDimension(dimension)&&
           (checked.version<20||!(g==Plugins::GenerationConfig{})))Plugins::validateGeneration(g,dimension);
    }
    if(checked.version>=20) {
        auto resolve=[&](const std::string& key,uint32_t fallback) {
            if(!Plugins::validKey(key))throw std::runtime_error("Invalid dimension key");
            try{return Plugins::resolveDimension(key);}catch(const std::exception&){if(!inspection&&!recovery)throw;reader.issue("missing_dimension",key,key,"");return fallback;}
        };
        const auto activeKey=reader.readString();
        const auto count=reader.read<uint32_t>();if(count<1||count>1024)throw std::runtime_error("Invalid dimension count");
        metadata.generation.clear();std::set<std::string> keys;
        for(uint32_t index=0;index<count;++index) {
            const auto key=reader.readString(),revision=reader.readString();if(!keys.insert(key).second)throw std::runtime_error("Duplicate saved dimension");
            const auto id=resolve(key,1024+index);
            metadata.savedDimensionKeys[id]=key;
            if(Plugins::validDimension(id)&&revision!=Plugins::dimensionContent(id).revision){reader.issue("dimension_revision",key,revision,Plugins::dimensionContent(id).revision);if(!inspection&&!recovery)throw std::runtime_error(reader.compatibilityError);}
            auto& g=metadata.generation[id];g.generator=reader.readString();g.revision=reader.readString();g.parameters=reader.readString();
            const auto modifiers=reader.read<uint16_t>();if(modifiers>64||g.parameters.size()>4096)throw std::runtime_error("Invalid saved generation configuration");
            for(uint16_t i=0;i<modifiers;++i){auto k=reader.readString(),r=reader.readString();g.modifiers.emplace_back(k,r);}
            validateGenerationShape(g);
            if(!Plugins::validDimension(id)) {
                if(!g.generator.empty()&&!Plugins::content().extensions.generators.count(g.generator))reader.issue("missing_generator",g.generator,g.revision,"");
                for(const auto& modifier:g.modifiers)if(!Plugins::content().extensions.modifiers.count(modifier.first))reader.issue("missing_modifier",modifier.first,modifier.second,"");
            }
            if(Plugins::validDimension(id)){repairGeneration(g,id);if(!inspection&&!recovery)Plugins::validateGeneration(g,id);}
            auto& state=metadata.dimensions[id];state.playerPosition=reader.read<glm::dvec3>();state.safePlayerPosition=reader.read<glm::dvec3>();
            auto flag=[&]{const auto v=reader.read<uint8_t>();if(v>1)throw std::runtime_error("Invalid dimension flag");return v!=0;};
            state.hasSafePosition=flag();state.positioned=flag();state.worldTicks=reader.read<uint64_t>();state.dayPhase=reader.read<float>();
            if(!std::isfinite(state.dayPhase)||state.dayPhase<0||state.dayPhase>=1)throw std::runtime_error("Invalid dimension day phase");
            for(int axis=0;axis<3;++axis)if(!std::isfinite(state.playerPosition[axis])||std::abs(state.playerPosition[axis])>30000000||!std::isfinite(state.safePlayerPosition[axis])||std::abs(state.safePlayerPosition[axis])>30000000)throw std::runtime_error("Invalid dimension position");
            state.safePosition=glm::ivec3(glm::floor(state.safePlayerPosition));
            state.weather.raining=flag();state.weather.thundering=flag();state.weather.rainTicks=reader.read<uint32_t>();state.weather.thunderTicks=reader.read<uint32_t>();state.weather.sequence=reader.read<uint64_t>();
            if(flag())state.bedSpawn=reader.read<glm::ivec3>();
        }
        if(!keys.count(activeKey)||!keys.count("minecraftc:overworld"))throw std::runtime_error("Missing saved dimension state");
        metadata.activeDimension=static_cast<DimensionId>(resolve(activeKey,2048));
        if(count!=Plugins::content().extensions.dimensions.size()){reader.issue("dimension_registry","dimensions",std::to_string(count),std::to_string(Plugins::content().extensions.dimensions.size()));if(!inspection&&!recovery)throw std::runtime_error(reader.compatibilityError);}
    }
    if(checked.version>=21){const auto count=reader.read<uint32_t>();if(count>1024)throw std::runtime_error("Too many time policies");for(uint32_t i=0;i<count;++i){const auto key=reader.readString();uint32_t id=1024+i;try{id=Plugins::resolveDimension(key);}catch(...){if(!inspection&&!recovery)throw;}const auto cycle=reader.read<uint32_t>(),advancing=reader.read<uint32_t>();if(cycle>86400000||advancing>2||!metadata.timePolicies.emplace(id,std::make_pair(cycle,advancing)).second)throw std::runtime_error("Invalid time policy");}}
    if(recovery) {
        for(auto& entry:metadata.generation)if(Plugins::validDimension(entry.first))repairGeneration(entry.second,entry.first);
        for(auto it=metadata.generation.begin();it!=metadata.generation.end();) {
            if(!Plugins::validDimension(it->first)){metadata.dimensions.erase(it->first);metadata.timePolicies.erase(it->first);it=metadata.generation.erase(it);}else ++it;
        }
        for(const auto& entry:Plugins::content().extensions.dimensions) {
            if(!metadata.generation.count(entry.first)) {reader.issue("extra_dimension",entry.second.key,"",entry.second.revision);metadata.generation[entry.first]=entry.second.generation;}
        }
        for(auto it=metadata.timePolicies.begin();it!=metadata.timePolicies.end();)if(!Plugins::validDimension(it->first))it=metadata.timePolicies.erase(it);else ++it;
        if(!Plugins::validDimension(metadata.activeDimension)) {
            metadata.needsRecoverySpawn=true;
            metadata.activeDimension=DimensionId::Overworld;
            const auto found=metadata.dimensions.find(0);
            if(found!=metadata.dimensions.end()&&found->second.hasSafePosition)metadata.playerPosition=found->second.safePlayerPosition;
            else metadata.playerPosition=glm::dvec3(metadata.worldSpawn)+glm::dvec3(.5,.01,.5);
        }
        if(!Plugins::validDimension(1))metadata.heaven={};
    }
    if(!reader.compatibilityError.empty())metadata.pluginCompatibilityError=reader.compatibilityError;
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
    Reader reader(std::move(checked.payload), checked.version, false, m_policy==SaveReadPolicy::RecoverPlugins, m_issues);
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
    uint32_t generationVersion,uint64_t generationFingerprint) const {
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
    append(payload,generationFingerprint);
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
    int chunkX, int chunkZ, uint32_t generationVersion,uint64_t generationFingerprint) const {
    const auto path = generatedChunkPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return std::nullopt;
    try {
        CheckedBytes checked = readChecked(path);
        Reader reader(std::move(checked.payload), checked.version, false, m_policy==SaveReadPolicy::RecoverPlugins, m_issues);
        if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ ||
            reader.read<uint32_t>() != generationVersion)
            return std::nullopt;
        if(checked.version>=19&&reader.read<uint64_t>()!=generationFingerprint)return std::nullopt;
        if(checked.version<19&&generationFingerprint)return std::nullopt;
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
    int chunkX, int chunkZ, uint32_t generationVersion,uint64_t generationFingerprint) const {
    ChunkLoadBundle bundle;
    bundle.generated = loadGeneratedChunk(chunkX, chunkZ, generationVersion,generationFingerprint);
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
    Reader reader(std::move(checked.payload), checked.version, false, m_policy==SaveReadPolicy::RecoverPlugins, m_issues);
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
    Reader reader(std::move(checked.payload), checked.version, false, m_policy==SaveReadPolicy::RecoverPlugins, m_issues);
    if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ)
        throw std::runtime_error("Entity chunk coordinate mismatch");
    const uint32_t count = reader.read<uint32_t>();
    if (count > 4096) throw std::runtime_error("Too many chunk entities");
    std::vector<WorldMetadata::PersistedEntity> result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto entity=readEntity(reader, checked.version);
        if(m_policy!=SaveReadPolicy::RecoverPlugins || (Plugins::validEntity(entity.type) && !(entity.type==static_cast<uint16_t>(EntityType::Item) && entity.item.empty())))result.push_back(std::move(entity));
    }
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
    Reader reader(std::move(checked.payload), checked.version, false, m_policy==SaveReadPolicy::RecoverPlugins, m_issues);
    if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ)
        throw std::runtime_error("Entity population coordinate mismatch");
    const uint32_t version = reader.read<uint32_t>();
    if (!reader.finished())
        throw std::runtime_error("Unexpected entity population data");
    return version;
}

namespace {
std::string directoryForKey(const std::string& key);
struct DurableFile {
    std::filesystem::path root;
    char kind=0;
    int x=0,z=0;
};
DurableFile durableFile(const std::filesystem::path& relative) {
    const auto folder=relative.parent_path().filename().string();
    if(relative=="level.bin")return {{},'l',0,0};
    if(folder!="chunks" && folder!="block_entities" && folder!="entities")return {};
    // Restrict conversion to world partitions, excluding arbitrary plugin/private trees.
    const auto parent=relative.parent_path().parent_path();
    if(!parent.empty()) {
        if(*parent.begin()!="dimensions")return {};
        const auto directory=parent.lexically_relative("dimensions");
        if(directory!="heaven") {
            std::string encoded;
            for(const auto& segment:directory)encoded+=segment.string();
            if(encoded.rfind("d_",0)!=0)return {};
            encoded.erase(0,2);
            if(encoded.empty()||encoded.size()%2||encoded.size()>512)return {};
            std::string key;const std::string hex="0123456789abcdef";
            for(size_t i=0;i<encoded.size();i+=2) {
                const auto hi=hex.find(encoded[i]),lo=hex.find(encoded[i+1]);
                if(hi==std::string::npos||lo==std::string::npos)return {};
                key+=static_cast<char>((hi<<4)|lo);
            }
            if(!Plugins::validKey(key)||directory!=std::filesystem::path(directoryForKey(key)))return {};
        }
    }
    if(relative.extension()!=".bin")return {};
    std::smatch match;const auto filename=relative.filename().string();
    static const std::regex pattern(R"(^([cbep])\.(-?[0-9]+)\.(-?[0-9]+)\.bin$)");
    if(!std::regex_match(filename,match,pattern))throw std::runtime_error("Invalid durable partition filename: "+relative.generic_string());
    const char kind=match[1].str()[0];
    if((folder=="chunks" && kind!='c') || (folder=="block_entities" && kind!='b') || (folder=="entities" && kind!='e' && kind!='p'))throw std::runtime_error("Invalid durable partition kind");
    return {parent,kind,std::stoi(match[2]),std::stoi(match[3])};
}
std::string directoryForKey(const std::string& key) {
    if(key=="minecraftc:overworld")return "";
    if(key=="minecraftc:heaven")return "heaven";
    std::string result="d_";const char* hex="0123456789abcdef";
    for(unsigned char c:key){result+=hex[c>>4];result+=hex[c&15];}
    for(size_t i=122;i<result.size();i+=123)result.insert(i,"/");
    return result;
}
bool under(const std::filesystem::path& path,const std::filesystem::path& prefix) {
    auto a=path.begin(),b=prefix.begin();for(;b!=prefix.end();++a,++b)if(a==path.end()||*a!=*b)return false;return true;
}
std::vector<std::filesystem::path> worldFiles(const std::filesystem::path& root) {
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(root)))throw std::runtime_error("World recovery rejects symbolic links");
    std::vector<std::filesystem::path> files;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
        if(entry.is_symlink())throw std::runtime_error("World recovery rejects symbolic links: "+entry.path().string());
        if(entry.is_regular_file())files.push_back(entry.path().lexically_relative(root));
        else if(!entry.is_directory())throw std::runtime_error("Unsupported world file: "+entry.path().string());
    }
    std::sort(files.begin(),files.end());return files;
}
uint64_t hashText(const std::string& text,uint64_t hash) {
    for(unsigned char c:text){hash^=c;hash*=1099511628211ULL;}
    hash^=0;hash*=1099511628211ULL;return hash;
}
uint64_t compatibilityHashSeed() {
    uint64_t hash=1469598103934665603ULL;
    auto requirements=Plugins::content().requirements;std::sort(requirements.begin(),requirements.end());
    for(const auto& requirement:requirements){hash=hashText(requirement.first,hash);hash=hashText(requirement.second,hash);}
    return hash;
}
uint64_t hashFile(const std::filesystem::path& path,uint64_t hash) {
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot read world file: "+path.string());
    std::array<char,65536> buffer{};
    while(input){input.read(buffer.data(),buffer.size());for(std::streamsize i=0;i<input.gcount();++i){hash^=static_cast<uint8_t>(buffer[static_cast<size_t>(i)]);hash*=1099511628211ULL;}}
    if(!input.eof())throw std::runtime_error("Cannot read complete world file");return hash;
}
bool disposable(const std::filesystem::path& relative,const std::vector<std::filesystem::path>& dimensions) {
    auto cache=[](const std::filesystem::path& path) {
        if(path.empty())return false;
        const auto first=path.begin()->string();
        return first=="generated"||first=="lod"||path=="spawn.cache";
    };
    if(cache(relative))return true;
    for(const auto& dimension:dimensions)
        if(under(relative,dimension)&&cache(relative.lexically_relative(dimension)))return true;
    return false;
}
}

void SaveStore::recoverFileTo(const std::filesystem::path& relative,const std::filesystem::path& destination) const {
    const auto file=durableFile(relative);
    SaveStore source(m_worldDirectory/file.root,m_policy,m_issues),target(destination/file.root);
    if(file.kind) {
        const auto checked=readChecked(m_worldDirectory/relative);
        if(checked.version!=SAVE_FORMAT_VERSION)throw std::runtime_error("Durable world partitions require save format v21");
    }
    switch(file.kind) {
        case 'l': {auto metadata=source.loadMetadata();if(!WorldGenContext::canLoadGeneration(metadata.generationVersion))throw std::runtime_error("World generation version is incompatible");if(!destination.empty()) {
            target.saveMetadata(metadata);
            if(metadata.needsRecoverySpawn&&!std::filesystem::exists(m_worldDirectory/RECOVERY_SPAWN_MARKER)) {
                std::ofstream marker(destination/RECOVERY_SPAWN_MARKER,std::ios::binary);marker<<"pending\n";marker.close();
                if(!marker)throw std::runtime_error("Cannot record recovery landing check");
            }
        }break;}
        case 'c': {const auto values=source.loadChunkOverrides(file.x,file.z);if(!destination.empty())target.saveChunkOverrides(file.x,file.z,values);break;}
        case 'b': {
            auto values=source.loadBlockEntities(file.x,file.z);
            if(m_policy==SaveReadPolicy::RecoverPlugins) {
                const auto blocks=source.loadChunkOverrides(file.x,file.z);
                values.erase(std::remove_if(values.begin(),values.end(),[&](const auto& value){
                    if(value.value.type==BlockEntityType::Resource)return value.value.resourceBlock==0;
                    const auto block=std::find_if(blocks.begin(),blocks.end(),[&](const auto& b){return b.localIndex==value.localIndex;});
                    return block!=blocks.end()&&block->block==BlockId::AIR;
                }),values.end());
            }
            if(!destination.empty())target.saveBlockEntities(file.x,file.z,values);break;
        }
        case 'e': {const auto values=source.loadChunkEntities(file.x,file.z);if(!destination.empty())target.saveChunkEntities(file.x,file.z,values);break;}
        case 'p': {const auto value=source.loadChunkEntityPopulationVersion(file.x,file.z);if(!destination.empty())target.saveChunkEntityPopulationVersion(file.x,file.z,value);break;}
        default: if(!destination.empty()){std::filesystem::create_directories((destination/relative).parent_path());std::filesystem::copy_file(m_worldDirectory/relative,destination/relative);}break;
    }
}

void SaveStore::validatePluginFiles() const {
    for(const auto& relative:worldFiles(m_worldDirectory))if(durableFile(relative).kind)recoverFileTo(relative,{});
}

WorldCompatibilityReport SaveStore::inspectCompatibility() const {
    WorldCompatibilityReport result;
    try {
        SaveStore recover(m_worldDirectory,SaveReadPolicy::RecoverPlugins,&result.issues);
        const auto metadata=recover.loadMetadata();
        if(!WorldGenContext::canLoadGeneration(metadata.generationVersion))throw std::runtime_error("World generation version is incompatible");
        uint64_t hash=compatibilityHashSeed();
        for(const auto& relative:worldFiles(m_worldDirectory)) {
            hash=hashText(relative.generic_u8string(),hash);
            hash=hashText(std::to_string(std::filesystem::file_size(m_worldDirectory/relative)),hash);
            hash=hashFile(m_worldDirectory/relative,hash);
            if(durableFile(relative).kind)recover.recoverFileTo(relative,{});
        }
        std::sort(result.issues.begin(),result.issues.end());
        result.issues.erase(std::unique(result.issues.begin(),result.issues.end()),result.issues.end());
        result.token=std::to_string(hash);
    }catch(const std::exception& e){result.blocker=e.what();}
    return result;
}

WorldRecovery::WorldRecovery(std::filesystem::path world,const std::string& confirmedToken,bool keepBackup)
    :m_world(std::filesystem::absolute(std::move(world)).lexically_normal()),m_keepBackup(keepBackup),m_confirmedToken(confirmedToken) {
    const auto report=SaveStore(m_world).inspectCompatibility();
    if(!report.recoverable()||report.token!=confirmedToken)throw std::runtime_error("World/plugin compatibility changed; refresh the world list and review the warning again");
    const auto base=m_world.parent_path()/".plugin-recovery";
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(base)))throw std::runtime_error("Recovery directory is a symbolic link");
    std::filesystem::create_directories(base);
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    for(unsigned n=0;;++n){m_transaction=base/(m_world.filename().string()+"-"+std::to_string(stamp)+"-"+std::to_string(n));if(std::filesystem::create_directory(m_transaction))break;}
    m_stage=m_transaction/"staged";m_backup=m_transaction/"original";
    try {
        std::ofstream journal(m_transaction/"world-id",std::ios::binary);journal<<m_world.filename().string();journal.close();if(!journal)throw std::runtime_error("Cannot write recovery journal");
        if(!m_keepBackup) {
            std::ofstream discard(m_transaction/"discard-original",std::ios::binary);discard<<"discard after publication\n";discard.close();
            if(!discard)throw std::runtime_error("Cannot record recovery backup choice");
        }
        std::filesystem::create_directory(m_stage);
        const auto metadata=SaveStore(m_world,SaveReadPolicy::RecoverPlugins).loadMetadata();
        std::vector<std::filesystem::path> removed,dimensions;
        for(const auto& dimension:metadata.savedDimensionKeys)if(dimension.first!=0) {
            const auto directory=std::filesystem::path("dimensions")/directoryForKey(dimension.second);
            dimensions.push_back(directory);
            if(!Plugins::validDimension(dimension.first))removed.push_back(directory);
        }
        m_sourceFiles=worldFiles(m_world);
        for(const auto& relative:m_sourceFiles) {
            if(disposable(relative,dimensions))continue;
            if(std::any_of(removed.begin(),removed.end(),[&](const auto& prefix){return under(relative,prefix);}))continue;
            m_files.push_back(relative);
        }
    }catch(...){rollback();throw;}
}
WorldRecovery::~WorldRecovery(){if(!m_done)cancel();}
float WorldRecovery::progress() const {
    if(m_done)return 1.0f;
    const float fraction=static_cast<float>(m_next)/static_cast<float>((m_phase==Phase::VerifySource?m_sourceFiles:m_files).size()+1);
    return m_phase==Phase::Convert?fraction*.6f:m_phase==Phase::Validate?.6f+fraction*.2f:m_phase==Phase::VerifySource?.8f+fraction*.19f:.99f;
}
void WorldRecovery::rollback() {
    if(m_published && std::filesystem::exists(m_backup)) {
        // Invalidate completion before the rollback can leave the world path
        // temporarily absent. An interruption must restore the original.
        std::filesystem::remove(m_transaction/"committed");
        std::filesystem::rename(m_world,m_stage);m_published=false;
    }
    if(!m_backup.empty() && std::filesystem::exists(m_backup) && !std::filesystem::exists(m_world))std::filesystem::rename(m_backup,m_world);
    // Paths are exclusively created children of the validated recovery directory.
    if(!m_stage.empty() && std::filesystem::exists(m_stage))std::filesystem::remove_all(m_stage);
}
void WorldRecovery::cancel(){if(m_done)return;try{rollback();}catch(const std::exception& e){m_error=e.what();}m_done=true;}
void WorldRecovery::step() {
    if(m_done)return;
    try {
        if(m_phase==Phase::Convert) {
            if(m_next<m_files.size()) {SaveStore(m_world,SaveReadPolicy::RecoverPlugins).recoverFileTo(m_files[m_next],m_stage);++m_next;return;}
            m_next=0;m_phase=Phase::Validate;return;
        }
        if(m_phase==Phase::Validate) {
            if(m_next<m_files.size()) {SaveStore(m_stage).recoverFileTo(m_files[m_next],{});++m_next;return;}
            SaveStore(m_stage).loadMetadata();
            m_next=0;m_sourceHash=compatibilityHashSeed();m_phase=Phase::VerifySource;return;
        }
        if(m_phase==Phase::VerifySource) {
            if(m_next<m_sourceFiles.size()) {
                const auto& relative=m_sourceFiles[m_next];
                m_sourceHash=hashText(relative.generic_u8string(),m_sourceHash);
                m_sourceHash=hashText(std::to_string(std::filesystem::file_size(m_world/relative)),m_sourceHash);
                m_sourceHash=hashFile(m_world/relative,m_sourceHash);++m_next;return;
            }
            if(worldFiles(m_world)!=m_sourceFiles || std::to_string(m_sourceHash)!=m_confirmedToken)
                throw std::runtime_error("World/plugins changed during recovery; refresh the world list and review the warning again");
            m_phase=Phase::Publish;
        }
        std::filesystem::rename(m_world,m_backup);
        try {std::filesystem::rename(m_stage,m_world);}catch(...){std::filesystem::rename(m_backup,m_world);throw;}
        m_published=true;
        std::ofstream committed(m_transaction/"committed",std::ios::binary);committed<<"published\n";committed.close();
        if(!committed)throw std::runtime_error("Cannot finalize recovery journal");
        m_succeeded=true;m_done=true;
    }catch(const std::exception& e){m_error=e.what();try{rollback();}catch(const std::exception& rollbackError){m_error+="; rollback: "+std::string(rollbackError.what());}m_done=true;}
    if(m_succeeded && !m_keepBackup) {
        // Publication is committed: cleanup must never roll back a partially deleted original.
        // Keep the journal so interrupted/failed cleanup is retried before listing worlds.
        try {
            std::error_code error;
            std::filesystem::remove_all(m_backup,error);
            if(!error)m_backup.clear();
        }catch(const std::exception&) { /* Publication succeeded; report pending cleanup in the UI. */ }
    }
}
void WorldRecovery::restoreInterrupted(const std::filesystem::path& saves) {
    const auto base=saves/".plugin-recovery";
    if(!std::filesystem::exists(base))return;
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(base)))throw std::runtime_error("Recovery directory is a symbolic link");
    for(const auto& transaction:std::filesystem::directory_iterator(base)) {
        if(transaction.is_symlink()||!transaction.is_directory())continue;
        std::ifstream completion(transaction.path()/"committed",std::ios::binary);
        std::string completionState;std::getline(completion,completionState);
        if(completionState=="published") {
            if(std::filesystem::exists(transaction.path()/"discard-original")) {
                const auto original=transaction.path()/"original";
                if(std::filesystem::is_symlink(std::filesystem::symlink_status(original)))throw std::runtime_error("Recovery transaction contains symbolic links");
                std::error_code error;std::filesystem::remove_all(original,error);
            }
            continue;
        }
        std::ifstream journal(transaction.path()/"world-id",std::ios::binary);std::string id;std::getline(journal,id);
        if(id.empty()||id=="."||id==".."||!std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';}))continue;
        const auto backup=transaction.path()/"original",stage=transaction.path()/"staged",world=saves/id;
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(backup))||std::filesystem::is_symlink(std::filesystem::symlink_status(stage)))throw std::runtime_error("Recovery transaction contains symbolic links");
        if(std::filesystem::exists(backup)&&!std::filesystem::exists(world))std::filesystem::rename(backup,world);
        else if(std::filesystem::exists(backup)&&std::filesystem::exists(world)&&!std::filesystem::exists(stage)) {
            // Publication completed before interruption; record completion so a
            // subsequent explicit deletion never resurrects an old backup.
            std::ofstream committed(transaction.path()/"committed",std::ios::binary);committed<<"published\n";committed.close();
            if(!committed)throw std::runtime_error("Cannot finalize interrupted recovery journal");
            if(std::filesystem::exists(transaction.path()/"discard-original")) {
                std::error_code error;std::filesystem::remove_all(backup,error);
            }
        }
    }
}
