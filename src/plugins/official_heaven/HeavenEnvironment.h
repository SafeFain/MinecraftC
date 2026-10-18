#pragma once
#include "renderer/RenderEnvironment.h"
#include "minecraftc/extensions.h"
namespace OfficialHeaven {
inline RenderEnvironment environment(RenderEnvironment env) {
    env.skyStyle = RenderSkyStyle::Layered;
    env.skyFlags=MC_SKY_NO_ATMOSPHERE|MC_SKY_NO_LOW_CLOUDS|MC_SKY_NO_VOXEL_CLOUDS|MC_SKY_NO_LIGHT_SHAFTS;
    env.gradeColor={1.018f,1.008f,.982f};env.gradeStrength=.36f;
    const float daylight = std::clamp(env.daylight, 0.0f, 1.0f);
    const float twilight = std::clamp(
        1.0f - std::abs(env.sunDirection.y) / 0.34f, 0.0f, 1.0f);
    const glm::vec3 dayZenith(0.18f, 0.62f, 0.96f);
    const glm::vec3 dayHorizon(0.98f, 0.88f, 0.60f);
    const glm::vec3 nightZenith(0.035f, 0.055f, 0.18f);
    const glm::vec3 nightHorizon(0.22f, 0.16f, 0.42f);
    env.zenithColor = glm::mix(nightZenith, dayZenith, daylight);
    env.horizonColor = glm::mix(nightHorizon, dayHorizon, daylight);
    env.horizonColor = glm::mix(
        env.horizonColor, glm::vec3(1.0f, 0.50f, 0.22f),
        twilight * (0.30f + 0.70f * daylight));
    env.fogColor = glm::mix(env.horizonColor, env.zenithColor, 0.22f);
    env.starIntensity = std::min(1.0f, env.starIntensity * 1.28f +
        (1.0f - daylight) * 0.08f);
    env.directColor = glm::mix(
        glm::vec3(0.50f, 0.60f, 1.00f),
        glm::vec3(1.00f, 0.94f, 0.76f), daylight);
    env.ambientColor = glm::mix(
        glm::vec3(0.22f, 0.25f, 0.48f),
        glm::vec3(0.70f, 0.88f, 0.98f), daylight);
    env.directIntensity *= 0.90f;
    env.ambientIntensity = std::min(1.0f, env.ambientIntensity * 1.08f);
    return env;
}

MC_EnvironmentProfileV2 environmentDefinition();
}
