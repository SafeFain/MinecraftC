#version 450

layout(location=0) in float kind;
layout(location=1) in vec2 uv;
layout(location=2) flat in float textureIndex;
layout(location=3) flat in float phase;
layout(push_constant) uniform ParticleUniforms {
    mat4 viewProjection;
    vec4 cameraRightTime;
    vec4 cameraUpIntensity;
    vec4 atlasParams;
} frame;
layout(set=0,binding=0) uniform sampler2D blockAtlas;
layout(location=0) out vec4 outColor;

vec4 atlasFragment(){
    float tiles=frame.atlasParams.x;
    float tile=floor(textureIndex+0.5);
    vec2 origin=vec2(mod(tile,tiles),floor(tile/tiles))/tiles;
    vec2 offset=vec2(fract(phase*7.13),fract(phase*13.71))*0.55;
    return texture(blockAtlas,origin+(offset+uv*0.42)/tiles);
}

void main(){
    vec4 color;
    if(kind<0.5){
        color=atlasFragment();
        color.rgb=mix(color.rgb,vec3(0.58,0.72,0.90),0.32);
        color.a*=0.86*frame.cameraUpIntensity.w;
    }else if(kind<1.5){
        float flake=1.0-smoothstep(0.30,0.52,length(uv-vec2(0.5)));
        color=vec4(0.90,0.94,1.0,flake*0.84*frame.cameraUpIntensity.w);
    }else if(kind<2.5){
        float edge=smoothstep(0.0,0.18,uv.x)*
            (1.0-smoothstep(0.82,1.0,uv.x));
        color=vec4(0.78,0.87,1.0,edge*0.96);
    }else{
        color=atlasFragment();
        color.a*=0.92;
    }
    if(color.a<0.01)discard;
    if(frame.atlasParams.y>0.5)
        color.rgb=pow(max(color.rgb,vec3(0.0)),vec3(1.0/2.2));
    outColor=color;
}
