#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;

uniform mat4 uMVP;
uniform int uTextureIndex;

out vec2 vUV;

void main() {
    int column = uTextureIndex % 3;
    int sourceRow = uTextureIndex / 3;
    int atlasRow = 2 - sourceRow;
    vUV = (vec2(column, atlasRow) + aUV) / 3.0;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
