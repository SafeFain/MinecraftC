#version 450

layout(push_constant) uniform CloudUniforms {
    mat4 viewProjection;
    vec4 origin;
    vec4 color;
    vec4 lighting;
} cloud;
layout(location=0) flat in vec3 faceNormal;
layout(location=1) flat in uint faceVisible;
layout(location=0) out vec4 outColor;
void main(){
    if(faceVisible==0u)discard;
    float direct=max(dot(faceNormal,normalize(cloud.lighting.xyz)),0.0);
    float upward=faceNormal.y*0.5+0.5;
    float weather=cloud.lighting.w;
    float legacyShade=0.48+0.28*upward+0.24*direct*(1.0-weather*0.55);
    float enhancedShade=0.42+0.31*upward+0.27*direct*(1.0-weather*0.58);
    float shade=mix(legacyShade,enhancedShade,cloud.origin.w);
    float silver=pow(direct,6.0)*(1.0-weather)*0.20;
    vec3 color=cloud.color.rgb*shade+
        vec3(1.0,0.88,0.70)*silver*cloud.origin.w;
    if(cloud.color.a>0.5)color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));
    outColor=vec4(color,1.0);
}
