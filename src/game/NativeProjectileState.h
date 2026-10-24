#pragma once
#include <algorithm>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>

// The existing shared entity-state envelope carries stable native ownership.
// No runtime room/entity ID, plugin provider or ABI callback is persisted.
namespace NativeProjectileState {
inline bool validOwner(const std::string& key) {
    return key=="local" || (key.size()==32 && std::all_of(key.begin(),key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}));
}
inline std::string encode(const std::string& key) {
    if(!validOwner(key))throw std::runtime_error("Invalid native projectile owner");
    return nlohmann::json{{"native_owner",key}}.dump();
}
inline std::string owner(const std::string& state) {
    const auto json=nlohmann::json::parse(state,nullptr,false);
    if(json.is_discarded()||!json.is_object()||!json.contains("native_owner")||!json["native_owner"].is_string())return {};
    const auto key=json["native_owner"].get<std::string>();return validOwner(key)?key:std::string{};
}
}
