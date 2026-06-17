#pragma once

#include "debug/Log.h"
#include <chrono>
#include <string>
#include <algorithm>
#include <cfloat>

namespace Debug {

// ── RAII scoped timer ──────────────────────────────────────────────────
// Logs elapsed time on destruction. If elapsed exceeds warnThresholdMs,
// logs at Warning level instead of the specified level.
//
// Usage:
//   { ScopedTimer t("MeshBuild", LogLevel::Debug, 16.0f); ... }
//   MC_PROFILE_SCOPE("ChunkRender");
//   MC_PROFILE_FUNC();

class ScopedTimer {
public:
    using Clock = std::chrono::high_resolution_clock;

    explicit ScopedTimer(const char* name,
                         LogLevel level = LogLevel::Trace,
                         float warnThresholdMs = 1000.0f)
        : m_name(name)
        , m_level(level)
        , m_warnThresholdMs(warnThresholdMs)
        , m_start(Clock::now())
    {}

    ~ScopedTimer() {
        if (!m_stopped) stop();
    }

    float stop() {
        m_stopped = true;
        float ms = elapsedMs();

        LogLevel outLevel = m_level;
        if (ms >= m_warnThresholdMs && m_level < LogLevel::Warn) {
            outLevel = LogLevel::Warn;
        }

        Log::log(outLevel, __FILE__, 0,
                 std::string("[PROFILE] ") + m_name + ": " +
                 std::to_string(ms) + " ms" +
                 (ms >= m_warnThresholdMs ? " (SLOW)" : ""));

        return ms;
    }

    float elapsedMs() const {
        auto now = Clock::now();
        return std::chrono::duration<float, std::milli>(now - m_start).count();
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;

private:
    const char*           m_name;
    LogLevel              m_level;
    float                 m_warnThresholdMs;
    Clock::time_point     m_start;
    bool                  m_stopped = false;
};

// ── Per-frame timing statistics ────────────────────────────────────────
// Tracks frame time min/max/average. Logs a periodic summary every
// `logIntervalFrames` frames.
//
// Usage:
//   FrameTimer m_frameTimer;
//   In main loop:
//     m_frameTimer.beginFrame();
//     ... game logic + rendering ...
//     m_frameTimer.endFrame();

class FrameTimer {
public:
    using Clock = std::chrono::high_resolution_clock;

    explicit FrameTimer(int logIntervalFrames = 300)
        : m_logIntervalFrames(logIntervalFrames)
    {}

    void beginFrame() {
        m_frameStart = Clock::now();
    }

    void endFrame() {
        float ms = std::chrono::duration<float, std::milli>(
            Clock::now() - m_frameStart).count();

        ++m_frameCount;
        m_totalMs += ms;
        m_minMs = std::min(m_minMs, ms);
        m_maxMs = std::max(m_maxMs, ms);

        if (m_frameCount % m_logIntervalFrames == 0) {
            float avg = m_totalMs / static_cast<float>(m_frameCount);
            LOG_INFO("[FRAME] avg=" << avg << " ms  "
                     "min=" << m_minMs << " ms  "
                     "max=" << m_maxMs << " ms  "
                     "over " << m_frameCount << " frames  "
                     "(" << (1000.0f / avg) << " FPS)");
            reset();
        }
    }

    float averageMs() const {
        return m_frameCount > 0 ? m_totalMs / static_cast<float>(m_frameCount) : 0.0f;
    }
    float minMs()     const { return m_minMs; }
    float maxMs()     const { return m_maxMs; }
    size_t frameCount() const { return m_frameCount; }

    void reset() {
        m_totalMs = 0.0f;
        m_minMs   = FLT_MAX;
        m_maxMs   = 0.0f;
        m_frameCount = 0;
    }

private:
    Clock::time_point m_frameStart;
    float m_totalMs = 0.0f;
    float m_minMs   = FLT_MAX;
    float m_maxMs   = 0.0f;
    size_t m_frameCount = 0;
    int m_logIntervalFrames;
};

} // namespace Debug

// ── Convenience macros ─────────────────────────────────────────────────

#define MC_PROFILE_SCOPE(name) \
    Debug::ScopedTimer _mc_prof_##__LINE__(name)

#define MC_PROFILE_FUNC() \
    Debug::ScopedTimer _mc_prof_##__LINE__(__func__)
