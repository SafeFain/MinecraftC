#version 450

layout(push_constant) uniform CloudUniforms {
    mat4 viewProjection;
    vec4 origin;
    vec4 color;
} cloud;
layout(location=0) out vec4 outColor;
void main(){outColor=vec4(cloud.color.rgb,1.0);}
