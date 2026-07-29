#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec4 aInstancePositionAndWidth;
layout(location = 3) in vec2 aInstanceDepthAndHeight;

uniform mat4 uViewProjection;

void main() {
    vec3 size = vec3(aInstancePositionAndWidth.w,
                     aInstanceDepthAndHeight.y,
                     aInstanceDepthAndHeight.x);
    vec3 worldPosition = aInstancePositionAndWidth.xyz + aPosition * size;
    gl_Position = uViewProjection * vec4(worldPosition, 1.0);
}
