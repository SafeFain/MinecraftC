#version 450

layout(location=0) in vec2 uv;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 worldPosition;
layout(set=0,binding=0) uniform sampler2D baseTexture;
layout(set=1,binding=0) uniform ModelUniforms {
    mat4 viewProjection;
    mat4 model;
    mat4 node;
    mat4 joints[64];
    vec4 baseColor;
    vec4 params;
    vec4 cameraFogStart;
    vec4 fogColorEnd;
    vec4 lightDirection;
    vec4 directColor;
    vec4 ambientColor;
    vec4 options;
} data;
layout(location=0) out vec4 outColor;

void main() {
    vec4 base=(data.params.y>0.5?texture(baseTexture,uv):vec4(1.0))*data.baseColor;
    if(base.a<data.params.x)discard;
    float diffuse=max(dot(normalize(normal),normalize(data.lightDirection.xyz)),0.0);
    vec3 color=base.rgb*(data.ambientColor.rgb+data.directColor.rgb*diffuse);
    float fog=clamp((length(worldPosition-data.cameraFogStart.xyz)-data.cameraFogStart.w)/
        max(data.fogColorEnd.w-data.cameraFogStart.w,0.001),0.0,1.0);
    color=mix(color,data.fogColorEnd.rgb,fog);
    if(data.params.z>0.5)color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));
    outColor=vec4(color,base.a);
}
