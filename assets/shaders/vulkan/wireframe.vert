#version 450

layout(push_constant) uniform WireUniforms {
    mat4 modelViewProjection;
    vec4 options;
} wire;

const vec3 positions[24]=vec3[24](
    vec3(0,0,0),vec3(1,0,0), vec3(1,0,0),vec3(1,0,1),
    vec3(1,0,1),vec3(0,0,1), vec3(0,0,1),vec3(0,0,0),
    vec3(0,1,0),vec3(1,1,0), vec3(1,1,0),vec3(1,1,1),
    vec3(1,1,1),vec3(0,1,1), vec3(0,1,1),vec3(0,1,0),
    vec3(0,0,0),vec3(0,1,0), vec3(1,0,0),vec3(1,1,0),
    vec3(1,0,1),vec3(1,1,1), vec3(0,0,1),vec3(0,1,1));

void main(){
    gl_Position=wire.modelViewProjection*vec4(positions[gl_VertexIndex],1.0);
}
