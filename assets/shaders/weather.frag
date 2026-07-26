#version 330 core

in float vKind;
in vec2 vUv;

uniform float uIntensity;
uniform int uManualGamma;

out vec4 outColor;

void main() {
    vec4 color;
    if (vKind < 0.5) {
        float edge = smoothstep(0.0, 0.22, vUv.x) *
                     (1.0 - smoothstep(0.78, 1.0, vUv.x));
        color = vec4(0.56, 0.70, 0.86, edge * 0.72 * uIntensity);
    } else if (vKind < 1.5) {
        vec2 centered = vUv - vec2(0.5);
        float flake = 1.0 - smoothstep(0.30, 0.52, length(centered));
        color = vec4(0.90, 0.94, 1.0, flake * 0.84 * uIntensity);
    } else {
        float edge = smoothstep(0.0, 0.18, vUv.x) *
                     (1.0 - smoothstep(0.82, 1.0, vUv.x));
        color = vec4(0.78, 0.87, 1.0, edge * 0.96);
    }
    if (color.a < 0.01) discard;
    if (uManualGamma != 0)
        color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = color;
}
