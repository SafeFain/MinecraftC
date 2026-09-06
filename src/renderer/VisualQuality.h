#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "renderer/RenderEnvironment.h"

enum class VisualQuality : uint8_t { Low, Medium, High, Ultra };

struct EnhancedVisualSettings {
    bool enabled = false;
    bool custom = false;
    bool bloom = true;
    bool ambientOcclusion = true;
    bool lightShafts = true;
    bool reflections = true;
    bool atmosphere = true;
    bool materialMotion = true;
    bool ambientParticles = true;
    uint8_t bloomStrength = 100;
    uint8_t ambientOcclusionStrength = 100;
    uint8_t lightShaftStrength = 100;
    uint8_t reflectionStrength = 100;
    uint8_t atmosphereStrength = 100;
    uint8_t materialMotionStrength = 100;
    uint8_t ambientParticleStrength = 100;
};

struct VisualQualityConfig {
    int sceneSamples = 1;
    bool materialNormals = false;
    int aoResolutionDivisor = 0;
    int aoDirections = 0;
    int aoSteps = 0;
    int bloomLevels = 0;
    int cloudShadowSamples = 0;
    bool voxelClouds = false;
    bool cirrusClouds = false;
    float normalStrength = 0.0f;
};

struct EnhancedVisualConfig {
    int bloomLevels = 0;
    float atmosphereStrength = 0.0f;
    float materialMotionStrength = 0.0f;
    float ambientParticlesPerSecond = 0.0f;
    int screenEffectDivisor = 0;
    int aoDirections = 0;
    int aoSteps = 0;
    int lightShaftSamples = 0;
    int reflectionSteps = 0;
    int reflectionRefineSteps = 0;
    float reflectionDistance = 0.0f;
    int shadowBlockerSamples = 0;
    int shadowFilterSamples = 0;
    float bloomStrength = 1.0f;
    float ambientOcclusionStrength = 1.0f;
    float lightShaftStrength = 1.0f;
    float reflectionStrength = 1.0f;

    bool usesSurfaceData() const { return screenEffectDivisor > 0; }
    bool usesScreenSpaceReflections() const { return reflectionSteps > 0; }
};

inline EnhancedVisualConfig enhancedVisualConfig(VisualQuality quality,
                                                 bool enabled) {
    if (!enabled) return {};
    switch (quality) {
        case VisualQuality::Low:
            return {0, 0.25f, 0.25f, 0.0f,
                    0, 0, 0, 0, 0, 0, 0.0f, 0, 1};
        case VisualQuality::Medium:
            return {2, 0.50f, 0.60f, 4.0f,
                    4, 4, 2, 8, 0, 0, 0.0f, 0, 4};
        case VisualQuality::High:
            return {3, 0.75f, 0.85f, 8.0f,
                    4, 6, 3, 12, 12, 2, 64.0f, 4, 8};
        case VisualQuality::Ultra:
            return {4, 1.00f, 1.00f, 12.0f,
                    2, 8, 4, 16, 24, 4, 96.0f, 6, 12};
    }
    return {};
}

inline EnhancedVisualConfig enhancedVisualConfig(
    VisualQuality quality, const EnhancedVisualSettings& settings) {
    EnhancedVisualConfig result = enhancedVisualConfig(quality, settings.enabled);
    if (!settings.enabled || !settings.custom) return result;
    const auto amount = [](uint8_t value) {
        return std::clamp(static_cast<float>(value) / 100.0f, 0.0f, 1.0f);
    };
    if (!settings.bloom || settings.bloomStrength == 0) result.bloomLevels = 0;
    if (!settings.ambientOcclusion || settings.ambientOcclusionStrength == 0) {
        result.aoDirections = 0;
        result.aoSteps = 0;
    }
    if (!settings.lightShafts || settings.lightShaftStrength == 0)
        result.lightShaftSamples = 0;
    if (!settings.reflections || settings.reflectionStrength == 0) {
        result.reflectionSteps = 0;
        result.reflectionRefineSteps = 0;
        result.reflectionDistance = 0.0f;
    }
    if (!settings.atmosphere || settings.atmosphereStrength == 0)
        result.atmosphereStrength = 0.0f;
    if (!settings.materialMotion || settings.materialMotionStrength == 0)
        result.materialMotionStrength = 0.0f;
    if (!settings.ambientParticles || settings.ambientParticleStrength == 0)
        result.ambientParticlesPerSecond = 0.0f;
    result.bloomStrength = settings.bloom ? amount(settings.bloomStrength) : 0.0f;
    result.ambientOcclusionStrength = settings.ambientOcclusion
        ? amount(settings.ambientOcclusionStrength) : 0.0f;
    result.lightShaftStrength = settings.lightShafts
        ? amount(settings.lightShaftStrength) : 0.0f;
    result.reflectionStrength = settings.reflections
        ? amount(settings.reflectionStrength) : 0.0f;
    result.atmosphereStrength *= amount(settings.atmosphereStrength);
    result.materialMotionStrength *= amount(settings.materialMotionStrength);
    result.ambientParticlesPerSecond *= amount(settings.ambientParticleStrength);
    if (result.aoDirections == 0 && result.lightShaftSamples == 0 &&
        result.reflectionSteps == 0)
        result.screenEffectDivisor = 0;
    return result;
}

inline VisualQualityConfig visualQualityConfig(VisualQuality quality) {
    switch (quality) {
        case VisualQuality::Low:
            return {1, false, 0, 0, 0, 0, 0, false, false, 0.0f};
        case VisualQuality::Medium:
            return {2, true, 4, 4, 2, 3, 1, true, false, 0.65f};
        case VisualQuality::High:
            return {4, true, 2, 6, 3, 5, 4, true, true, 0.85f};
        case VisualQuality::Ultra:
            return {4, true, 2, 8, 4, 6, 4, true, true, 1.0f};
    }
    return visualQualityConfig(VisualQuality::Medium);
}

struct PostProcessState {
    RenderEnvironment environment{};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    float exposure = 1.0f;
    float underwater = 0.0f;
    float hurt = 0.0f;
};

class VisualExposure {
public:
    void reset(float exposure = 1.0f) {
        m_exposure = std::clamp(exposure, 0.75f, 1.65f);
        m_initialized = false;
    }

    float update(float skyLight, float blockLight,
                 const RenderEnvironment& environment, float dt) {
        const float localLight = std::max(
            std::clamp(skyLight, 0.0f, 1.0f) *
                (0.28f + environment.daylight * 0.72f),
            std::clamp(blockLight, 0.0f, 1.0f) * 0.82f);
        const float darkness = 1.0f - std::clamp(localLight, 0.0f, 1.0f);
        float target = 0.95f + darkness * darkness * 0.65f;
        target -= environment.lightningFlash * 0.18f;
        target = std::clamp(target, 0.75f, 1.65f);
        if (!m_initialized) {
            m_exposure = target;
            m_initialized = true;
            return m_exposure;
        }
        dt = std::clamp(dt, 0.0f, 0.1f);
        // Bright scenes need a quick response to avoid a white flash. Dark
        // adaptation stays deliberately slower so cave entrances feel deep.
        const float seconds = target < m_exposure ? 0.25f : 1.5f;
        const float blend = 1.0f - std::exp(-dt / seconds);
        m_exposure += (target - m_exposure) * blend;
        return m_exposure;
    }

    float value() const { return m_exposure; }

private:
    float m_exposure = 1.0f;
    bool m_initialized = false;
};
