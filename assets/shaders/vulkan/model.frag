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
    vec3 surfaceNormal=normalize(normal);
    vec3 light=normalize(data.lightDirection.xyz);
    vec3 viewDirection=normalize(data.cameraFogStart.xyz-worldPosition);
    float diffuse=max(dot(surfaceNormal,light),0.0);
    vec3 halfDirection=normalize(light+viewDirection);
    float specular=pow(max(dot(surfaceNormal,halfDirection),0.0),28.0)*0.13;
    vec3 color=base.rgb*(data.ambientColor.rgb+data.directColor.rgb*diffuse)+
        data.directColor.rgb*specular;
    float fogCoordinate=clamp((length(worldPosition-data.cameraFogStart.xyz)-
        data.cameraFogStart.w)/max(data.fogColorEnd.w-data.cameraFogStart.w,0.001),
        0.0,1.0);
    float fog=1.0-exp(-3.0*fogCoordinate*fogCoordinate);
    color=mix(color,data.fogColorEnd.rgb,fog);
    if(data.params.z>0.5)color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));
    outColor=vec4(color,base.a);
}
