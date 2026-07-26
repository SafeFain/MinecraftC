#include "game/Weather.h"
#include "world/BiomeMap.h"
#include "Config.h"

#include <algorithm>

namespace {
uint64_t mix64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
}

PrecipitationType precipitationFor(Biome biome, int worldY) {
    if (biome == Biome::DESERT || biome == Biome::SAVANNA ||
        biome == Biome::BADLANDS) return PrecipitationType::None;
    if (biome == Biome::SNOW_TUNDRA) return PrecipitationType::Snow;
    const int snowLine = getBiomeProps(biome).snowLine;
    if (snowLine < Config::SNOW_LINE_DISABLED && worldY >= snowLine)
        return PrecipitationType::Snow;
    return PrecipitationType::Rain;
}

void WeatherSystem::reset(uint64_t worldSeed, const WeatherSaveState& state) {
    m_seed = worldSeed;
    m_state = state;
    if (m_state.rainTicks == 0) m_state.rainTicks = nextRainDuration();
    if (m_state.thunderTicks == 0) m_state.thunderTicks = nextThunderDuration();
    m_rainGradient = m_state.raining ? 1.0f : 0.0f;
    m_thunderGradient = thundering() ? 1.0f : 0.0f;
}

uint32_t WeatherSystem::randomDuration(uint32_t minimum, uint32_t maximum) {
    const uint64_t random = mix64(m_seed ^
        (0x9e3779b97f4a7c15ULL * (++m_state.sequence)));
    return minimum + static_cast<uint32_t>(random % (maximum - minimum + 1));
}

void WeatherSystem::tick() {
    if (m_state.rainTicks > 0 && --m_state.rainTicks == 0) {
        m_state.raining = !m_state.raining;
        m_state.rainTicks = nextRainDuration();
    }
    if (m_state.thunderTicks > 0 && --m_state.thunderTicks == 0) {
        m_state.thundering = !m_state.thundering;
        m_state.thunderTicks = nextThunderDuration();
    }

    const float rainTarget = m_state.raining ? 1.0f : 0.0f;
    const float thunderTarget = thundering() ? 1.0f : 0.0f;
    m_rainGradient += std::clamp(rainTarget - m_rainGradient, -0.01f, 0.01f);
    m_thunderGradient += std::clamp(
        thunderTarget - m_thunderGradient, -0.01f, 0.01f);
}

void WeatherSystem::setWeather(WeatherType type) {
    m_state.raining = type != WeatherType::Clear;
    m_state.thundering = type == WeatherType::Thunder;
    m_state.rainTicks = nextRainDuration();
    m_state.thunderTicks = nextThunderDuration();
}

WeatherType WeatherSystem::type() const {
    if (thundering()) return WeatherType::Thunder;
    return m_state.raining ? WeatherType::Rain : WeatherType::Clear;
}
