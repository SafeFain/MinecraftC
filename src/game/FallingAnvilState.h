#pragma once
#include "world/Block.h"
#include <cmath>
#include <optional>
#include <nlohmann/json.hpp>

// Reserved native state in the existing durable entity envelope. A falling
// anvil uses the item renderer but is excluded from ordinary pickup physics.
struct FallingAnvilState {
    BlockId block = BlockId::ANVIL;
    double originY = 0;
    std::string encode() const { return nlohmann::json{{"falling_anvil",static_cast<uint16_t>(block)},{"origin_y",originY}}.dump(); }
    static std::optional<FallingAnvilState> decode(const std::string& state) {
        if (state.find("falling_anvil") == std::string::npos) return {};
        const auto json = nlohmann::json::parse(state,nullptr,false);
        if (!json.is_object() || !json.contains("falling_anvil") || !json["falling_anvil"].is_number_unsigned() || !json.contains("origin_y") || !json["origin_y"].is_number()) throw std::runtime_error("Invalid falling anvil state");
        const auto raw = json["falling_anvil"].get<uint64_t>(); const auto y = json["origin_y"].get<double>();
        if (raw > UINT16_MAX || !isAnvil(static_cast<BlockId>(raw)) || !std::isfinite(y) || y < -64 || y > 320) throw std::runtime_error("Invalid falling anvil position/block");
        return FallingAnvilState{static_cast<BlockId>(raw),y};
    }
};
