#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Plugins {
struct GenerationConfig {
    std::string generator, revision, parameters="{}";
    std::vector<std::pair<std::string,std::string>> modifiers;
    bool operator==(const GenerationConfig& b) const {
        return generator==b.generator&&revision==b.revision&&parameters==b.parameters&&modifiers==b.modifiers;
    }
};

inline uint64_t generationFingerprint(const GenerationConfig& config) {
    uint64_t hash=1469598103934665603ULL;
    auto add=[&](const std::string& s){for(unsigned char c:s){hash^=c;hash*=1099511628211ULL;}hash^=255;hash*=1099511628211ULL;};
    add(config.generator);add(config.revision);add(config.parameters);
    for(const auto& m:config.modifiers){add(m.first);add(m.second);}
    return hash;
}
} // namespace Plugins
