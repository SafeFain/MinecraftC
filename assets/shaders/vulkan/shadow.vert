#version 450

layout(location=0) in vec3 position;
layout(location=2) in vec3 tileData;
layout(location=3) in float faceData;
layout(push_constant) uniform ShadowConstants {
    mat4 lightMvp;
    vec4 atlasParams;
} shadow;
layout(location=0) out vec2 atlasUv;
layout(location=1) flat out float alphaCutout;

void main() {
    int tileCount=max(int(shadow.atlasParams.x+0.5),1);
    int tileIndex=max(int(floor(tileData.z)),0);
    float tiles=float(tileCount);
    vec2 origin=vec2(tileIndex%tileCount,tileIndex/tileCount);
    vec2 local=mix(vec2(0.5/16.0),vec2(15.5/16.0),fract(tileData.xy));
    atlasUv=(origin+local)/tiles;
    alphaCutout=faceData>=32.0?1.0:
        faceData>=16.0?shadow.atlasParams.y:1.0;
    gl_Position=shadow.lightMvp*vec4(position,1.0);
}
