#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Config.h"

struct RenderEnvironment {
    float dayPhase = 0.0f;
    float daylight = 1.0f;
    float starIntensity = 0.0f;
    float ambientIntensity = 1.0f;
    float directIntensity = 1.0f;
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 moonDirection{0.0f, -1.0f, 0.0f};
    glm::vec3 lightDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 directColor{1.0f};
    glm::vec3 ambientColor{1.0f};
    glm::vec3 zenithColor{0.25f, 0.55f, 0.90f};
    glm::vec3 horizonColor{0.62f, 0.78f, 0.92f};
    glm::vec3 fogColor{0.62f, 0.78f, 0.92f};
    float rainIntensity = 0.0f;
    float thunderIntensity = 0.0f;
    float lightningFlash = 0.0f;
};

inline RenderEnvironment applyWeather(RenderEnvironment env, float rain,
                                      float thunder, float lightningFlash) {
    rain = std::clamp(rain, 0.0f, 1.0f);
    thunder = std::clamp(thunder, 0.0f, rain);
    lightningFlash = std::clamp(lightningFlash, 0.0f, 1.0f);
    env.rainIntensity = rain;
    env.thunderIntensity = thunder;
    env.lightningFlash = lightningFlash;
    env.starIntensity *= 1.0f - rain;
    env.directIntensity *= 1.0f - 0.42f * rain - 0.46f * thunder;
    env.ambientIntensity *= 1.0f - 0.16f * rain - 0.26f * thunder;
    const glm::vec3 rainSky(0.22f, 0.28f, 0.34f);
    const glm::vec3 stormSky(0.075f, 0.09f, 0.12f);
    env.zenithColor = glm::mix(env.zenithColor, rainSky, rain * 0.72f);
    env.zenithColor = glm::mix(env.zenithColor, stormSky, thunder * 0.82f);
    env.horizonColor = glm::mix(
        env.horizonColor, glm::vec3(0.34f, 0.38f, 0.42f), rain * 0.70f);
    env.horizonColor = glm::mix(
        env.horizonColor, glm::vec3(0.10f, 0.12f, 0.15f), thunder * 0.78f);
    env.fogColor = glm::mix(env.horizonColor, env.zenithColor, 0.28f);
    env.directColor = glm::mix(
        env.directColor, glm::vec3(0.62f, 0.69f, 0.78f), rain * 0.55f);
    env.ambientColor = glm::mix(
        env.ambientColor, glm::vec3(0.38f, 0.43f, 0.50f), rain * 0.58f);
    if (lightningFlash > 0.0f) {
        const glm::vec3 flash(0.72f, 0.80f, 1.0f);
        env.zenithColor += flash * lightningFlash * 0.65f;
        env.horizonColor += flash * lightningFlash * 0.48f;
        env.fogColor += flash * lightningFlash * 0.38f;
        env.ambientIntensity = std::max(
            env.ambientIntensity, 0.8f * lightningFlash);
    }
    return env;
}

inline glm::vec3 cloudColorForEnvironment(const RenderEnvironment& env) {
    constexpr glm::vec3 clearDayColor(0.92f, 0.94f, 0.96f);
    const float daylightShade = 0.30f + 0.70f *
        std::clamp(env.daylight, 0.0f, 1.0f);
    const float weatherShade = 1.0f -
        0.25f * std::clamp(env.rainIntensity, 0.0f, 1.0f) -
        0.30f * std::clamp(env.thunderIntensity, 0.0f, 1.0f);
    return clearDayColor * daylightShade * weatherShade;
}

class DayNightCycle {
public:
    static constexpr float MORNING_PHASE = 0.04f;
    static constexpr float STATIC_DAY_PHASE = 0.25f;

    void resetMorning() {
        m_phase = Config::DAY_CYCLE_MINUTES == 0
            ? STATIC_DAY_PHASE : MORNING_PHASE;
        m_manualTimeSet = false;
    }

    void setDay() { m_phase = 0.0f; m_manualTimeSet = true; }
    void setNight() { m_phase = 0.5f; m_manualTimeSet = true; }

    // Restore a persisted dimension phase while keeping the phase bounded.
    // Invalid values are treated as morning so corrupted metadata cannot
    // produce NaNs in the sky or lighting path.
    void setPhase(float phase) {
        if (!std::isfinite(phase) || phase < 0.0f || phase >= 1.0f)
            phase = MORNING_PHASE;
        m_phase = phase;
        m_manualTimeSet = false;
    }

    void update(float deltaSeconds, int cycleMinutes, bool advancing) {
        if (cycleMinutes == 0) {
            if (!m_manualTimeSet) m_phase = STATIC_DAY_PHASE;
            return;
        }
        if (advancing && deltaSeconds > 0.0f) {
            const float seconds = static_cast<float>(cycleMinutes) * 60.0f;
            m_phase += std::min(deltaSeconds, 0.1f) / seconds;
            m_phase -= std::floor(m_phase);
        }
    }

    float phase() const { return m_phase; }
    static bool isDayPhase(float phase) { return phase >= 0.0f && phase < 0.5f; }
    bool isDay() const { return isDayPhase(m_phase); }
    bool isNight() const { return !isDay(); }

    RenderEnvironment evaluate() const {
        constexpr float PI = 3.14159265358979323846f;
        const float angle = m_phase * 2.0f * PI;
        const glm::vec3 sun = glm::normalize(glm::vec3(
            std::cos(angle), std::sin(angle), 0.32f));
        const glm::vec3 moon = -sun;
        const float daylight = smoothstep(-0.12f, 0.18f, sun.y);
        const float moonlight = smoothstep(-0.08f, 0.20f, moon.y);
        const float twilight = std::clamp(
            1.0f - std::abs(sun.y) / 0.32f, 0.0f, 1.0f);

        RenderEnvironment env;
        env.dayPhase = m_phase;
        env.daylight = daylight;
        env.starIntensity = smoothstep(0.18f, 0.72f, 1.0f - daylight);
        env.sunDirection = sun;
        env.moonDirection = moon;
        env.lightDirection = daylight >= 0.12f ? sun : moon;
        env.directIntensity = daylight * 0.82f + moonlight * 0.16f;
        env.ambientIntensity = Config::NIGHT_AMBIENT_MIN +
            daylight * (0.76f - Config::NIGHT_AMBIENT_MIN);

        const glm::vec3 warmSun(1.00f, 0.91f, 0.72f);
        const glm::vec3 noonSun(1.00f, 0.98f, 0.91f);
        const glm::vec3 moonColor(0.42f, 0.53f, 0.78f);
        env.directColor = glm::mix(
            moonColor, glm::mix(noonSun, warmSun, twilight * 0.72f), daylight);
        env.ambientColor = glm::mix(
            glm::vec3(0.16f, 0.20f, 0.31f),
            glm::vec3(0.72f, 0.82f, 0.92f), daylight);

        env.zenithColor = glm::mix(
            glm::vec3(0.008f, 0.014f, 0.045f),
            glm::vec3(0.18f, 0.48f, 0.88f), daylight);
        env.horizonColor = glm::mix(
            glm::vec3(0.025f, 0.035f, 0.075f),
            glm::vec3(0.62f, 0.79f, 0.94f), daylight);
        env.horizonColor = glm::mix(
            env.horizonColor, glm::vec3(0.98f, 0.37f, 0.16f),
            twilight * (0.25f + 0.75f * daylight));
        env.fogColor = glm::mix(env.horizonColor, env.zenithColor, 0.18f);
        return env;
    }

private:
    float m_phase = MORNING_PHASE;
    bool m_manualTimeSet = false;

    static float smoothstep(float edge0, float edge1, float value) {
        float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
};
