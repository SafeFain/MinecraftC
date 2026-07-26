#version 330 core
out vec4 FragColor;

uniform vec3 uColor;
uniform sampler2D uEntityAtlas;
uniform int uUseTexture;
uniform int uManualGamma;
in vec2 vUV;

void main() {
    vec3 color = uUseTexture != 0 ? texture(uEntityAtlas, vUV).rgb : uColor;
    if (uManualGamma != 0) color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
