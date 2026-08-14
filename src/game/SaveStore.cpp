#include "game/SaveStore.h"
#include "Config.h"
#include "core/Platform.h"
#include "entity/EntityLogic.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

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
    explicit Reader(Bytes bytes) : m_bytes(std::move(bytes)) {}

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

void writeAtomic(const std::filesystem::path& path, const Bytes& payload) {
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
    stack.id = static_cast<ItemId>(reader.read<uint16_t>());
    stack.count = reader.read<uint8_t>();
    stack.damage = reader.read<uint16_t>();
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
    // EntityType values are serialized as uint8_t; PrimedTnt is the highest
    // valid value, so reject anything beyond it.
    if (entity.type > static_cast<uint8_t>(EntityType::PrimedTnt))
        throw std::runtime_error("Save contains invalid entity type");
    return entity;
}

void appendBlockEntity(Bytes& payload, const PersistedBlockEntity& entity) {
    append(payload, entity.localIndex);
    append(payload, static_cast<uint8_t>(entity.value.type));
    if (entity.value.type == BlockEntityType::Chest) {
        for (const auto& stack : entity.value.chest) appendStack(payload, stack);
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
    if (type > static_cast<uint8_t>(BlockEntityType::Furnace))
        throw std::runtime_error("Invalid block entity type");
    entity.value.type = static_cast<BlockEntityType>(type);
    if (entity.value.type == BlockEntityType::Chest) {
        for (auto& stack : entity.value.chest) stack = readStack(reader);
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
    writeAtomic(m_worldDirectory / "level.bin", payload);
}

WorldMetadata SaveStore::loadMetadata() const {
    CheckedBytes checked = readChecked(m_worldDirectory / "level.bin");
    Reader reader(std::move(checked.payload));
    WorldMetadata metadata;
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
    if (!reader.finished()) throw std::runtime_error("Unexpected trailing save data");
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

void SaveStore::saveChunkOverrides(
    int chunkX, int chunkZ, const std::vector<BlockOverride>& overrides) const {
    Bytes payload;
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, static_cast<uint32_t>(overrides.size()));
    for (const auto& entry : overrides) {
        if (entry.localIndex >= static_cast<uint32_t>(Config::CHUNK_VOLUME) ||
            static_cast<uint8_t>(entry.block) >= static_cast<uint8_t>(BlockId::COUNT))
            throw std::runtime_error("Invalid block override");
        append(payload, entry.localIndex);
        append(payload, static_cast<uint8_t>(entry.block));
    }
    writeAtomic(chunkPath(chunkX, chunkZ), payload);
}

std::vector<BlockOverride> SaveStore::loadChunkOverrides(int chunkX, int chunkZ) const {
    const auto path = chunkPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return {};
    CheckedBytes checked = readChecked(path);
    Reader reader(std::move(checked.payload));
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
        entry.block = static_cast<BlockId>(reader.read<uint8_t>());
        const uint32_t legacyLimit = 16u * 128u * 16u;
        const uint32_t limit = checked.version >= 6
            ? static_cast<uint32_t>(Config::CHUNK_VOLUME) : legacyLimit;
        if (entry.localIndex >= limit ||
            static_cast<uint8_t>(entry.block) >= static_cast<uint8_t>(BlockId::COUNT))
            throw std::runtime_error("Invalid block override");
        overrides.push_back(entry);
    }
    if (!reader.finished()) throw std::runtime_error("Unexpected trailing chunk data");
    return overrides;
}

void SaveStore::saveGeneratedChunk(
    int chunkX, int chunkZ, const std::vector<uint8_t>& blocks,
    uint32_t generationVersion) const {
    if (blocks.size() != static_cast<size_t>(Config::CHUNK_VOLUME))
        throw std::runtime_error("Invalid generated chunk size");
    Bytes payload;
    payload.reserve(sizeof(int32_t) * 2 + sizeof(uint32_t) * 2 + blocks.size());
    append(payload, static_cast<int32_t>(chunkX));
    append(payload, static_cast<int32_t>(chunkZ));
    append(payload, generationVersion);
    append(payload, static_cast<uint32_t>(blocks.size()));
    payload.insert(payload.end(), blocks.begin(), blocks.end());
    writeAtomic(generatedChunkPath(chunkX, chunkZ), payload);
}

std::optional<std::vector<uint8_t>> SaveStore::loadGeneratedChunk(
    int chunkX, int chunkZ, uint32_t generationVersion) const {
    const auto path = generatedChunkPath(chunkX, chunkZ);
    if (!std::filesystem::exists(path)) return std::nullopt;
    try {
        CheckedBytes checked = readChecked(path);
        Reader reader(std::move(checked.payload));
        if (reader.read<int32_t>() != chunkX || reader.read<int32_t>() != chunkZ ||
            reader.read<uint32_t>() != generationVersion)
            return std::nullopt;
        const uint32_t size = reader.read<uint32_t>();
        if (size != static_cast<uint32_t>(Config::CHUNK_VOLUME)) return std::nullopt;
        std::vector<uint8_t> blocks;
        blocks.reserve(size);
        for (uint32_t i = 0; i < size; ++i) {
            const uint8_t block = reader.read<uint8_t>();
            if (block >= static_cast<uint8_t>(BlockId::COUNT)) return std::nullopt;
            blocks.push_back(block);
        }
        if (!reader.finished()) return std::nullopt;
        return blocks;
    } catch (const std::exception&) {
        return std::nullopt;
    }
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
    Reader reader(std::move(checked.payload));
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
    Reader reader(std::move(checked.payload));
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
