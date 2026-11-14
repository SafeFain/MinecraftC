#pragma once
#include "game/NativeProjectileState.h"
#include <cmath>
#include <optional>

// Durable enchantment attribution is independent of transient XP kill credit.
namespace EnchantmentHitContext {
struct Value { std::string owner; float remaining = 0; };
inline std::optional<Value> read(const std::string& state) {
    if (state.find("minecraftc:enchantment_hit") == std::string::npos) return {};
    const auto json=nlohmann::json::parse(state,nullptr,false);
    if (!json.is_object() || !json.contains("minecraftc:enchantment_hit")) return {};
    const auto& hit=json["minecraftc:enchantment_hit"];
    if (!hit.is_object() || !hit.contains("owner") || !hit["owner"].is_string() ||
        !hit.contains("remaining") || !hit["remaining"].is_number()) throw std::runtime_error("Invalid enchantment hit context");
    Value value{hit["owner"].get<std::string>(),hit["remaining"].get<float>()};
    if (!NativeProjectileState::validOwner(value.owner) || !std::isfinite(value.remaining) ||
        value.remaining<=0 || value.remaining>5) throw std::runtime_error("Invalid enchantment hit context");
    return value;
}
inline std::string write(const std::string& state, const std::string& owner, float remaining) {
    auto json=nlohmann::json::parse(state,nullptr,false);
    if (!json.is_object()) return state;
    if (remaining<=0) json.erase("minecraftc:enchantment_hit");
    else {
        if (!NativeProjectileState::validOwner(owner) || !std::isfinite(remaining) || remaining>5)
            throw std::runtime_error("Invalid enchantment hit context");
        json["minecraftc:enchantment_hit"]={{"owner",owner},{"remaining",remaining}};
    }
    return json.dump();
}
inline void tick(std::string& state, float dt) {
    if (const auto hit=read(state)) state=write(state,hit->owner,hit->remaining-dt);
}
}
