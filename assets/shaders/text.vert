#version 330 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

uniform mat4 uProjection;
uniform vec3 uTextColor;

out vec2 vTexCoord;
out vec3 vColor;

void main() {
    gl_Position = uProjection * vec4(inPosition, 0.0, 1.0);
    vTexCoord = inTexCoord;
    vColor = uTextColor;
}
