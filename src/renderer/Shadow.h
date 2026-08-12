#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class ShadowQuality : uint8_t { Off, Low, Medium, High };

struct ShadowConfig {
    int cascadeCount = 0;
    int resolution = 0;
    float distance = 0.0f;
};

inline ShadowConfig shadowConfig(ShadowQuality quality) {
    switch (quality) {
        case ShadowQuality::Low: return {1, 1024, 64.0f};
        case ShadowQuality::Medium: return {3, 1024, 192.0f};
        case ShadowQuality::High: return {4, 2048, 256.0f};
        default: return {};
    }
}

struct ShadowCascades {
    std::array<glm::mat4, 4> lightViewProjection{glm::mat4(1.0f), glm::mat4(1.0f),
                                                glm::mat4(1.0f), glm::mat4(1.0f)};
    glm::vec4 splits{0.0f};
    int count = 0;
    int resolution = 0;
};

inline bool shadowIntersectsAabb(const glm::mat4& matrix, const glm::vec3& minimum,
                                 const glm::vec3& maximum, bool zeroToOneDepth) {
    std::array<glm::vec4,8> clip{};
    int index=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        clip[index++]=matrix*glm::vec4(x?maximum.x:minimum.x,
            y?maximum.y:minimum.y,z?maximum.z:minimum.z,1.0f);
    auto outside=[&](auto predicate){return std::all_of(clip.begin(),clip.end(),predicate);};
    if(outside([](const glm::vec4&p){return p.x < -p.w;})||
       outside([](const glm::vec4&p){return p.x >  p.w;})||
       outside([](const glm::vec4&p){return p.y < -p.w;})||
       outside([](const glm::vec4&p){return p.y >  p.w;})||
       outside([](const glm::vec4&p){return p.z >  p.w;}))return false;
    return zeroToOneDepth
        ? !outside([](const glm::vec4&p){return p.z < 0.0f;})
        : !outside([](const glm::vec4&p){return p.z < -p.w;});
}

inline ShadowCascades buildShadowCascades(ShadowQuality quality,
        const glm::mat4& inverseViewProjection, const glm::mat4& view,
        const glm::vec3& lightDirection, float cameraNear, float cameraFar) {
    const ShadowConfig config = shadowConfig(quality);
    ShadowCascades result;
    result.count = config.cascadeCount;
    result.resolution = config.resolution;
    if (result.count == 0) return result;

    cameraFar = std::min(cameraFar, config.distance);
    std::array<glm::vec3, 8> fullCorners{};
    int corner = 0;
    for (int z = 0; z < 2; ++z) for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        glm::vec4 p = inverseViewProjection * glm::vec4(
            x ? 1.0f : -1.0f, y ? 1.0f : -1.0f, z ? 1.0f : -1.0f, 1.0f);
        fullCorners[corner++] = glm::vec3(p) / p.w;
    }
    float sourceFar = cameraFar;
    for (int i = 4; i < 8; ++i)
        sourceFar = std::max(sourceFar, -(view * glm::vec4(fullCorners[i], 1.0f)).z);
    float previous = cameraNear;
    for (int cascade = 0; cascade < result.count; ++cascade) {
        const float ratio = static_cast<float>(cascade + 1) / result.count;
        const float logarithmic = cameraNear * std::pow(cameraFar / cameraNear, ratio);
        const float uniform = cameraNear + (cameraFar - cameraNear) * ratio;
        const float split = glm::mix(uniform, logarithmic, 0.65f);
        result.splits[cascade] = split;
        const float nearRatio = (previous - cameraNear) / (sourceFar - cameraNear);
        const float farRatio = (split - cameraNear) / (sourceFar - cameraNear);
        std::array<glm::vec3, 8> corners{};
        for (int i = 0; i < 4; ++i) {
            const glm::vec3 ray = fullCorners[i + 4] - fullCorners[i];
            corners[i] = fullCorners[i] + ray * nearRatio;
            corners[i + 4] = fullCorners[i] + ray * farRatio;
        }
        glm::vec3 center(0.0f);
        for (const glm::vec3& p : corners) center += p;
        center /= 8.0f;
        float radius = 0.0f;
        for (const glm::vec3& p : corners) radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;
        const glm::vec3 direction = glm::normalize(lightDirection);
        const glm::vec3 up = std::abs(direction.y) > 0.98f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
        glm::mat4 lightView = glm::lookAt(center + direction * (radius + 384.0f), center, up);
        glm::vec4 snapped = lightView * glm::vec4(center, 1.0f);
        const float texel = (2.0f * radius) / static_cast<float>(result.resolution);
        snapped.x = std::floor(snapped.x / texel) * texel;
        snapped.y = std::floor(snapped.y / texel) * texel;
        const glm::vec4 current = lightView * glm::vec4(center, 1.0f);
        lightView = glm::translate(glm::mat4(1.0f), glm::vec3(snapped - current)) * lightView;
        result.lightViewProjection[cascade] = glm::ortho(
            -radius, radius, -radius, radius, 0.1f, radius * 2.0f + 768.0f) * lightView;
        previous = split;
    }
    return result;
}
