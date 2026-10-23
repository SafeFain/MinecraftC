#version 450

layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUv;
layout(location=3) in uvec4 inJoints;
layout(location=4) in vec4 inWeights;
struct ModelEnvironment {
    mat4 viewProjection;
    vec4 cameraFogStart;
    vec4 fogColorEnd;
    vec4 lightDirection;
    vec4 directColor;
    vec4 ambientColor;
    vec4 options;
};
struct ModelInstance {
    mat4 transform;
    vec4 baseColor;
    vec4 params;
    uvec4 indices;
};
layout(std430,set=1,binding=0) readonly buffer Environments { ModelEnvironment environments[]; };
layout(std430,set=1,binding=1) readonly buffer Instances { ModelInstance instances[]; };
layout(std430,set=1,binding=2) readonly buffer Palettes { mat4 joints[]; };
layout(location=3) flat out uint instanceIndex;
layout(location=0) out vec2 uv;
layout(location=1) out vec3 normal;
layout(location=2) out vec3 worldPosition;

void main() {
    instanceIndex=gl_InstanceIndex;
    ModelInstance data=instances[instanceIndex];
    ModelEnvironment environment=environments[data.indices.z];
    mat4 skin=mat4(1.0);
    if(data.indices.y>0u) {
        uvec4 j=min(inJoints,uvec4(data.indices.y-1u))+uvec4(data.indices.x);
        skin=inWeights.x*joints[j.x]+inWeights.y*joints[j.y]+inWeights.z*joints[j.z]+inWeights.w*joints[j.w];
    }
    mat4 transform=data.transform*skin;
    vec4 world=transform*vec4(inPosition,1.0);
    uv=inUv;
    normal=normalize(mat3(transpose(inverse(transform)))*inNormal);
    worldPosition=world.xyz;
    gl_Position=environment.viewProjection*world;
}
