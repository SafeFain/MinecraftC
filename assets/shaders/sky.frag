#version 330 core

in vec2 vNdc;

uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform float uStarIntensity;
uniform int uManualGamma;

out vec4 outColor;

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

void main() {
    vec4 farPoint = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 world = farPoint.xyz / farPoint.w;
    vec3 ray = normalize(world - uCameraPosition);

    float height = smoothstep(-0.08, 0.72, ray.y);
    vec3 color = mix(uHorizonColor, uZenithColor, height);

    float sun = smoothstep(0.99915, 0.99972, dot(ray, uSunDirection));
    float sunGlow = pow(max(dot(ray, uSunDirection), 0.0), 96.0);
    color += vec3(1.0, 0.72, 0.38) * sunGlow * 0.22;
    color = mix(color, vec3(1.0, 0.94, 0.74), sun);

    float moon = smoothstep(0.99945, 0.99978, dot(ray, uMoonDirection));
    color = mix(color, vec3(0.72, 0.80, 1.0), moon * uStarIntensity);

    vec3 starCell = floor(ray * 620.0);
    float starSeed = hash31(starCell);
    float stars = step(0.9965, starSeed) * smoothstep(0.0, 0.18, ray.y);
    color += vec3(0.68, 0.76, 1.0) * stars * uStarIntensity;

    if (uManualGamma != 0)
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
