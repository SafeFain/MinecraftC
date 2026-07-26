#version 330 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inLighting;
layout(location = 2) in vec3 inTileCoord;
layout(location = 3) in float inFace;

uniform mat4 uMVP;
uniform vec3 uChunkOrigin;

out float vAO;
out float vSkyLight;
out float vAlpha;
out float vBlockLight;
out vec2 vTileUV;
out vec3 vWorldPosition;
flat out float vTile;
flat out float vFace;

void main() {
    gl_Position = uMVP * vec4(inPosition, 1.0);
    vWorldPosition = inPosition + uChunkOrigin;
    vAO = inLighting.x;
    vSkyLight = inLighting.y;
    vAlpha = inLighting.w;
    vBlockLight = inLighting.z;
    vTileUV = inTileCoord.xy;
    vTile = inTileCoord.z;
    vFace = inFace;
}
