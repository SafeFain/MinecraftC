#pragma once
#include "plugins/Json.h"
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace Plugins {
// Ordered registration stages, not filesystem traversal order. Keep the authoring
// tool's directory list in sync; runtime validators remain the source of truth.
inline const std::vector<std::string>& packageCategories() {
    static const std::vector<std::string> names{
        "materials","blocks","block_families","block_geometry","items","dimensions",
        "recipes","entities","generators","block_behaviors","item_behaviors",
        "loot_tables","structures","player_actions","environments","particles",
        "held_models","playlists","render_resources","render_passes"};
    return names;
}
struct PackageDefinition {
    std::string category, path, identity;
    Json value;
};
inline std::string packageIdentity(const Json& j,const std::string& category) {
    const char* field=category=="block_geometry"||category=="block_behaviors"?"block":
        category=="item_behaviors"||category=="held_models"?"item":"key";
    return j.at(field).text();
}
inline void packageSource(Json& j,const std::string& source) {
    j.source=source;
    for(auto& entry:j.object)packageSource(entry.second,source+"/"+entry.first);
    for(size_t i=0;i<j.array.size();++i)packageSource(j.array[i],source+"/"+std::to_string(i));
}
// Pipelines may precede their shaders lexically. Sort references before calling
// the existing immediate handle resolver, retaining stable path order otherwise.
inline void orderPackageRenderResources(std::vector<PackageDefinition>& definitions) {
    std::map<std::string,size_t> byKey;
    for(size_t i=0;i<definitions.size();++i)if(definitions[i].category=="render_resources") {
        if(!byKey.emplace(definitions[i].identity,i).second)
            throw std::runtime_error(definitions[i].path+": duplicate render resource "+definitions[i].identity);
    }
    std::vector<size_t> incoming(definitions.size());
    std::vector<std::set<size_t>> outgoing(definitions.size());
    std::set<std::pair<std::string,size_t>> ready;
    for(const auto& entry:byKey) {
        const auto i=entry.second;
        for(const char* field:{"vertex_shader","fragment_shader","texture"})if(definitions[i].value.has(field)) {
            const auto dependency=byKey.find(definitions[i].value.at(field).text());
            if(dependency!=byKey.end()&&outgoing[dependency->second].insert(i).second)++incoming[i];
        }
    }
    for(const auto& entry:byKey)if(incoming[entry.second]==0)ready.emplace(definitions[entry.second].path,entry.second);
    std::vector<PackageDefinition> sorted;
    while(!ready.empty()) {
        const auto i=ready.begin()->second;ready.erase(ready.begin());sorted.push_back(definitions[i]);
        for(const auto next:outgoing[i])if(--incoming[next]==0)ready.emplace(definitions[next].path,next);
    }
    if(sorted.size()!=byKey.size())for(const auto& entry:byKey)if(incoming[entry.second]) {
        const auto& d=definitions[entry.second];throw std::runtime_error(d.path+": cyclic render resource reference "+d.identity);
    }
    size_t resource=0;
    for(auto& definition:definitions)if(definition.category=="render_resources")definition=std::move(sorted[resource++]);
}
}
