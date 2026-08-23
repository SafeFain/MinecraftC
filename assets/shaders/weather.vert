#version 330 core

layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aPositionKind;
layout(location = 2) in vec4 aParticleParams;

uniform mat4 uViewProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uTime;

out float vKind;
out vec2 vUv;
out float vTexture;
out float vPhase;

void main() {
    float kind = aPositionKind.w;
    float phase = aParticleParams.x;
    float width = kind < 0.5 ? aParticleParams.z : (kind < 1.5 ? 0.18 :
        (kind < 2.5 ? 0.16 : (kind < 3.5 ? aParticleParams.z :
        (kind < 5.5 ? aParticleParams.z * (0.65 + phase * 0.85) :
        (kind < 6.5 ? aParticleParams.z * (1.0 + 0.30 * sin(uTime * 1.6 + phase * 6.283)) :
        (kind < 7.5 ? aParticleParams.z * (0.80 + 0.20 * sin(uTime * 2.2 + phase * 6.283)) :
         aParticleParams.z * (1.0 + 0.35 * sin(uTime * 4.0 + phase * 6.283))))))));
    float height = kind < 0.5 ? aParticleParams.z : (kind < 1.5 ? 0.22 :
        (kind < 2.5 ? 4.2 : (kind < 3.5 ? aParticleParams.z :
        (kind < 5.5 ? 0.13 : (kind < 6.5 ? aParticleParams.z * 1.7 :
        (kind < 7.5 ? width * 0.55 : width))))));
    vec3 position = aPositionKind.xyz;
    if (kind > 0.5 && kind < 1.5)
        position += uCameraRight * sin(uTime * 1.7 + phase * 6.283) * 0.13;
    if (kind > 1.5 && kind < 2.5)
        position += uCameraRight * (fract(sin(phase * 91.7) * 43758.5) - 0.5) * 0.7;
    if (kind > 5.5)
        position += uCameraUp * sin(uTime * 0.9 + phase * 6.283) * 0.08;
    vec2 corner = vec2(aCorner.x, aCorner.y - 0.5);
    if (kind < 0.5 || kind > 2.5) {
        float c = cos(aParticleParams.w), s = sin(aParticleParams.w);
        corner = mat2(c, -s, s, c) * corner;
    }
    position += uCameraRight * corner.x * width;
    position += uCameraUp * corner.y * height;
    if (kind < 2.5) position += uCameraUp * height * 0.5;
    gl_Position = uViewProjection * vec4(position, 1.0);
    vKind = kind;
    vUv = aCorner + vec2(0.5, 0.0);
    vTexture = aParticleParams.y;
    vPhase = phase;
}
