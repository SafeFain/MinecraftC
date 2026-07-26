#version 330 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

uniform mat4 uProjection;
uniform vec4 uColor;

out vec4 vColor;
out vec2 vTexCoord;

void main() {
    gl_Position = uProjection * vec4(inPosition, 0.0, 1.0);
    vColor = uColor;
    vTexCoord = inTexCoord;
}
