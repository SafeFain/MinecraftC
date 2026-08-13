#version 330 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inLighting;
layout(location = 2) in vec3 inTileCoord;
layout(location = 3) in float inFace;

uniform mat4 uMVP;
uniform vec3 uChunkOrigin;
uniform float uTime;
uniform float uRainIntensity;

out float vAO;
out float vSkyLight;
out float vAlpha;
out float vBlockLight;
out vec2 vTileUV;
out vec3 vWorldPosition;
flat out float vTile;
flat out float vFace;

void main() {
    vec3 position = inPosition;
    vec3 world = inPosition + uChunkOrigin;
    if (inFace > 5.5) {
        float rootWeight = clamp(inTileCoord.y, 0.0, 1.0);
        float phase = dot(world.xz, vec2(0.43, 0.71));
        vec2 wind = vec2(sin(uTime * 1.18 + phase),
                         cos(uTime * 0.91 + phase * 1.37));
        position.xz += wind * rootWeight *
                       (0.026 + uRainIntensity * 0.048);
        world = position + uChunkOrigin;
    }
    gl_Position = uMVP * vec4(position, 1.0);
    vWorldPosition = world;
    vAO = inLighting.x;
    vSkyLight = inLighting.y;
    vAlpha = inLighting.w;
    vBlockLight = inLighting.z;
    vTileUV = inTileCoord.xy;
    vTile = inTileCoord.z;
    vFace = inFace;
}
