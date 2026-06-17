#version 330 core

layout(location = 0) in vec3 inPosition;
uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
