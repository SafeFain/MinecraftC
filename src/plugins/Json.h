#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Plugins {
// Manifests use the project's existing JSON library, with bounded depth/size
// and duplicate-key rejection so declarations have a single interpretation.
struct Json {
    enum class Type { Null, Boolean, Number, String, Array, Object } type=Type::Null;
    bool boolean=false;
    double number=0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string,Json> object;
    const Json& at(const std::string& key) const {
        if(type!=Type::Object||!object.count(key))throw std::runtime_error("Missing JSON field: "+key);
        return object.at(key);
    }
    bool has(const std::string& key) const {return object.count(key)!=0;}
    std::string text() const {
        if(type!=Type::String)throw std::runtime_error("Expected JSON string");
        return string;
    }
    std::string text(const std::string& key,const std::string& fallback={}) const {return has(key)?at(key).text():fallback;}
    double numeric(const std::string& key,double fallback) const {
        if(!has(key))return fallback;
        if(at(key).type!=Type::Number)throw std::runtime_error("Expected JSON number: "+key);
        return at(key).number;
    }
    bool flag(const std::string& key,bool fallback) const {
        if(!has(key))return fallback;
        if(at(key).type!=Type::Boolean)throw std::runtime_error("Expected JSON boolean: "+key);
        return at(key).boolean;
    }
    static Json convert(const nlohmann::json& input) {
        Json result;
        if(input.is_null())return result;
        if(input.is_boolean()){result.type=Type::Boolean;result.boolean=input.get<bool>();}
        else if(input.is_number()){result.type=Type::Number;result.number=input.get<double>();if(!std::isfinite(result.number))throw std::runtime_error("Non-finite JSON number");}
        else if(input.is_string()){result.type=Type::String;result.string=input.get<std::string>();}
        else if(input.is_array()){result.type=Type::Array;for(const auto& entry:input)result.array.push_back(convert(entry));}
        else {result.type=Type::Object;for(auto it=input.begin();it!=input.end();++it)result.object.emplace(it.key(),convert(it.value()));}
        return result;
    }
    static Json parse(std::string_view input) {
        if(input.size()>16*1024*1024)throw std::runtime_error("JSON exceeds size limit");
        std::vector<std::set<std::string>> keys;
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value) {
            if(depth>64)throw std::runtime_error("JSON exceeds nesting limit");
            if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();
            if(event==nlohmann::json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)throw std::runtime_error("Duplicate JSON key");
            if(event==nlohmann::json::parse_event_t::object_end)keys.pop_back();
            return true;
        };
        return convert(nlohmann::json::parse(input.begin(),input.end(),callback));
    }
};
}
