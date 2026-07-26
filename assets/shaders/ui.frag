#version 330 core

in vec4 vColor;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform int uUseTexture;
uniform int uManualGamma;
out vec4 outColor;

void main() {
    vec4 linearTint = vec4(pow(max(vColor.rgb, vec3(0.0)), vec3(2.2)), vColor.a);
    vec4 color = uUseTexture != 0
        ? texture(uTexture, vTexCoord) * linearTint : linearTint;
    if (color.a < 0.1) discard;
    if (uManualGamma != 0)
        color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = color;
}
