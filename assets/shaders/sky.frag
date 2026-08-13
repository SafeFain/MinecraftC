#version 330 core

in vec2 vNdc;

uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform float uStarIntensity;
uniform float uRainIntensity;
uniform float uThunderIntensity;
uniform float uWeatherTime;
uniform int uRenderClouds;
uniform int uRenderCirrus;
uniform int uManualGamma;

out vec4 outColor;

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float cloudNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash31(vec3(cell, 17.0));
    float b = hash31(vec3(cell + vec2(1.0, 0.0), 17.0));
    float c = hash31(vec3(cell + vec2(0.0, 1.0), 17.0));
    float d = hash31(vec3(cell + vec2(1.0), 17.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec2 cubeSkyUv(vec3 direction, out float face) {
    vec3 magnitude = abs(direction);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        face = direction.x >= 0.0 ? 0.0 : 1.0;
        return vec2(direction.z, direction.y) / magnitude.x;
    }
    if (magnitude.y >= magnitude.z) {
        face = direction.y >= 0.0 ? 2.0 : 3.0;
        return vec2(direction.x, direction.z) / magnitude.y;
    }
    face = direction.z >= 0.0 ? 4.0 : 5.0;
    return vec2(direction.x, direction.y) / magnitude.z;
}

vec3 starTemperature(float seed) {
    vec3 warm = vec3(1.00, 0.76, 0.58);
    vec3 neutral = vec3(0.92, 0.95, 1.00);
    vec3 blue = vec3(0.60, 0.73, 1.00);
    return seed < 0.48
        ? mix(warm, neutral, smoothstep(0.08, 0.48, seed))
        : mix(neutral, blue, smoothstep(0.58, 0.96, seed));
}

vec3 starLayer(vec2 skyUv, float face, float density, float threshold,
               float radiusScale, float time) {
    vec2 coordinate = (skyUv * 0.5 + 0.5) * density;
    vec2 cell = floor(coordinate);
    vec2 local = fract(coordinate);
    vec3 identity = vec3(cell, face * 137.0 + density);
    float existence = hash31(identity);
    float visible = step(threshold, existence);

    vec2 offset = vec2(
        hash31(identity + vec3(19.1, 7.7, 3.4)),
        hash31(identity + vec3(3.8, 29.3, 11.2)));
    offset = 0.16 + offset * 0.68;
    float character = hash31(identity + vec3(41.7, 13.5, 23.9));
    float distanceToStar = length(local - offset);
    float radius = mix(0.045, 0.095, character * character) * radiusScale;
    float antialias = max(fwidth(distanceToStar), 0.006);
    float disc = 1.0 - smoothstep(radius - antialias,
                                  radius + antialias, distanceToStar);
    float core = 1.0 - smoothstep(0.0, radius * 0.34, distanceToStar);
    float brightness = mix(0.42, 1.18, character);
    float twinklePhase = hash31(identity + vec3(61.2, 5.4, 37.8));
    float twinkle = 0.91 + 0.09 * sin(
        time * mix(0.45, 0.82, twinklePhase) + twinklePhase * 6.2831853);
    return starTemperature(hash31(identity + vec3(5.2, 71.4, 17.6))) *
           (disc + core * 0.42) * brightness * twinkle * visible;
}

void main() {
    vec4 farPoint = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 world = farPoint.xyz / farPoint.w;
    vec3 ray = normalize(world - uCameraPosition);

    float height = smoothstep(-0.08, 0.72, ray.y);
    vec3 color = mix(uHorizonColor, uZenithColor, height);

    float celestial = 1.0 - uRainIntensity;
    float sun = smoothstep(0.99915, 0.99972, dot(ray, uSunDirection)) * celestial;
    float sunGlow = pow(max(dot(ray, uSunDirection), 0.0), 96.0);
    float forwardHaze = pow(max(dot(ray, uSunDirection), 0.0), 8.0) *
                        smoothstep(-0.10, 0.24, uSunDirection.y);
    color += vec3(1.0, 0.55, 0.20) * forwardHaze * 0.10 * celestial;
    color += vec3(1.0, 0.72, 0.38) * sunGlow * 0.34 * celestial;
    color += vec3(3.2, 2.45, 1.35) * sun;

    float moon = smoothstep(0.99945, 0.99978, dot(ray, uMoonDirection)) * celestial;
    float moonHalo = pow(max(dot(ray, uMoonDirection), 0.0), 180.0) *
                     celestial * uStarIntensity;
    color += vec3(0.30, 0.40, 0.72) * moonHalo * 0.14;
    color = mix(color, vec3(0.72, 0.80, 1.0), moon * uStarIntensity);

    float starFace = 0.0;
    vec2 starUv = cubeSkyUv(ray, starFace);
    float horizonFade = smoothstep(-0.015, 0.20, ray.y);
    float clearNight = uStarIntensity * pow(max(celestial, 0.0), 1.7);
    float moonOcclusion = 1.0 - smoothstep(
        0.9965, 0.99955, dot(ray, uMoonDirection));

    vec3 milkyNormal = normalize(vec3(0.28, 0.91, -0.30));
    float milkyLatitude = abs(dot(ray, milkyNormal));
    float milkyBand = exp(-milkyLatitude * milkyLatitude * 76.0);
    float milkyTexture = cloudNoise(
        starUv * 7.5 + vec2(starFace * 17.3, starFace * 9.1));
    milkyTexture *= 0.55 + 0.45 * cloudNoise(
        starUv * 18.0 + vec2(starFace * 4.7, 31.0));
    color += mix(vec3(0.075, 0.095, 0.18), vec3(0.16, 0.12, 0.23),
                 smoothstep(0.25, 0.85, milkyTexture)) *
             milkyBand * (0.035 + milkyTexture * 0.055) *
             clearNight * horizonFade;

    vec3 stars = starLayer(starUv, starFace, 185.0, 0.988, 0.78, uWeatherTime);
    stars += starLayer(starUv, starFace, 96.0, 0.966, 1.00, uWeatherTime);
    stars += starLayer(starUv, starFace, 52.0, 0.975, 1.34, uWeatherTime) * 1.18;
    color += stars * clearNight * horizonFade * moonOcclusion;

    if (uRenderCirrus != 0 && ray.y > 0.04) {
        vec2 cirrusUv = ray.xz / max(ray.y + 0.20, 0.10);
        cirrusUv = cirrusUv * vec2(0.34, 1.8) +
                   vec2(uWeatherTime * 0.0025, uWeatherTime * 0.0007);
        float wisps = cloudNoise(cirrusUv) * 0.58 +
                      cloudNoise(cirrusUv * 2.13 + 19.0) * 0.42;
        wisps = smoothstep(0.64, 0.83, wisps) *
                smoothstep(0.04, 0.28, ray.y) * (1.0 - uRainIntensity);
        color += vec3(0.42, 0.48, 0.56) * wisps * 0.16;
    }

    if (uRenderClouds != 0 && ray.y > 0.025) {
        vec2 cloudUv = ray.xz / max(ray.y, 0.06) * 0.42;
        cloudUv += vec2(uWeatherTime * 0.012, uWeatherTime * 0.004);
        float noise = cloudNoise(cloudUv) * 0.65 +
                      cloudNoise(cloudUv * 2.07 + 13.0) * 0.35;
        float coverage = mix(0.72, 0.40, uRainIntensity);
        float cloud = smoothstep(coverage, coverage + 0.15, noise) *
                      smoothstep(0.025, 0.16, ray.y);
        vec3 cloudColor = mix(vec3(0.82, 0.86, 0.90),
                              vec3(0.12, 0.14, 0.17),
                              uRainIntensity * 0.72 + uThunderIntensity * 0.28);
        color = mix(color, cloudColor, cloud * (0.36 + uRainIntensity * 0.54));
    }

    if (uManualGamma != 0)
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
