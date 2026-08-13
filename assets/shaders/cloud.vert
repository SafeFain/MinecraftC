#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec4 aInstancePositionAndWidth;
layout(location = 3) in vec2 aInstanceDepthAndHeight;
layout(location = 4) in uint aVisibleFaces;

uniform mat4 uViewProjection;
uniform vec3 uCloudOrigin;
flat out vec3 vNormal;
flat out uint vFaceVisible;

const vec3 FACE_NORMALS[6] = vec3[6](
    vec3( 0.0,  0.0, -1.0), vec3(0.0, 0.0, 1.0),
    vec3(-1.0,  0.0,  0.0), vec3(1.0, 0.0, 0.0),
    vec3( 0.0,  1.0,  0.0), vec3(0.0,-1.0, 0.0));

void main() {
    int face = gl_VertexID / 6;
    vec3 size = vec3(aInstancePositionAndWidth.w,
                     aInstanceDepthAndHeight.y,
                     aInstanceDepthAndHeight.x);
    vec3 worldPosition = uCloudOrigin + aInstancePositionAndWidth.xyz +
                         aPosition * size;
    vNormal = FACE_NORMALS[face];
    vFaceVisible = (aVisibleFaces >> uint(face)) & 1u;
    gl_Position = uViewProjection * vec4(worldPosition, 1.0);
}
