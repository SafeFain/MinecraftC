#pragma once

#include <cstdint>

enum class Biome : uint8_t;

enum class PrecipitationType : uint8_t {
    None,
    Rain,
    Snow
};

PrecipitationType precipitationFor(Biome biome, int worldY);

enum class WeatherType : uint8_t {
    Clear = 0,
    Rain = 1,
    Thunder = 2
};

struct WeatherSaveState {
    bool raining = false;
    bool thundering = false;
    uint32_t rainTicks = 0;
    uint32_t thunderTicks = 0;
    uint64_t sequence = 0;
};

class WeatherSystem {
public:
    static constexpr uint32_t CLEAR_MIN_TICKS = 12000;
    static constexpr uint32_t CLEAR_MAX_TICKS = 180000;
    static constexpr uint32_t RAIN_MIN_TICKS = 12000;
    static constexpr uint32_t RAIN_MAX_TICKS = 24000;
    static constexpr uint32_t THUNDER_MIN_TICKS = 3600;
    static constexpr uint32_t THUNDER_MAX_TICKS = 15600;

    void reset(uint64_t worldSeed, const WeatherSaveState& state = {});
    void tick();
    void setWeather(WeatherType type);

    WeatherType type() const;
    bool raining() const { return m_state.raining; }
    bool thundering() const { return m_state.raining && m_state.thundering; }
    float rainGradient() const { return m_rainGradient; }
    float thunderGradient() const { return m_thunderGradient; }
    const WeatherSaveState& saveState() const { return m_state; }

private:
    uint64_t m_seed = 0;
    WeatherSaveState m_state;
    float m_rainGradient = 0.0f;
    float m_thunderGradient = 0.0f;

    uint32_t randomDuration(uint32_t minimum, uint32_t maximum);
    uint32_t nextRainDuration() {
        return m_state.raining
            ? randomDuration(RAIN_MIN_TICKS, RAIN_MAX_TICKS)
            : randomDuration(CLEAR_MIN_TICKS, CLEAR_MAX_TICKS);
    }
    uint32_t nextThunderDuration() {
        return m_state.thundering
            ? randomDuration(THUNDER_MIN_TICKS, THUNDER_MAX_TICKS)
            : randomDuration(CLEAR_MIN_TICKS, CLEAR_MAX_TICKS);
    }
};
