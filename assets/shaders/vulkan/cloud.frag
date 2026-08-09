#version 450

layout(push_constant) uniform CloudUniforms {
    mat4 viewProjection;
    vec4 origin;
    vec4 color;
} cloud;
layout(location=0) out vec4 outColor;
void main(){vec3 color=cloud.color.rgb;if(cloud.color.a>0.5)
color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));outColor=vec4(color,1.0);}
