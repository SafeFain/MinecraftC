#version 450

layout(location=0) in vec4 inPositionAndWidth;
layout(location=1) in vec2 inDepthAndHeight;
layout(location=2) in uint inVisibleFaces;
layout(push_constant) uniform CloudUniforms {
    mat4 viewProjection;
    vec4 origin;
    vec4 color;
    vec4 lighting;
} cloud;
layout(location=0) flat out vec3 faceNormal;
layout(location=1) flat out uint faceVisible;

const vec3 positions[36]=vec3[36](
    vec3(0,0,0),vec3(1,0,0),vec3(1,1,0),vec3(0,0,0),vec3(1,1,0),vec3(0,1,0),
    vec3(1,0,1),vec3(0,0,1),vec3(0,1,1),vec3(1,0,1),vec3(0,1,1),vec3(1,1,1),
    vec3(0,0,1),vec3(0,0,0),vec3(0,1,0),vec3(0,0,1),vec3(0,1,0),vec3(0,1,1),
    vec3(1,0,0),vec3(1,0,1),vec3(1,1,1),vec3(1,0,0),vec3(1,1,1),vec3(1,1,0),
    vec3(0,1,0),vec3(1,1,0),vec3(1,1,1),vec3(0,1,0),vec3(1,1,1),vec3(0,1,1),
    vec3(0,0,1),vec3(1,0,1),vec3(1,0,0),vec3(0,0,1),vec3(1,0,0),vec3(0,0,0));
const vec3 normals[6]=vec3[6](
    vec3(0,0,-1),vec3(0,0,1),vec3(-1,0,0),
    vec3(1,0,0),vec3(0,1,0),vec3(0,-1,0));

void main(){
    int face=gl_VertexIndex/6;
    vec3 size=vec3(inPositionAndWidth.w,inDepthAndHeight.y,inDepthAndHeight.x);
    vec3 world=cloud.origin.xyz+inPositionAndWidth.xyz+positions[gl_VertexIndex]*size;
    faceNormal=normals[face];
    faceVisible=(inVisibleFaces>>uint(face))&1u;
    gl_Position=cloud.viewProjection*vec4(world,1.0);
}
