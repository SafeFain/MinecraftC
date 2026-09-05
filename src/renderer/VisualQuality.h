#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "renderer/RenderEnvironment.h"

enum class VisualQuality : uint8_t { Low, Medium, High, Ultra };

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
};

inline EnhancedVisualConfig enhancedVisualConfig(VisualQuality quality,
                                                 bool enabled) {
    if (!enabled) return {};
    switch (quality) {
        case VisualQuality::Low: return {0, 0.25f, 0.25f, 0.0f};
        case VisualQuality::Medium: return {2, 0.50f, 0.60f, 4.0f};
        case VisualQuality::High: return {3, 0.75f, 0.85f, 8.0f};
        case VisualQuality::Ultra: return {4, 1.00f, 1.00f, 12.0f};
    }
    return {};
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
