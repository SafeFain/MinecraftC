#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

constexpr float PROJECTILE_GRAVITY = 9.8f;
constexpr float BOW_FULL_CHARGE_SECONDS = 1.0f;
constexpr float BOW_MIN_SPEED = 6.0f;
constexpr float BOW_MAX_SPEED = 24.0f;
constexpr float BOW_MIN_DAMAGE = 2.0f;
constexpr float BOW_MAX_DAMAGE = 6.0f;

struct ProjectileLaunch {
    glm::dvec3 origin{0.0};
    glm::vec3 velocity{0.0f};
    float damage = 0.0f;
};

inline float bowChargeProgress(float seconds) {
    return std::clamp(seconds / BOW_FULL_CHARGE_SECONDS, 0.0f, 1.0f);
}

inline float bowChargeStrength(float seconds) {
    const float progress = bowChargeProgress(seconds);
    return std::clamp((progress * progress + 2.0f * progress) / 3.0f,
                      0.0f, 1.0f);
}

inline float bowLaunchSpeed(float strength) {
    return glm::mix(BOW_MIN_SPEED, BOW_MAX_SPEED,
                    std::clamp(strength, 0.0f, 1.0f));
}

inline float bowLaunchDamage(float strength) {
    return glm::mix(BOW_MIN_DAMAGE, BOW_MAX_DAMAGE,
                    std::clamp(strength, 0.0f, 1.0f));
}

inline glm::vec3 projectileLaunchVelocity(const glm::vec3& direction,
                                          float speed,
                                          const glm::vec3& inheritedVelocity) {
    const float length = glm::length(direction);
    if (length <= 0.00001f || speed <= 0.0f) return inheritedVelocity;
    return direction / length * speed + inheritedVelocity;
}

inline glm::dvec3 projectilePosition(const glm::dvec3& origin,
                                     const glm::vec3& initialVelocity,
                                     double seconds) {
    const double time = std::max(0.0, seconds);
    return origin + glm::dvec3(initialVelocity) * time +
        glm::dvec3(0.0, -0.5 * PROJECTILE_GRAVITY * time * time, 0.0);
}

inline glm::vec3 projectileVelocityAfter(const glm::vec3& initialVelocity,
                                         float seconds) {
    glm::vec3 result = initialVelocity;
    result.y -= PROJECTILE_GRAVITY * std::max(0.0f, seconds);
    return result;
}

// Earliest contact of the analytic parabola with a closed, stationary AABB.
// Horizontal slabs restrict the time interval; vertical boundary roots retain
// both the ascending and descending parts, including contact at the apex.
inline std::optional<double> projectileAabbHit(
    const glm::dvec3& origin, const glm::vec3& velocity,
    const glm::dvec3& minimum, const glm::dvec3& maximum,
    double seconds) {
    if (seconds < 0.0) return std::nullopt;
    double enter = 0.0, leave = seconds;
    for (int axis : {0, 2}) {
        if (velocity[axis] == 0.0f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
                return std::nullopt;
            continue;
        }
        double first = (minimum[axis] - origin[axis]) / velocity[axis];
        double last = (maximum[axis] - origin[axis]) / velocity[axis];
        if (first > last) std::swap(first, last);
        enter = std::max(enter, first);
        leave = std::min(leave, last);
        if (enter > leave) return std::nullopt;
    }
    const double y = origin.y + velocity.y * enter -
        0.5 * PROJECTILE_GRAVITY * enter * enter;
    if (y >= minimum.y && y <= maximum.y) return enter;
    std::optional<double> hit;
    for (double boundary : {minimum.y, maximum.y}) {
        const double offset = origin.y - boundary;
        double discriminant = static_cast<double>(velocity.y) * velocity.y +
            2.0 * PROJECTILE_GRAVITY * offset;
        // Tangent contacts can round just below zero, particularly after the
        // subtraction of world-space heights. Bound that error, not real gaps.
        const double roundoff = 8.0 * std::numeric_limits<double>::epsilon() *
            (static_cast<double>(velocity.y) * velocity.y +
             2.0 * PROJECTILE_GRAVITY * (std::abs(origin.y) + std::abs(boundary)));
        if (discriminant < -roundoff) continue;
        discriminant = std::max(0.0, discriminant);
        // Stable quadratic roots avoid cancellation for contacts near the origin.
        const double q = velocity.y + std::copysign(std::sqrt(discriminant), velocity.y);
        const double first = q / PROJECTILE_GRAVITY;
        const double second = q == 0.0 ? first : -2.0 * offset / q;
        for (double time : {first, second})
            if (time >= enter && time <= leave && (!hit || time < *hit))
                hit = time;
    }
    return hit;
}

inline std::optional<glm::vec3> lowArcBallisticVelocity(
    const glm::dvec3& origin, const glm::dvec3& target, float speed,
    const glm::vec3& inheritedVelocity = glm::vec3(0.0f)) {
    if (speed <= 0.0f) return std::nullopt;
    const glm::dvec3 delta = target - origin;
    if (glm::length(delta) <= 0.00001) return inheritedVelocity;

    const glm::dvec3 inherited(inheritedVelocity);
    const auto requiredRelativeSpeedSquared = [&](double time) {
        const glm::dvec3 relativeVelocity = delta / time - inherited +
            glm::dvec3(0.0, 0.5 * PROJECTILE_GRAVITY * time, 0.0);
        return glm::dot(relativeVelocity, relativeVelocity);
    };
    const double targetSpeedSquared = static_cast<double>(speed) * speed;
    constexpr double minimumTime = 0.005;
    constexpr double maximumTime = 6.0;
    constexpr int searchSteps = 512;
    double lower = minimumTime;
    double lowerValue = requiredRelativeSpeedSquared(lower) - targetSpeedSquared;
    for (int step = 1; step <= searchSteps; ++step) {
        const double upper = minimumTime +
            (maximumTime - minimumTime) * step / searchSteps;
        const double upperValue =
            requiredRelativeSpeedSquared(upper) - targetSpeedSquared;
        if (lowerValue > 0.0 && upperValue <= 0.0) {
            double low = lower;
            double high = upper;
            for (int iteration = 0; iteration < 48; ++iteration) {
                const double middle = (low + high) * 0.5;
                if (requiredRelativeSpeedSquared(middle) > targetSpeedSquared)
                    low = middle;
                else
                    high = middle;
            }
            const double time = (low + high) * 0.5;
            const glm::dvec3 relativeVelocity = delta / time - inherited +
                glm::dvec3(0.0, 0.5 * PROJECTILE_GRAVITY * time, 0.0);
            return glm::vec3(relativeVelocity + inherited);
        }
        lower = upper;
        lowerValue = upperValue;
    }
    return std::nullopt;
}
