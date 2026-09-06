#version 450

layout(set=0,binding=0) uniform sampler2D surfaceData;
layout(set=0,binding=1) uniform sampler2D previousHistory;
layout(set=0,binding=2) uniform sampler3D irradiance0;
layout(set=0,binding=3) uniform sampler3D irradiance1;
layout(set=0,binding=4) uniform sampler3D irradiance2;
layout(set=0,binding=5) uniform sampler3D irradiance3;
layout(set=0,binding=6) uniform sampler2D receiverAlbedo;
layout(set=0,binding=10) uniform GiScreenUniforms {
    mat4 inverseViewProjection;
    mat4 previousViewProjection;
    vec4 cameraWorld;
    vec4 currentWorldOrigin;
    vec4 previousWorldOrigin;
    vec4 minimumCellAndSize[4];
    vec4 config;
    vec4 temporal;
} gi;
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

vec4 sampleLevel(int level,vec3 world,float lod){
    vec4 mapping=gi.minimumCellAndSize[level];
    vec3 cell=world/mapping.w;
    vec3 local=cell-mapping.xyz;
    if(any(lessThan(local,vec3(0.5)))||
       any(greaterThan(local,vec3(gi.config.x-0.5))))return vec4(0.0,0.0,0.0,-1.0);
    vec3 uv=(mod(floor(cell),gi.config.x)+0.5)/gi.config.x;
    if(level==0)return textureLod(irradiance0,uv,lod);
    if(level==1)return textureLod(irradiance1,uv,lod);
    if(level==2)return textureLod(irradiance2,uv,lod);
    return textureLod(irradiance3,uv,lod);
}

vec4 sampleClipmap(vec3 world,float diameter){
    int count=int(gi.config.y+0.5);
    for(int level=0;level<4;++level){
        if(level>=count)break;
        vec4 mapping=gi.minimumCellAndSize[level];
        vec3 local=world/mapping.w-mapping.xyz;
        if(all(greaterThanEqual(local,vec3(1.0)))&&
           all(lessThan(local,vec3(gi.config.x-1.0)))){
            float lod=max(0.0,log2(max(diameter/mapping.w,1.0)));
            return sampleLevel(level,world,lod);
        }
    }
    return vec4(0.0,0.0,0.0,-1.0);
}

vec3 coneDirection(vec3 normal,int index,int count,float rotation){
    vec3 tangent=normalize(abs(normal.y)<0.9?cross(normal,vec3(0,1,0)):
        cross(normal,vec3(1,0,0)));
    vec3 bitangent=cross(normal,tangent);
    float angle=6.2831853*(float(index)/max(float(count),1.0)+rotation);
    return normalize(normal*0.72+(tangent*cos(angle)+bitangent*sin(angle))*0.69);
}

vec3 traceGi(vec3 world,vec3 normal,out float validCoverage){
    int cones=int(gi.config.z+0.5);
    int steps=int(gi.config.w+0.5);
    float rotation=fract(sin(dot(floor(gl_FragCoord.xy),vec2(12.9898,78.233))+
        post.texelTime.z)*43758.5453);
    vec3 total=vec3(0.0);
    validCoverage=0.0;
    for(int cone=0;cone<6;++cone){
        if(cone>=cones)break;
        vec3 direction=coneDirection(normal,cone,cones,rotation);
        float travel=1.25;
        float transmittance=1.0;
        vec3 accumulated=vec3(0.0);
        float coverage=0.0;
        for(int stepIndex=0;stepIndex<8;++stepIndex){
            if(stepIndex>=steps)break;
            float diameter=max(1.0,travel*0.58);
            vec4 sampleValue=sampleClipmap(
                world+normal*0.45+direction*travel,diameter);
            if(sampleValue.a<0.0){travel+=diameter;continue;}
            float opacity=clamp(sampleValue.a,0.0,1.0);
            accumulated+=sampleValue.rgb*transmittance*(0.22+opacity*0.78);
            transmittance*=1.0-opacity*0.72;
            coverage+=1.0;
            travel+=diameter;
        }
        total+=accumulated;
        validCoverage+=coverage/max(float(steps),1.0);
    }
    validCoverage/=max(float(cones),1.0);
    return total/max(float(cones),1.0)*gi.temporal.x;
}

float screenAo(vec4 center,vec3 normal){
    float centerDistance=abs(center.z);
    int directions=int(post.screenQuality.x+0.5);
    int steps=int(post.screenQuality.y+0.5);
    float occlusion=0.0,samples=0.0;
    if(center.z>0.01&&center.w<1.9){
        for(int direction=0;direction<8;++direction){
            if(direction>=directions)break;
            float angle=6.2831853*(float(direction)+0.37)/max(float(directions),1.0);
            vec2 axis=vec2(cos(angle),sin(angle));
            for(int stepIndex=1;stepIndex<=4;++stepIndex){
                if(stepIndex>steps)break;
                float radius=(2.0+float(stepIndex)*3.0)*
                    (1.0+min(centerDistance,96.0)*0.012);
                vec4 neighbor=texture(surfaceData,clamp(vUv+axis*post.texelTime.xy*radius,
                    vec2(0.0),vec2(1.0)));
                float closer=smoothstep(0.18,1.8,centerDistance-abs(neighbor.z)-
                    float(stepIndex)*0.08);
                occlusion+=closer*(0.55+0.45*max(dot(normal,
                    decodeNormal(neighbor.xy)),0.0));
                samples+=1.0;
            }
        }
    }
    return 1.0-clamp(occlusion/max(samples,1.0),0.0,1.0)*0.30;
}

void main(){
    vec4 surface=texture(surfaceData,vUv);
    vec3 normal=decodeNormal(surface.xy);
    float ao=screenAo(surface,normal);
    if(surface.z<=0.01||surface.w>=1.9){outEffects=vec4(0.0,0.0,0.0,ao);return;}
    vec2 ndc=vUv*2.0-1.0;
    vec4 farPoint=gi.inverseViewProjection*vec4(ndc,1.0,1.0);
    vec3 ray=normalize(farPoint.xyz/farPoint.w-
        (gi.cameraWorld.xyz-gi.currentWorldOrigin.xyz));
    vec3 world=gi.cameraWorld.xyz+ray*abs(surface.z);
    float coverage=0.0;
    vec4 receiver=texture(receiverAlbedo,vUv);
    vec3 current=traceGi(world,normal,coverage)*receiver.rgb;
    if(receiver.a>0.5)current=vec3(0.0);
    vec3 localPrevious=world-gi.previousWorldOrigin.xyz;
    vec4 previousClip=gi.previousViewProjection*vec4(localPrevious,1.0);
    vec2 previousUv=previousClip.xy/max(previousClip.w,0.0001)*0.5+0.5;
    bool inside=previousClip.w>0.0&&all(greaterThan(previousUv,vec2(0.002)))&&
        all(lessThan(previousUv,vec2(0.998)));
    float weight=0.0;
    vec3 history=current;
    if(inside&&gi.temporal.y>0.5&&coverage>0.05){
        vec4 previousSurface=texture(surfaceData,previousUv);
        vec3 previousNormal=decodeNormal(previousSurface.xy);
        float depthTolerance=max(0.18,abs(surface.z)*0.018);
        bool reject=abs(abs(previousSurface.z)-abs(surface.z))>depthTolerance||
            dot(normal,previousNormal)<0.82;
        if(!reject){
            history=texture(previousHistory,previousUv).rgb;
            vec3 extent=max(vec3(0.03),abs(current)*0.45+vec3(0.04));
            history=clamp(history,current-extent,current+extent);
            weight=gi.temporal.z*coverage;
        }
    }
    vec3 result=mix(current,history,clamp(weight,0.0,0.95));
    int shaftSamples=int(post.screenQuality.z+0.5);
    if(shaftSamples>0&&post.sunScreen.z>0.5){
        vec2 toSun=post.sunScreen.xy-vUv;
        float radial=1.0-smoothstep(0.08,1.15,length(toSun));
        result+=vec3(1.0,0.74,0.42)*radial*post.sunScreen.w*0.035;
    }
    outEffects=vec4(max(result,vec3(0.0)),ao);
}
