#include "plugins/AtlasPublication.h"
#include "plugins/AtlasQueries.h"

#include <mutex>
#include <shared_mutex>

namespace Plugins {
namespace {
std::shared_ptr<const AtlasBindings> activeBindings;
std::shared_mutex bindingsMutex;

template <typename Map, typename Key>
uint16_t tileOrFallback(const Map& tiles, const Key& key) {
    const auto it = tiles.find(key);
    return it == tiles.end() ? 0 : it->second;
}
}

void publishAtlasBindings(std::shared_ptr<const AtlasBindings> bindings) {
    const std::unique_lock lock(bindingsMutex);
    activeBindings.swap(bindings);
}

std::optional<uint16_t> blockFaceTile(uint16_t block, size_t face) {
    const std::shared_lock lock(bindingsMutex);
    if (!activeBindings) return std::nullopt;
    const auto it = activeBindings->blocks.find(block);
    if (it == activeBindings->blocks.end()) return std::nullopt;
    return it->second.at(face);
}

uint16_t itemTile(uint16_t item) {
    const std::shared_lock lock(bindingsMutex);
    return activeBindings ? tileOrFallback(activeBindings->items, item) : 0;
}

uint16_t registeredMaterialTile(const std::string& key) {
    const std::shared_lock lock(bindingsMutex);
    return activeBindings ? tileOrFallback(activeBindings->materials, key) : 0;
}
}
