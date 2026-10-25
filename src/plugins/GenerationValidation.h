#pragma once

#include <set>
#include <stdexcept>
#include <string>

#include "plugins/ContentQueries.h"
#include "plugins/Json.h"

// Generation configurations are JSON parameters owned by content packs. Only
// save, network and plugin validation need the JSON dependency, so it lives
// here instead of the read-only content query header.
namespace Plugins {
inline void validateGeneration(const GenerationConfig& config,uint32_t dimension) {
    if(!validDimension(dimension)||config.parameters.size()>4096||config.modifiers.size()>64)
        throw std::runtime_error("Invalid generator configuration limits");
    if(dimension!=0&&dimension!=2&&config.generator.empty())throw std::runtime_error("Plugin dimension requires a generator");
    if(Json::parse(config.parameters).type!=Json::Type::Object)
        throw std::runtime_error("Generator parameters must be a JSON object");
    if(config.generator.empty()&&!config.revision.empty())throw std::runtime_error("Builtin generator has no plugin revision");
    std::set<std::string> selected;
    for(const auto& m:config.modifiers)if(!selected.insert(m.first).second)throw std::runtime_error("Duplicate generator modifier");
    if(!config.generator.empty()) {
        auto g=content().extensions.generators.find(config.generator);
        if(g==content().extensions.generators.end()||g->second.dimension!=dimension||g->second.revision!=config.revision)
            throw std::runtime_error("Missing/incompatible generator: "+config.generator);
    }
    for(const auto& m:config.modifiers) {
        auto it=content().extensions.modifiers.find(m.first);
        if(it==content().extensions.modifiers.end()||it->second.dimension!=dimension||it->second.revision!=m.second)
            throw std::runtime_error("Missing/incompatible modifier: "+m.first);
    }
}
} // namespace Plugins
