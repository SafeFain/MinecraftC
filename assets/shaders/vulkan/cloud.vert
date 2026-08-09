#version 450

layout(location=0) in vec4 inPositionAndWidth;
layout(location=1) in vec2 inDepthAndHeight;
layout(push_constant) uniform CloudUniforms {
    mat4 viewProjection;
    vec4 origin;
    vec4 color;
} cloud;

const vec3 positions[36]=vec3[36](
    vec3(0,0,0),vec3(1,0,0),vec3(1,1,0),vec3(0,0,0),vec3(1,1,0),vec3(0,1,0),
    vec3(1,0,1),vec3(0,0,1),vec3(0,1,1),vec3(1,0,1),vec3(0,1,1),vec3(1,1,1),
    vec3(0,0,1),vec3(0,0,0),vec3(0,1,0),vec3(0,0,1),vec3(0,1,0),vec3(0,1,1),
    vec3(1,0,0),vec3(1,0,1),vec3(1,1,1),vec3(1,0,0),vec3(1,1,1),vec3(1,1,0),
    vec3(0,1,0),vec3(1,1,0),vec3(1,1,1),vec3(0,1,0),vec3(1,1,1),vec3(0,1,1),
    vec3(0,0,1),vec3(1,0,1),vec3(1,0,0),vec3(0,0,1),vec3(1,0,0),vec3(0,0,0));

void main(){
    vec3 size=vec3(inPositionAndWidth.w,inDepthAndHeight.y,inDepthAndHeight.x);
    vec3 world=cloud.origin.xyz+inPositionAndWidth.xyz+positions[gl_VertexIndex]*size;
    gl_Position=cloud.viewProjection*vec4(world,1.0);
}
