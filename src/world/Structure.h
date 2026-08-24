#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

enum class StructureType : uint8_t {
    None = 0,
    Village,
    DesertVillage,
    TravelerHut,
    AbandonedCamp,
    DesertWell,
    Igloo,
    RuinedTower,
    LumberCamp,
    XiguangRuin,
    StarCrystalGeode,
    CloudspireTower,
    Count
};

inline constexpr std::array<StructureType, 8> OVERWORLD_STRUCTURE_TYPES{
    StructureType::Village,
    StructureType::DesertVillage,
    StructureType::TravelerHut,
    StructureType::AbandonedCamp,
    StructureType::DesertWell,
    StructureType::Igloo,
    StructureType::RuinedTower,
    StructureType::LumberCamp,
};

inline constexpr std::array<StructureType, 3> HEAVEN_STRUCTURE_TYPES{
    StructureType::XiguangRuin,
    StructureType::StarCrystalGeode,
    StructureType::CloudspireTower,
};

inline constexpr std::array<StructureType, 11> STRUCTURE_TYPES{
    StructureType::Village,
    StructureType::DesertVillage,
    StructureType::TravelerHut,
    StructureType::AbandonedCamp,
    StructureType::DesertWell,
    StructureType::Igloo,
    StructureType::RuinedTower,
    StructureType::LumberCamp,
    StructureType::XiguangRuin,
    StructureType::StarCrystalGeode,
    StructureType::CloudspireTower,
};

inline constexpr bool isOverworldStructure(StructureType type) {
    for (const StructureType candidate : OVERWORLD_STRUCTURE_TYPES)
        if (candidate == type) return true;
    return false;
}

inline constexpr bool isHeavenStructure(StructureType type) {
    for (const StructureType candidate : HEAVEN_STRUCTURE_TYPES)
        if (candidate == type) return true;
    return false;
}

inline constexpr std::string_view structureCommandName(StructureType type) {
    switch (type) {
        case StructureType::Village: return "village";
        case StructureType::DesertVillage: return "desert_village";
        case StructureType::TravelerHut: return "traveler_hut";
        case StructureType::AbandonedCamp: return "abandoned_camp";
        case StructureType::DesertWell: return "desert_well";
        case StructureType::Igloo: return "igloo";
        case StructureType::RuinedTower: return "ruined_tower";
        case StructureType::LumberCamp: return "lumber_camp";
        case StructureType::XiguangRuin: return "xiguang_ruin";
        case StructureType::StarCrystalGeode: return "star_crystal_geode";
        case StructureType::CloudspireTower: return "cloudspire_tower";
        default: return "none";
    }
}

inline std::optional<StructureType> parseStructureCommandName(
    std::string_view name) {
    constexpr std::string_view projectNamespace = "minecraftc:";
    if (name.substr(0, projectNamespace.size()) == projectNamespace)
        name.remove_prefix(projectNamespace.size());
    for (const StructureType type : STRUCTURE_TYPES)
        if (name == structureCommandName(type)) return type;

    // Accept the Java registry's word order for the two village variants as
    // aliases while keeping MinecraftC's established names canonical.
    if (name == "village_plains" || name == "plains_village")
        return StructureType::Village;
    if (name == "village_desert") return StructureType::DesertVillage;
    return {};
}
