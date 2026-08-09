#version 450

layout(location = 0) in vec2 textureUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D blockTexture;

void main() {
    outColor = texture(blockTexture, textureUv);
}
