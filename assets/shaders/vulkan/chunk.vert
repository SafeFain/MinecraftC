#version 450

layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inLighting;
layout(location=2) in vec3 inTileCoord;
layout(location=3) in float inFace;
layout(push_constant) uniform FrameUniforms {
    mat4 modelViewProjection;
    vec4 atlasAndLighting;
    vec4 chunkOrigin;
    vec4 tint;
    vec4 lodWorldOrigin;
} frame;
layout(set=1,binding=0) uniform ChunkEnvironment {
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 directColorIntensity;
    vec4 ambientColorIntensity;
    vec4 fogColorDistance;
    vec4 materialParams;
    vec4 weatherParams;
    mat4 shadowMatrices[4];
    vec4 shadowSplits;
    vec4 shadowOptions;
} environment;
layout(location=0) out vec4 lighting;
layout(location=1) out vec2 tileUv;
layout(location=2) flat out float tile;
layout(location=3) flat out float face;
layout(location=4) out vec3 worldPosition;

void main() {
    vec3 position=inPosition;
    vec3 world=inPosition+frame.chunkOrigin.xyz;
    float surfaceFace=inFace>=16.0?inFace-16.0:inFace;
    if(surfaceFace>5.5){
        float rootWeight=clamp(inTileCoord.y,0.0,1.0);
        float phase=dot(world.xz,vec2(0.43,0.71));
        vec2 wind=vec2(sin(environment.weatherParams.x*1.18+phase),
            cos(environment.weatherParams.x*0.91+phase*1.37));
        position.xz+=wind*rootWeight*(0.026+environment.weatherParams.y*0.048);
        world=position+frame.chunkOrigin.xyz;
    }
    gl_Position=frame.modelViewProjection*vec4(position,1.0);
    lighting=inLighting;
    tileUv=inTileCoord.xy;
    tile=inTileCoord.z;
    face=inFace;
    worldPosition=world;
}
