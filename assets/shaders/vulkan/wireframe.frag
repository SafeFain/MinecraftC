#version 450

layout(location=0) out vec4 outColor;
layout(push_constant) uniform WireUniforms {mat4 modelViewProjection;vec4 options;} wire;

void main(){
    vec3 color=vec3(0.0);if(wire.options.x>0.5)
        color=pow(color,vec3(1.0/2.2));outColor=vec4(color,1.0);
}
