#version 330 core

in vec2 vTexCoord;
in vec3 vColor;

uniform sampler2D uFontAtlas;
uniform int uManualGamma;

out vec4 outColor;

void main() {
    float alpha = texture(uFontAtlas, vTexCoord).a;
    if (alpha < 0.01) discard;
    vec3 color = pow(max(vColor, vec3(0.0)), vec3(2.2));
    if (uManualGamma != 0)
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, alpha);
}
