#version 450

layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUv;
layout(location=3) in uvec4 inJoints;
layout(location=4) in vec4 inWeights;
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
layout(location=0) out vec2 uv;
layout(location=1) out vec3 normal;
layout(location=2) out vec3 worldPosition;

void main() {
    mat4 skin=inWeights.x*data.joints[inJoints.x]+inWeights.y*data.joints[inJoints.y]+
        inWeights.z*data.joints[inJoints.z]+inWeights.w*data.joints[inJoints.w];
    mat4 transform=data.model*data.node*skin;
    vec4 world=transform*vec4(inPosition,1.0);
    uv=inUv;
    normal=normalize(mat3(transpose(inverse(transform)))*inNormal);
    worldPosition=world.xyz;
    gl_Position=data.viewProjection*world;
}
