#version 450
#extension GL_GOOGLE_include_directive : require

layout(set=0,binding=0) uniform sampler2D surfaceData;
layout(location=0) in vec2 vUv;
layout(location=0) out vec4 outEffects;

layout(push_constant) uniform PostConstants {
    vec4 exposureBloom;
    vec4 effects;
    vec4 texelTime;
    vec4 environment;
    vec4 celestial;
    vec4 screenQuality;
    vec4 reflection;
    vec4 sunScreen;
} post;

vec3 decodeNormal(vec2 encoded){
    vec2 f=encoded*2.0-1.0;
    vec3 n=vec3(f,1.0-abs(f.x)-abs(f.y));
    if(n.z<0.0)n.xy=(1.0-abs(n.yx))*sign(n.xy);
    return normalize(n);
}

#include "screen_effect_common.glsl"

void main(){
    vec4 center=texture(surfaceData,vUv);
    vec2 effects=vec2(screenAo(center,decodeNormal(center.xy)),screenShafts());
    outEffects=vec4(effects,0.0,1.0);
}
