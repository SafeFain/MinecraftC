#version 330 core

in vec2 vTexCoord;
in vec3 vColor;

uniform sampler2D uFontAtlas;

out vec4 outColor;

void main() {
    float alpha = texture(uFontAtlas, vTexCoord).a;
    if (alpha < 0.5) discard;
    outColor = vec4(vColor, 1.0);
}
