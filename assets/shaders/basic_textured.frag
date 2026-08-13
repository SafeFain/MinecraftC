#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uTint;

void main() {
    FragColor = texture(uTexture, vUV) * uTint;
    if (FragColor.a < 0.1) discard;
}
