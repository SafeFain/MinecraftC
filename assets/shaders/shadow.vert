#version 330 core

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 inTileData;

uniform mat4 uLightMVP;
uniform float uAtlasTiles;

out vec2 vAtlasUV;

void main() {
    float tile = floor(inTileData.z + 0.5);
    vec2 origin = vec2(mod(tile, uAtlasTiles), floor(tile / uAtlasTiles));
    vec2 local = mix(vec2(0.5 / 16.0), vec2(15.5 / 16.0), fract(inTileData.xy));
    vAtlasUV = (origin + local) / uAtlasTiles;
    gl_Position = uLightMVP * vec4(inPosition, 1.0);
}
