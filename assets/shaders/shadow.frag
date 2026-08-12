#version 330 core

in vec2 vAtlasUV;
uniform sampler2D uBlockAtlas;

void main() {
    if (texture(uBlockAtlas, vAtlasUV).a < 0.35) discard;
}
