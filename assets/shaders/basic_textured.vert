#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;

uniform mat4 uMVP;
out vec2 vUV;

void main() {
    // Asset pixel rows use a top-left origin, matching Vulkan sampling. OpenGL
    // texture coordinates use a bottom-left origin, so normalize here.
    vUV = vec2(aUV.x, 1.0 - aUV.y);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
