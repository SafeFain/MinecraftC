#version 450

layout(location=0) in vec4 inPositionKind;
layout(location=1) in vec4 inParticleParams;
layout(push_constant) uniform ParticleUniforms {
    mat4 viewProjection;
    vec4 cameraRightTime;
    vec4 cameraUpIntensity;
    vec4 atlasParams;
} frame;
layout(location=0) out float kind;
layout(location=1) out vec2 uv;
layout(location=2) flat out float textureIndex;
layout(location=3) flat out float phase;

const vec2 corners[6]=vec2[6](
    vec2(-0.5,0.0),vec2(0.5,0.0),vec2(0.5,1.0),
    vec2(-0.5,0.0),vec2(0.5,1.0),vec2(-0.5,1.0));

void main(){
    kind=inPositionKind.w;
    phase=inParticleParams.x;
    float width=kind<0.5?inParticleParams.z:(kind<1.5?0.18:
        (kind<2.5?0.16:(kind<3.5?inParticleParams.z:
        (kind<5.5?inParticleParams.z*(0.65+phase*0.85):
        (kind<6.5?inParticleParams.z*(1.0+0.30*sin(frame.cameraRightTime.w*1.6+phase*6.283)):
        (kind<7.5?inParticleParams.z*(0.80+0.20*sin(frame.cameraRightTime.w*2.2+phase*6.283)):
         inParticleParams.z*(1.0+0.35*sin(frame.cameraRightTime.w*4.0+phase*6.283))))))));
    float height=kind<0.5?inParticleParams.z:(kind<1.5?0.22:
        (kind<2.5?4.2:(kind<3.5?inParticleParams.z:
        (kind<5.5?0.13:(kind<6.5?inParticleParams.z*1.7:
        (kind<7.5?width*0.55:width))))));
    vec3 right=frame.cameraRightTime.xyz;
    vec3 up=frame.cameraUpIntensity.xyz;
    vec3 position=inPositionKind.xyz;
    if(kind>0.5&&kind<1.5)
        position+=right*sin(frame.cameraRightTime.w*1.7+phase*6.283)*0.13;
    if(kind>1.5&&kind<2.5)
        position+=right*(fract(sin(phase*91.7)*43758.5)-0.5)*0.7;
    if(kind>5.5)position+=up*sin(frame.cameraRightTime.w*0.9+phase*6.283)*0.08;
    vec2 corner=corners[gl_VertexIndex];
    if(kind<0.5||kind>2.5){
        float c=cos(inParticleParams.w),s=sin(inParticleParams.w);
        corner=mat2(c,-s,s,c)*vec2(corner.x,corner.y-0.5);
    }else{
        corner.y-=0.5;
    }
    position+=right*corner.x*width+up*corner.y*height;
    if(kind<2.5)position+=up*height*0.5;
    gl_Position=frame.viewProjection*vec4(position,1.0);
    uv=corners[gl_VertexIndex]+vec2(0.5,0.0);
    textureIndex=inParticleParams.y;
}
