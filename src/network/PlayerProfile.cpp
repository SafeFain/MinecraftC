#include "network/PlayerProfile.h"
#include "core/Platform.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Lan {
namespace {
constexpr uint32_t PROFILE_MAGIC=0x504e414c;
constexpr uint16_t PROFILE_VERSION=2;
constexpr size_t MAX_PROFILE_BYTES=4096;
uint64_t checksum(const Bytes& bytes) {
    uint64_t value=14695981039346656037ull;
    for(const auto byte:bytes) {value^=byte;value*=1099511628211ull;}
    return value;
}
void writeStack(Writer& writer,const ItemStack& stack) {
    if(!isValidItemId(stack.id) || stack.count>getItemProps(stack.id).maxStack ||
        ((stack.id==ItemId::EMPTY)!=(stack.count==0))) throw ProtocolError("Invalid profile item stack");
    writer.u16(static_cast<uint16_t>(stack.id));writer.u8(stack.count);writer.u16(stack.damage);
}
ItemStack readStack(Reader& reader) {
    const auto id=static_cast<ItemId>(reader.u16());const auto count=reader.u8();const auto damage=reader.u16();
    if(!isValidItemId(id) || count>getItemProps(id).maxStack || ((id==ItemId::EMPTY)!=(count==0)))
        throw ProtocolError("Invalid profile item stack");
    return {id,count,damage};
}
void validatePosition(const glm::dvec3& position) {
    for(int i=0;i<3;++i) if(!std::isfinite(position[i]) || std::abs(position[i])>30000000.0) throw ProtocolError("Invalid profile position");
}
void validateStats(const PlayerProfile& profile) {
    if(profile.mode>GameMode::Spectator || profile.dimension>DimensionId::Heaven || !validIdentity(profile.identity) ||
        !std::isfinite(profile.health) || profile.health<0 || profile.health>20 || profile.hunger>20 ||
        !std::isfinite(profile.saturation) || profile.saturation<0 || profile.saturation>20 ||
        !std::isfinite(profile.exhaustion) || profile.exhaustion<0 || profile.exhaustion>40)
        throw ProtocolError("Invalid player profile");
    for(const auto& position:profile.positions) validatePosition(position);
    if(profile.bedSpawn) validatePosition(glm::dvec3(*profile.bedSpawn));
}
Bytes readFile(const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(size>MAX_PROFILE_BYTES || !size) throw ProtocolError("Invalid player profile size");
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read player profile");
    Bytes bytes(static_cast<size_t>(size));input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!input) throw std::runtime_error("Cannot read complete player profile");
    return bytes;
}
void writeFile(const std::filesystem::path& path,const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary=path.string()+".tmp-"+randomToken();
    try {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        if(!output) throw std::runtime_error("Cannot create player profile temporary file");
        output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));output.flush();
        if(!output) throw std::runtime_error("Cannot write player profile");
        output.close();if(!output) throw std::runtime_error("Cannot close player profile");
        std::error_code error;
        if(!Platform::replaceFileAtomically(temporary,path,error)) throw std::runtime_error("Cannot replace player profile: "+error.message());
    } catch(...) {std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
Bytes encodeProfile(const PlayerProfile& profile) {
    validateStats(profile);Writer writer;writer.u32(PROFILE_MAGIC);writer.u16(PROFILE_VERSION);
    writer.text(profile.identity.id,32);writer.text(profile.identity.credential,32);writer.text(profile.identity.nickname,64);
    writer.u8(static_cast<uint8_t>(profile.mode));writer.u8(static_cast<uint8_t>(profile.dimension));
    for(size_t i=0;i<2;++i) {writer.u8(profile.positioned[i]?1:0);for(int axis=0;axis<3;++axis) writer.f64(profile.positions[i][axis]);}
    writer.u8(profile.bedSpawn?1:0);
    if(profile.bedSpawn) for(int axis=0;axis<3;++axis) writer.u32(static_cast<uint32_t>((*profile.bedSpawn)[axis]));
    writer.f32(profile.health);writer.u8(profile.hunger);writer.f32(profile.saturation);writer.f32(profile.exhaustion);writer.u32(profile.foodTickTimer);
    for(const auto& stack:profile.inventory.storage()) writeStack(writer,stack);
    for(const auto& stack:profile.inventory.armor()) writeStack(writer,stack);
    writeStack(writer,profile.inventory.offhand());
    writeStack(writer,profile.cursor); for (auto item : profile.crafting) writeStack(writer,item);
    const auto hash=checksum(writer.bytes);writer.u64(hash);return std::move(writer.bytes);
}
PlayerProfile decodeProfile(const Bytes& bytes) {
    if(bytes.size()<14 || bytes.size()>MAX_PROFILE_BYTES) throw ProtocolError("Invalid player profile size");
    Bytes payload(bytes.begin(),bytes.end()-8),trailer(bytes.end()-8,bytes.end());Reader check(trailer);
    if(check.u64()!=checksum(payload)) throw ProtocolError("Player profile checksum mismatch");
    Reader reader(payload);
    if(reader.u32()!=PROFILE_MAGIC) throw ProtocolError("Unsupported player profile format");
    const auto version=reader.u16();
    if(version<1 || version>PROFILE_VERSION) throw ProtocolError("Unsupported player profile format");
    PlayerProfile profile;profile.identity={reader.text(32),reader.text(32),reader.text(64)};
    profile.mode=static_cast<GameMode>(reader.u8());profile.dimension=static_cast<DimensionId>(reader.u8());
    for(size_t i=0;i<2;++i) {const auto positioned=reader.u8();if(positioned>1) throw ProtocolError("Invalid profile flag");profile.positioned[i]=positioned!=0;for(int axis=0;axis<3;++axis) profile.positions[i][axis]=reader.f64();}
    const auto bed=reader.u8();if(bed>1) throw ProtocolError("Invalid profile flag");
    if(bed) {glm::ivec3 position;for(int axis=0;axis<3;++axis) {const uint32_t value=reader.u32();position[axis]=value<=INT32_MAX?static_cast<int32_t>(value):static_cast<int32_t>(-1-static_cast<int64_t>(UINT32_MAX-value));}profile.bedSpawn=position;}
    profile.health=reader.f32();profile.hunger=reader.u8();profile.saturation=reader.f32();profile.exhaustion=reader.f32();profile.foodTickTimer=reader.u32();
    for(size_t i=0;i<36;++i) profile.inventory.slot(i)=readStack(reader);
    for(auto& stack:profile.inventory.armor()) stack=readStack(reader);
    profile.inventory.offhand()=readStack(reader);
    if (version >= 2) { profile.cursor=readStack(reader); for (auto& item : profile.crafting) item=readStack(reader); }
    reader.finish();validateStats(profile);return profile;
}
std::filesystem::path ProfileStore::path(const std::string& id) const {
    if(!validIdentity({id,std::string(32,'0'),"Player"})) throw ProtocolError("Invalid player profile identity");
    return m_directory/(id+".lanplayer");
}
Identity ProfileStore::localIdentity(const std::filesystem::path& directory) {
    const auto file=directory/"lan-identity";
    if(std::filesystem::exists(file)) return decodeProfile(readFile(file)).identity;
    PlayerProfile profile;profile.identity={randomToken(),randomToken(),"Player"};writeFile(file,encodeProfile(profile));return profile.identity;
}
bool ProfileStore::setLocalNickname(const std::filesystem::path& directory, const std::string& nickname) {
    auto identity = localIdentity(directory); identity.nickname = nickname;
    if (!validIdentity(identity)) return false;
    PlayerProfile profile; profile.identity = identity;
    writeFile(directory/"lan-identity", encodeProfile(profile)); return true;
}
std::optional<PlayerProfile> ProfileStore::load(const Identity& identity) const {
    if(!validIdentity(identity)) throw ProtocolError("Invalid player identity");
    const auto file=path(identity.id);if(!std::filesystem::exists(file)) return std::nullopt;
    auto profile=decodeProfile(readFile(file));
    if(profile.identity.id!=identity.id || profile.identity.credential!=identity.credential) throw ProtocolError("Player credential mismatch");
    profile.identity.nickname=identity.nickname;return profile;
}
bool ProfileStore::accepts(const Identity& identity) const {
    try {load(identity);return true;} catch(const std::exception&) {return false;}
}
void ProfileStore::save(const PlayerProfile& profile) const {
    // Never replace another identity's existing credential, even if a caller
    // accidentally supplies an unauthenticated profile.
    load(profile.identity);writeFile(path(profile.identity.id),encodeProfile(profile));
}
}
