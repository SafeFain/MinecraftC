#version 330 core
layout(location=0) in vec3 aPosition; layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV; layout(location=3) in uvec4 aJoints;
layout(location=4) in vec4 aWeights;
uniform mat4 uViewProjection,uModel,uNode,uJoints[64];
out vec2 vUV; out vec3 vNormal; out vec3 vWorldPosition;
void main(){mat4 skin=aWeights.x*uJoints[aJoints.x]+aWeights.y*uJoints[aJoints.y]+aWeights.z*uJoints[aJoints.z]+aWeights.w*uJoints[aJoints.w];vec4 world=uModel*uNode*skin*vec4(aPosition,1.0);vWorldPosition=world.xyz;vNormal=normalize(mat3(transpose(inverse(uModel*uNode*skin)))*aNormal);vUV=vec2(aUV.x,1.0-aUV.y);gl_Position=uViewProjection*world;}
