#version 330 core
out vec4 FragColor;

uniform vec3 uColor;
uniform vec3 uLightDirection;
uniform float uRainIntensity;
uniform int uManualGamma;
flat in vec3 vNormal;
flat in uint vFaceVisible;

void main() {
    if (vFaceVisible == 0u) discard;
    float direct = max(dot(vNormal, normalize(uLightDirection)), 0.0);
    float upward = vNormal.y * 0.5 + 0.5;
    float shade = 0.48 + 0.28 * upward +
                  0.24 * direct * (1.0 - uRainIntensity * 0.55);
    vec3 color = uColor * shade;
    if (uManualGamma != 0) color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
