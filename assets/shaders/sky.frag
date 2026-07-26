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

void main() {
    vec4 farPoint = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 world = farPoint.xyz / farPoint.w;
    vec3 ray = normalize(world - uCameraPosition);

    float height = smoothstep(-0.08, 0.72, ray.y);
    vec3 color = mix(uHorizonColor, uZenithColor, height);

    float celestial = 1.0 - uRainIntensity;
    float sun = smoothstep(0.99915, 0.99972, dot(ray, uSunDirection)) * celestial;
    float sunGlow = pow(max(dot(ray, uSunDirection), 0.0), 96.0);
    color += vec3(1.0, 0.72, 0.38) * sunGlow * 0.22 * celestial;
    color = mix(color, vec3(1.0, 0.94, 0.74), sun);

    float moon = smoothstep(0.99945, 0.99978, dot(ray, uMoonDirection)) * celestial;
    color = mix(color, vec3(0.72, 0.80, 1.0), moon * uStarIntensity);

    vec3 starCell = floor(ray * 620.0);
    float starSeed = hash31(starCell);
    float stars = step(0.9965, starSeed) * smoothstep(0.0, 0.18, ray.y);
    color += vec3(0.68, 0.76, 1.0) * stars * uStarIntensity;

    if (ray.y > 0.025) {
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
