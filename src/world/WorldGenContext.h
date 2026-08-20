#pragma once

#include <cstdint>

// Stable seed-domain separation for world generation.  Adding random draws to
// one subsystem cannot perturb any other subsystem.
class WorldGenContext {
public:
    static constexpr uint32_t GENERATION_VERSION = 7;
    // Base chunk caches may be invalidated without changing the user-visible
    // generation version. v7 starts a fresh base-cache revision at 1.
    static constexpr uint32_t CHUNK_CACHE_VERSION =
        (GENERATION_VERSION << 16) | 1u;
    // Layout 4 adds independent macro-archetype, basin and landmark domains.
    static constexpr uint32_t SEED_LAYOUT_VERSION = 4;

    explicit WorldGenContext(uint64_t seed) : m_seed(seed) {}

    uint64_t seed() const { return m_seed; }

    uint64_t derive(uint64_t domain) const {
        uint64_t value = m_seed ^ domain ^
            (static_cast<uint64_t>(SEED_LAYOUT_VERSION) << 32);
        return mix(value);
    }

    int32_t noiseSeed(uint64_t domain) const {
        uint64_t value = derive(domain);
        return static_cast<int32_t>(value ^ (value >> 32));
    }

    static uint64_t hashPosition(uint64_t seed, int x, int y, int z) {
        uint64_t value = seed;
        value ^= mix(static_cast<uint64_t>(static_cast<int64_t>(x)));
        value ^= mix(static_cast<uint64_t>(static_cast<int64_t>(y)) +
                     0x9E3779B97F4A7C15ULL);
        value ^= mix(static_cast<uint64_t>(static_cast<int64_t>(z)) +
                     0xD1B54A32D192ED03ULL);
        return mix(value);
    }

    static uint64_t mix(uint64_t value) {
        value += 0x9E3779B97F4A7C15ULL;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

private:
    uint64_t m_seed;
};
