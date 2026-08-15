#pragma once

#include <glm/glm.hpp>

class ChunkStore;

// Owns the chunk lighting system: full relight of newly generated chunks
// (vertical sky/emission seeding + flood fill across chunk borders) and
// incremental removal/addition relight around a single edited cell. All
// passes run under the ChunkStore lock via withUnique.
class WorldLighting {
public:
    explicit WorldLighting(ChunkStore& chunks) : m_chunks(chunks) {}

    // Full relight of chunks whose lighting has not been initialized.
    // No-op unless dirty().
    void rebuild();

    // Incremental removal/addition relight around one edited cell.
    // No-op while a full rebuild is pending.
    void updateLightingAt(const glm::ivec3& position);

    bool dirty() const { return m_lightDirty; }
    void markDirty() { m_lightDirty = true; }

    // Drop lighting state (seed reset / teardown).
    void reset() {
        m_lightDirty = true;
        m_lightHasSources = false;
    }

private:
    ChunkStore& m_chunks;
    bool m_lightDirty = true;
    bool m_lightHasSources = false;
};
