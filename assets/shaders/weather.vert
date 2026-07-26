#version 330 core

layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aPositionKind;
layout(location = 2) in float aPhase;

uniform mat4 uViewProjection;
uniform vec3 uCameraRight;
uniform float uTime;

out float vKind;
out vec2 vUv;

void main() {
    float kind = aPositionKind.w;
    float width = kind < 0.5 ? 0.045 : (kind < 1.5 ? 0.18 : 0.16);
    float height = kind < 0.5 ? 1.65 : (kind < 1.5 ? 0.22 : 4.2);
    vec3 position = aPositionKind.xyz;
    if (kind > 0.5 && kind < 1.5)
        position += uCameraRight * sin(uTime * 1.7 + aPhase * 6.283) * 0.13;
    if (kind > 1.5)
        position += uCameraRight * (fract(sin(aPhase * 91.7) * 43758.5) - 0.5) * 0.7;
    position += uCameraRight * aCorner.x * width;
    position.y += aCorner.y * height;
    gl_Position = uViewProjection * vec4(position, 1.0);
    vKind = kind;
    vUv = aCorner + vec2(0.5, 0.0);
}
