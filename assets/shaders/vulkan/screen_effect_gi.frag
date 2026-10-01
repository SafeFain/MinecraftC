#version 450
#extension GL_GOOGLE_include_directive : require

layout(set=0,binding=0) uniform sampler2D surfaceData;
layout(set=0,binding=1) uniform sampler2D previousHistory;
layout(set=0,binding=2) uniform sampler3D irradiance0;
layout(set=0,binding=3) uniform sampler3D irradiance1;
layout(set=0,binding=4) uniform sampler3D irradiance2;
layout(set=0,binding=5) uniform sampler3D irradiance3;
layout(set=0,binding=6) uniform sampler2D receiverAlbedo;
layout(set=0,binding=7) uniform sampler2D previousSurfaceData;
layout(set=0,binding=10) uniform GiScreenUniforms {
    mat4 inverseViewProjection;
    mat4 previousViewProjection;
    vec4 cameraWorld;
    vec4 previousCameraWorld;
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

#include "gi_surface_filter.glsl"

// Validity stays discrete at mip zero. Never interpolate the -1 sentinel
// with opacity, or wrap a filter footprint across a world-space clipmap edge.
vec4 fetchCell(int level,ivec3 cell){
    ivec3 minimum=ivec3(gi.minimumCellAndSize[level].xyz);
    int resolution=int(gi.config.x);
    if(any(lessThan(cell,minimum))||
       any(greaterThanEqual(cell,minimum+ivec3(resolution))))return vec4(0,0,0,-1);
    ivec3 ring=ivec3(mod(vec3(cell),gi.config.x));
    if(level==0)return texelFetch(irradiance0,ring,0);
    if(level==1)return texelFetch(irradiance1,ring,0);
    if(level==2)return texelFetch(irradiance2,ring,0);
    return texelFetch(irradiance3,ring,0);
}

vec4 sampleClipmap(vec3 world,out int selectedLevel){
    int count=int(gi.config.y+0.5);
    selectedLevel=-1;
    for(int level=0;level<4;++level){
        if(level>=count)break;
        ivec3 cell=ivec3(floor(world/gi.minimumCellAndSize[level].w));
        vec4 value=fetchCell(level,cell);
        if(value.a>=0.0){selectedLevel=level;return value;}
    }
    return vec4(0,0,0,-1);
}

vec3 filteredRadiance(int level,vec3 world,float diameter,vec3 direction){
    float cellSize=gi.minimumCellAndSize[level].w;
    vec3 cell=world/cellSize-0.5;
    ivec3 base=ivec3(floor(cell));
    // A bounded local footprint replaces coarse toroidal mip sampling.
    // Wider cones soften the local lobe without importing remote ring cells.
    vec3 fraction=mix(fract(cell),vec3(0.5),
        clamp(diameter/cellSize-1.0,0.0,1.0));
    vec3 sum=vec3(0);
    float weight=0.0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec3 offset=ivec3(x,y,z);
        vec3 factors=mix(vec3(1.0)-fraction,fraction,vec3(offset));
        float w=factors.x*factors.y*factors.z;
        vec4 value=fetchCell(level,base+offset);
        // The forward half-space can lie behind the first opaque intersection.
        vec3 delta=(vec3(base+offset)+0.5)*cellSize-world;
        bool currentCell=all(equal(base+offset,ivec3(floor(world/cellSize))));
        if(value.a<0.0||(!currentCell&&dot(delta,direction)>cellSize*0.01))continue;
        sum+=value.rgb*w;
        weight+=w;
    }
    return sum/max(weight,0.0001);
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
    int budget=int(gi.config.w+0.5)*32;
    float maximumDistance=gi.temporal.w;
    // Stable world-space sampling avoids time-driven flicker on static surfaces.
    float rotation=fract(sin(dot(floor(world),vec3(12.9898,78.233,37.719)))*43758.5453);
    vec3 total=vec3(0.0);
    validCoverage=0.0;
    for(int cone=0;cone<6;++cone){
        if(cone>=cones)break;
        vec3 direction=coneDirection(normal,cone,cones,rotation);
        vec3 origin=world+normal*0.08;
        float travel=0.02;
        float transmittance=1.0;
        vec3 accumulated=vec3(0.0);
        float knownDistance=0.0;
        for(int stepIndex=0;stepIndex<256;++stepIndex){
            if(stepIndex>=budget||travel>maximumDistance||transmittance<0.02)break;
            vec3 position=origin+direction*travel;
            int level;
            vec4 value=sampleClipmap(position,level);
            // Try the coarser valid levels first; unknown space then ends the ray.
            // It neither spends radiance samples nor creates a fictitious occluder.
            if(level<0)break;
            float cellSize=gi.minimumCellAndSize[level].w;
            float opacity=clamp(value.a,0.0,1.0);
            if(opacity>0.0){
                float diameter=max(cellSize,travel*0.58);
                accumulated+=filteredRadiance(level,position,diameter,direction)*
                    transmittance*opacity;
                transmittance*=1.0-opacity;
            }
            // Visit each intersected cell instead of exponentially jumping over walls.
            vec3 cell=floor(position/cellSize);
            vec3 boundary=(cell+step(vec3(0.0),direction))*cellSize;
            vec3 crossing=vec3(1e20);
            for(int axis=0;axis<3;++axis)
                if(abs(direction[axis])>0.00001)
                    crossing[axis]=(boundary[axis]-position[axis])/direction[axis];
            travel+=max(min(crossing.x,min(crossing.y,crossing.z)),0.0)+0.001;
            knownDistance=min(travel,maximumDistance);
        }
        total+=accumulated;
        // A fully occluded ray is trustworthy even when short. An open ray
        // ending in unknown space or at its step budget has partial coverage.
        validCoverage+=1.0-transmittance*(1.0-knownDistance/maximumDistance);
    }
    validCoverage/=max(float(cones),1.0);
    return total/max(float(cones),1.0)*gi.temporal.x;
}

#include "screen_effect_common.glsl"

void main(){
    ivec2 surfaceSize=textureSize(surfaceData,0);
    ivec2 surfacePixel=clamp(ivec2(vUv*vec2(surfaceSize)),ivec2(0),surfaceSize-1);
    vec2 receiverUv=(vec2(surfacePixel)+0.5)/vec2(surfaceSize);
    vec4 surface=texelFetch(surfaceData,surfacePixel,0);
    vec3 normal=decodeNormal(surface.xy);
    float ao=screenAo(surface,normal);
    if(surface.z<=0.01||surface.w>=1.9){outEffects=vec4(0.0,0.0,0.0,ao);return;}
    vec2 ndc=receiverUv*2.0-1.0;
    vec4 farPoint=gi.inverseViewProjection*vec4(ndc,1.0,1.0);
    vec3 ray=normalize(farPoint.xyz/farPoint.w-
        (gi.cameraWorld.xyz-gi.currentWorldOrigin.xyz));
    vec3 world=gi.cameraWorld.xyz+ray*abs(surface.z);
    float coverage=0.0;
    vec4 receiver=texelFetch(receiverAlbedo,surfacePixel,0);
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
        float previousDistance=length(world-gi.previousCameraWorld.xyz);
        ivec2 size=textureSize(previousHistory,0);
        vec2 pixel=previousUv*vec2(size)-0.5;
        ivec2 base=ivec2(floor(pixel));
        vec2 fraction=fract(pixel);
        vec3 sum=vec3(0);
        float accepted=0.0;
        for(int y=0;y<2;++y)for(int x=0;x<2;++x){
            ivec2 tap=base+ivec2(x,y);
            if(any(lessThan(tap,ivec2(0)))||any(greaterThanEqual(tap,size)))continue;
            vec4 previousSurface=texelFetch(previousSurfaceData,tap,0);
            vec2 spatial=mix(vec2(1)-fraction,fraction,vec2(x,y));
            float w=spatial.x*spatial.y*
                giSurfaceWeight(surface,previousSurface,previousDistance,0.018);
            sum+=texelFetch(previousHistory,tap,0).rgb*w;
            accepted+=w;
        }
        if(accepted>0.01){
            history=sum/accepted;
            float change=length(history-current)/max(length(current)+0.03,0.03);
            vec3 extent=max(vec3(0.03),abs(current)*0.45+vec3(0.04));
            history=clamp(history,current-extent,current+extent);
            weight=gi.temporal.z*coverage*min(accepted,1.0)*
                (1.0-smoothstep(0.15,0.60,change));
        }
    }
    vec3 result=mix(current,history,clamp(weight,0.0,0.999));
    outEffects=vec4(max(result,vec3(0.0)),ao);
}
