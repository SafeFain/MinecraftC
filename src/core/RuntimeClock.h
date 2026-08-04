#pragma once

#include <SDL3/SDL_timer.h>

#include <cstdint>
#include <limits>

class RuntimeClock {
public:
    using Tick = uint64_t;
    using Source = Tick (*)();

    explicit RuntimeClock(Source source = &SDL_GetTicksNS) : m_source(source) {}

    Tick now() const { return m_source ? m_source() : 0; }

    static Tick elapsed(Tick start, Tick end) { return end >= start ? end - start : 0; }
    static double seconds(Tick duration) { return static_cast<double>(duration) / 1'000'000'000.0; }
    static uint64_t milliseconds(Tick duration) { return duration / 1'000'000; }
    static Tick fromSeconds(double value) {
        if (!(value > 0.0)) return 0;
        constexpr double limit = static_cast<double>(std::numeric_limits<Tick>::max());
        const double nanos = value * 1'000'000'000.0;
        return nanos >= limit ? std::numeric_limits<Tick>::max() : static_cast<Tick>(nanos);
    }

private:
    Source m_source;
};
