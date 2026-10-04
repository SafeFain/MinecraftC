#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Factual baseline: Mojang Java 1.21.11 server GameRules registrations.
// See tests/data/java-1.21.11-gamerules.tsv and docs/game-rules.md.
enum class GameRuleId : uint8_t {
    AdvanceTime,
    AdvanceWeather,
    AllowEnteringNetherUsingPortals,
    BlockDrops,
    BlockExplosionDropDecay,
    CommandBlocksWork,
    CommandBlockOutput,
    DrowningDamage,
    ElytraMovementCheck,
    EnderPearlsVanishOnDeath,
    EntityDrops,
    FallDamage,
    FireDamage,
    FireSpreadRadiusAroundPlayer,
    ForgiveDeadPlayers,
    FreezeDamage,
    GlobalSoundEvents,
    ImmediateRespawn,
    KeepInventory,
    LavaSourceConversion,
    LimitedCrafting,
    LocatorBar,
    LogAdminCommands,
    MaxBlockModifications,
    MaxCommandForks,
    MaxCommandSequenceLength,
    MaxEntityCramming,
    MaxMinecartSpeed,
    MaxSnowAccumulationHeight,
    MobDrops,
    MobExplosionDropDecay,
    MobGriefing,
    NaturalHealthRegeneration,
    PlayerMovementCheck,
    PlayersNetherPortalCreativeDelay,
    PlayersNetherPortalDefaultDelay,
    PlayersSleepingPercentage,
    ProjectilesCanBreakBlocks,
    Pvp,
    Raids,
    RandomTickSpeed,
    ReducedDebugInfo,
    RespawnRadius,
    SendCommandFeedback,
    ShowAdvancementMessages,
    ShowDeathMessages,
    SpawnerBlocksWork,
    SpawnMobs,
    SpawnMonsters,
    SpawnPatrols,
    SpawnPhantoms,
    SpawnWanderingTraders,
    SpawnWardens,
    SpectatorsGenerateChunks,
    SpreadVines,
    TntExplodes,
    TntExplosionDropDecay,
    UniversalAnger,
    WaterSourceConversion,
    DayNightDuration,
    Count
};
enum class GameRuleType : uint8_t { Boolean, Integer };
enum class GameRuleSupport : uint8_t { Implemented, Partial, Unavailable };
enum class GameRuleAdapter : uint8_t { Direct, Inverted, FireEnabled, FireAway };

struct GameRuleValue {
    GameRuleType type = GameRuleType::Boolean;
    int64_t number = 0;
    static GameRuleValue boolean(bool value) { return {GameRuleType::Boolean, value ? 1 : 0}; }
    static GameRuleValue integer(int64_t value) { return {GameRuleType::Integer, value}; }
    bool operator==(const GameRuleValue& other) const {
        return type == other.type && number == other.number;
    }
    bool operator!=(const GameRuleValue& other) const { return !(*this == other); }
};
struct GameRuleDefinition {
    GameRuleId id;
    std::string_view name;
    std::string_view fullName;
    std::string_view legacyName;
    GameRuleType type;
    int64_t defaultValue;
    int64_t minimum;
    int64_t maximum;
    GameRuleSupport support;
    GameRuleAdapter legacyAdapter = GameRuleAdapter::Direct;
};
inline constexpr std::array<GameRuleDefinition, static_cast<size_t>(GameRuleId::Count)> GAME_RULES{{
    {GameRuleId::AdvanceTime, "advance_time", "minecraft:advance_time", "doDaylightCycle", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::AdvanceWeather, "advance_weather", "minecraft:advance_weather", "doWeatherCycle", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::AllowEnteringNetherUsingPortals, "allow_entering_nether_using_portals", "minecraft:allow_entering_nether_using_portals", "allowEnteringNetherUsingPortals", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::BlockDrops, "block_drops", "minecraft:block_drops", "doTileDrops", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::BlockExplosionDropDecay, "block_explosion_drop_decay", "minecraft:block_explosion_drop_decay", "blockExplosionDropDecay", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::CommandBlocksWork, "command_blocks_work", "minecraft:command_blocks_work", "commandBlocksEnabled", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::CommandBlockOutput, "command_block_output", "minecraft:command_block_output", "commandBlockOutput", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::DrowningDamage, "drowning_damage", "minecraft:drowning_damage", "drowningDamage", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::ElytraMovementCheck, "elytra_movement_check", "minecraft:elytra_movement_check", "disableElytraMovementCheck", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Inverted},
    {GameRuleId::EnderPearlsVanishOnDeath, "ender_pearls_vanish_on_death", "minecraft:ender_pearls_vanish_on_death", "enderPearlsVanishOnDeath", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::EntityDrops, "entity_drops", "minecraft:entity_drops", "doEntityDrops", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::FallDamage, "fall_damage", "minecraft:fall_damage", "fallDamage", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::FireDamage, "fire_damage", "minecraft:fire_damage", "fireDamage", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::FireSpreadRadiusAroundPlayer, "fire_spread_radius_around_player", "minecraft:fire_spread_radius_around_player", "fireSpreadRadiusAroundPlayer", GameRuleType::Integer, 128, -1, 2147483647, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::ForgiveDeadPlayers, "forgive_dead_players", "minecraft:forgive_dead_players", "forgiveDeadPlayers", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::FreezeDamage, "freeze_damage", "minecraft:freeze_damage", "freezeDamage", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::GlobalSoundEvents, "global_sound_events", "minecraft:global_sound_events", "globalSoundEvents", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::ImmediateRespawn, "immediate_respawn", "minecraft:immediate_respawn", "doImmediateRespawn", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::KeepInventory, "keep_inventory", "minecraft:keep_inventory", "keepInventory", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::LavaSourceConversion, "lava_source_conversion", "minecraft:lava_source_conversion", "lavaSourceConversion", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::LimitedCrafting, "limited_crafting", "minecraft:limited_crafting", "doLimitedCrafting", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::LocatorBar, "locator_bar", "minecraft:locator_bar", "locatorBar", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::LogAdminCommands, "log_admin_commands", "minecraft:log_admin_commands", "logAdminCommands", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::MaxBlockModifications, "max_block_modifications", "minecraft:max_block_modifications", "commandModificationBlockLimit", GameRuleType::Integer, 32768, 1, 2147483647, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::MaxCommandForks, "max_command_forks", "minecraft:max_command_forks", "maxCommandForkCount", GameRuleType::Integer, 65536, 0, 2147483647, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::MaxCommandSequenceLength, "max_command_sequence_length", "minecraft:max_command_sequence_length", "maxCommandChainLength", GameRuleType::Integer, 65536, 0, 2147483647, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::MaxEntityCramming, "max_entity_cramming", "minecraft:max_entity_cramming", "maxEntityCramming", GameRuleType::Integer, 24, 0, 2147483647, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::MaxMinecartSpeed, "max_minecart_speed", "minecraft:max_minecart_speed", "minecartMaxSpeed", GameRuleType::Integer, 8, 1, 1000, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::MaxSnowAccumulationHeight, "max_snow_accumulation_height", "minecraft:max_snow_accumulation_height", "snowAccumulationHeight", GameRuleType::Integer, 1, 0, 8, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::MobDrops, "mob_drops", "minecraft:mob_drops", "doMobLoot", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::MobExplosionDropDecay, "mob_explosion_drop_decay", "minecraft:mob_explosion_drop_decay", "mobExplosionDropDecay", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::MobGriefing, "mob_griefing", "minecraft:mob_griefing", "mobGriefing", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::NaturalHealthRegeneration, "natural_health_regeneration", "minecraft:natural_health_regeneration", "naturalRegeneration", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::PlayerMovementCheck, "player_movement_check", "minecraft:player_movement_check", "disablePlayerMovementCheck", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Inverted},
    {GameRuleId::PlayersNetherPortalCreativeDelay, "players_nether_portal_creative_delay", "minecraft:players_nether_portal_creative_delay", "playersNetherPortalCreativeDelay", GameRuleType::Integer, 0, 0, 2147483647, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::PlayersNetherPortalDefaultDelay, "players_nether_portal_default_delay", "minecraft:players_nether_portal_default_delay", "playersNetherPortalDefaultDelay", GameRuleType::Integer, 80, 0, 2147483647, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::PlayersSleepingPercentage, "players_sleeping_percentage", "minecraft:players_sleeping_percentage", "playersSleepingPercentage", GameRuleType::Integer, 100, 0, 2147483647, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::ProjectilesCanBreakBlocks, "projectiles_can_break_blocks", "minecraft:projectiles_can_break_blocks", "projectilesCanBreakBlocks", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::Pvp, "pvp", "minecraft:pvp", "pvp", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::Raids, "raids", "minecraft:raids", "disableRaids", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Inverted},
    {GameRuleId::RandomTickSpeed, "random_tick_speed", "minecraft:random_tick_speed", "randomTickSpeed", GameRuleType::Integer, 3, 0, 2147483647, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::ReducedDebugInfo, "reduced_debug_info", "minecraft:reduced_debug_info", "reducedDebugInfo", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::RespawnRadius, "respawn_radius", "minecraft:respawn_radius", "spawnRadius", GameRuleType::Integer, 10, 0, 2147483647, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::SendCommandFeedback, "send_command_feedback", "minecraft:send_command_feedback", "sendCommandFeedback", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::ShowAdvancementMessages, "show_advancement_messages", "minecraft:show_advancement_messages", "announceAdvancements", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::ShowDeathMessages, "show_death_messages", "minecraft:show_death_messages", "showDeathMessages", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::SpawnerBlocksWork, "spawner_blocks_work", "minecraft:spawner_blocks_work", "spawnerBlocksEnabled", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::SpawnMobs, "spawn_mobs", "minecraft:spawn_mobs", "doMobSpawning", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::SpawnMonsters, "spawn_monsters", "minecraft:spawn_monsters", "spawnMonsters", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::SpawnPatrols, "spawn_patrols", "minecraft:spawn_patrols", "doPatrolSpawning", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::SpawnPhantoms, "spawn_phantoms", "minecraft:spawn_phantoms", "doInsomnia", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::SpawnWanderingTraders, "spawn_wandering_traders", "minecraft:spawn_wandering_traders", "doTraderSpawning", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::SpawnWardens, "spawn_wardens", "minecraft:spawn_wardens", "doWardenSpawning", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::SpectatorsGenerateChunks, "spectators_generate_chunks", "minecraft:spectators_generate_chunks", "spectatorsGenerateChunks", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::SpreadVines, "spread_vines", "minecraft:spread_vines", "doVinesSpread", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::TntExplodes, "tnt_explodes", "minecraft:tnt_explodes", "tntExplodes", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::TntExplosionDropDecay, "tnt_explosion_drop_decay", "minecraft:tnt_explosion_drop_decay", "tntExplosionDropDecay", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Partial, GameRuleAdapter::Direct},
    {GameRuleId::UniversalAnger, "universal_anger", "minecraft:universal_anger", "universalAnger", GameRuleType::Boolean, 0, 0, 1, GameRuleSupport::Unavailable, GameRuleAdapter::Direct},
    {GameRuleId::WaterSourceConversion, "water_source_conversion", "minecraft:water_source_conversion", "waterSourceConversion", GameRuleType::Boolean, 1, 0, 1, GameRuleSupport::Implemented, GameRuleAdapter::Direct},
    {GameRuleId::DayNightDuration, "DayNightDuration", "minecraftc:day_night_duration", "", GameRuleType::Integer, 1200, 1, 4294967295LL, GameRuleSupport::Implemented}
}};

inline const GameRuleDefinition& gameRuleDefinition(GameRuleId id) {
    return GAME_RULES.at(static_cast<size_t>(id));
}
struct GameRuleReference {
    GameRuleId id;
    GameRuleAdapter adapter = GameRuleAdapter::Direct;
};
inline std::optional<GameRuleReference> findGameRule(std::string_view name) {
    for (const auto& rule : GAME_RULES) {
        if (name == rule.name || name == rule.fullName) return GameRuleReference{rule.id};
        if (!rule.legacyName.empty() && name == rule.legacyName)
            return GameRuleReference{rule.id, rule.legacyAdapter};
    }
    if (name == "command_modification_block_limit") return GameRuleReference{GameRuleId::MaxBlockModifications};
    if (name == "doFireTick") return GameRuleReference{GameRuleId::FireSpreadRadiusAroundPlayer, GameRuleAdapter::FireEnabled};
    if (name == "allowFireTicksAwayFromPlayer") return GameRuleReference{GameRuleId::FireSpreadRadiusAroundPlayer, GameRuleAdapter::FireAway};
    return {};
}
inline GameRuleType gameRuleInputType(GameRuleReference ref) {
    return ref.adapter == GameRuleAdapter::FireEnabled || ref.adapter == GameRuleAdapter::FireAway
        ? GameRuleType::Boolean : gameRuleDefinition(ref.id).type;
}
inline bool validGameRuleValue(GameRuleId id, GameRuleValue value) {
    const auto& rule = gameRuleDefinition(id);
    return value.type == rule.type && value.number >= rule.minimum && value.number <= rule.maximum;
}
inline std::optional<GameRuleValue> parseGameRuleValue(GameRuleReference ref, std::string_view text) {
    GameRuleValue value;
    if (gameRuleInputType(ref) == GameRuleType::Boolean) {
        if (text != "true" && text != "false") return {};
        bool enabled = text == "true";
        if (ref.adapter == GameRuleAdapter::Inverted) enabled = !enabled;
        if (ref.adapter == GameRuleAdapter::FireEnabled)
            value = GameRuleValue::integer(enabled ? 128 : 0);
        else if (ref.adapter == GameRuleAdapter::FireAway)
            value = GameRuleValue::integer(enabled ? -1 : 128);
        else value = GameRuleValue::boolean(enabled);
    } else {
        int64_t number = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return {};
        value = GameRuleValue::integer(number);
    }
    return validGameRuleValue(ref.id, value) ? std::optional<GameRuleValue>(value) : std::nullopt;
}
inline GameRuleValue gameRuleQueryValue(GameRuleReference ref, GameRuleValue value) {
    if (ref.adapter == GameRuleAdapter::Inverted) return GameRuleValue::boolean(!value.number);
    if (ref.adapter == GameRuleAdapter::FireEnabled) return GameRuleValue::boolean(value.number != 0);
    if (ref.adapter == GameRuleAdapter::FireAway) return GameRuleValue::boolean(value.number == -1);
    return value;
}
inline std::string gameRuleValueText(GameRuleValue value) {
    return value.type == GameRuleType::Boolean ? (value.number ? "true" : "false") : std::to_string(value.number);
}
inline std::string gameRuleRange(GameRuleReference ref) {
    if (gameRuleInputType(ref) == GameRuleType::Boolean) return "true|false";
    const auto& rule = gameRuleDefinition(ref.id);
    return std::to_string(rule.minimum) + ".." + std::to_string(rule.maximum);
}

struct UnknownGameRule { std::string name; GameRuleValue value; };
class GameRuleSet {
public:
    GameRuleSet() {
        for (const auto& rule : GAME_RULES)
            if (rule.id != GameRuleId::DayNightDuration) m_values[static_cast<size_t>(rule.id)] = rule.defaultValue;
    }
    GameRuleValue get(GameRuleId id) const {
        return {gameRuleDefinition(id).type, m_values.at(static_cast<size_t>(id))};
    }
    bool boolean(GameRuleId id) const { return get(id).number != 0; }
    int64_t integer(GameRuleId id) const { return get(id).number; }
    bool set(GameRuleId id, GameRuleValue value) {
        // DayNightDuration has one authority in WorldMetadata, never this array.
        if (id == GameRuleId::DayNightDuration || !validGameRuleValue(id, value)) return false;
        m_values.at(static_cast<size_t>(id)) = value.number;
        return true;
    }
    std::vector<UnknownGameRule> unknown;
private:
    std::array<int64_t, static_cast<size_t>(GameRuleId::DayNightDuration)> m_values{};
};
