#version 450

layout(location=0) in vec3 position;
layout(location=2) in vec3 tileData;
layout(push_constant) uniform ShadowConstants {
    mat4 lightMvp;
    vec4 atlasParams;
} shadow;
layout(location=0) out vec2 atlasUv;

void main() {
    float tile=floor(tileData.z+0.5);
    float tiles=shadow.atlasParams.x;
    vec2 origin=vec2(mod(tile,tiles),floor(tile/tiles));
    vec2 local=mix(vec2(0.5/16.0),vec2(15.5/16.0),fract(tileData.xy));
    atlasUv=(origin+local)/tiles;
    gl_Position=shadow.lightMvp*vec4(position,1.0);
}
