#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 modelViewProjection;
} frame;

layout(location = 0) out vec2 textureUv;

void main() {
    gl_Position = frame.modelViewProjection * vec4(inPosition, 1.0);
    textureUv = inUv;
}
