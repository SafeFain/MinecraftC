#version 450

layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inLighting;
layout(location=2) in vec3 inTileCoord;
layout(location=3) in float inFace;
layout(push_constant) uniform FrameUniforms {
    mat4 modelViewProjection;
    vec4 atlasAndLighting;
    vec4 chunkOrigin;
    vec4 tint;
} frame;
layout(location=0) out vec4 lighting;
layout(location=1) out vec2 tileUv;
layout(location=2) flat out float tile;
layout(location=3) flat out float face;
layout(location=4) out vec3 worldPosition;

void main() {
    gl_Position=frame.modelViewProjection*vec4(inPosition,1.0);
    lighting=inLighting;
    tileUv=inTileCoord.xy;
    tile=inTileCoord.z;
    face=inFace;
    worldPosition=inPosition+frame.chunkOrigin.xyz;
}
