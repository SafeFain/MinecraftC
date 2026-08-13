#version 450

layout(location = 0) in vec2 textureUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D blockTexture;
layout(push_constant) uniform FrameUniforms {
    mat4 modelViewProjection;
    vec4 atlasAndLighting;
    vec4 chunkOrigin;
    vec4 tint;
} frame;

void main() {
    vec4 color=texture(blockTexture, textureUv)*frame.tint;
    if(color.a<0.1)discard;
    if(frame.atlasAndLighting.w>0.5)
        color.rgb=pow(max(color.rgb,vec3(0.0)),vec3(1.0/2.2));
    outColor=color;
}
