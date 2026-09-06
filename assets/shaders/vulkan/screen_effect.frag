#version 450

layout(set=0,binding=0) uniform sampler2D surfaceData;
layout(location=0) in vec2 vUv;
layout(location=0) out vec4 outEffects;

layout(push_constant) uniform PostConstants {
    vec4 exposureBloom;
    vec4 effects;
    vec4 texelTime;
    vec4 environment;
    vec4 celestial;
    vec4 screenQuality;
    vec4 reflection;
    vec4 sunScreen;
} post;

vec3 decodeNormal(vec2 encoded){
    vec2 f=encoded*2.0-1.0;
    vec3 n=vec3(f,1.0-abs(f.x)-abs(f.y));
    if(n.z<0.0)n.xy=(1.0-abs(n.yx))*sign(n.xy);
    return normalize(n);
}

void main(){
    vec4 center=texture(surfaceData,vUv);
    float centerDistance=abs(center.z);
    vec3 normal=decodeNormal(center.xy);
    int directions=int(post.screenQuality.x+0.5);
    int steps=int(post.screenQuality.y+0.5);
    float occlusion=0.0;
    float samples=0.0;
    if(center.z>0.01&&center.w<1.9){
        for(int direction=0;direction<8;++direction){
            if(direction>=directions)break;
            float angle=6.2831853*(float(direction)+0.37)/max(float(directions),1.0);
            vec2 axis=vec2(cos(angle),sin(angle));
            for(int stepIndex=1;stepIndex<=4;++stepIndex){
                if(stepIndex>steps)break;
                float radius=(2.0+float(stepIndex)*3.0)*
                    (1.0+min(centerDistance,96.0)*0.012);
                vec2 uv=vUv+axis*post.texelTime.xy*radius;
                vec4 neighbor=texture(surfaceData,clamp(uv,vec2(0.0),vec2(1.0)));
                float neighborDistance=abs(neighbor.z);
                float closer=smoothstep(0.18,1.8,
                    centerDistance-neighborDistance-float(stepIndex)*0.08);
                float facing=max(dot(normal,decodeNormal(neighbor.xy)),0.0);
                occlusion+=closer*(0.55+0.45*facing);
                samples+=1.0;
            }
        }
    }
    float ao=1.0-clamp(occlusion/max(samples,1.0),0.0,1.0)*0.30;

    int shaftSamples=int(post.screenQuality.z+0.5);
    float shafts=0.0;
    if(shaftSamples>0&&post.sunScreen.z>0.5&&post.celestial.w<0.5){
        vec2 toSun=post.sunScreen.xy-vUv;
        float illumination=0.0;
        for(int i=1;i<=16;++i){
            if(i>shaftSamples)break;
            vec2 uv=vUv+toSun*(float(i)/float(shaftSamples))*0.92;
            float distanceValue=texture(surfaceData,
                clamp(uv,vec2(0.0),vec2(1.0))).z;
            illumination+=1.0-smoothstep(0.001,0.08,abs(distanceValue));
        }
        float clearWeather=1.0-clamp(post.environment.x*0.72+
            post.environment.y*0.55,0.0,0.92);
        float sunHeight=smoothstep(-0.02,0.28,post.celestial.y);
        float radial=1.0-smoothstep(0.08,1.15,length(toSun));
        shafts=illumination/max(float(shaftSamples),1.0)*clearWeather*
            sunHeight*radial*post.effects.w;
    }
    outEffects=vec4(ao,shafts,0.0,1.0);
}
