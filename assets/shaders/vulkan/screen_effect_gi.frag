#version 450
#extension GL_GOOGLE_include_directive : require

layout(set=0,binding=0) uniform sampler2D surfaceData;
layout(set=0,binding=1) uniform sampler2D previousHistory;
layout(set=0,binding=6) uniform sampler2D receiverAlbedo;
layout(set=0,binding=7) uniform sampler2D previousSurfaceData;
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

#include "voxel_gi_trace.glsl"

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
    vec4 receiver=texelFetch(receiverAlbedo,surfacePixel,0);
    if(receiver.a>0.5){outEffects=vec4(0,0,0,ao);return;}
    vec3 localPrevious=world-gi.previousWorldOrigin.xyz;
    vec4 previousClip=gi.previousViewProjection*vec4(localPrevious,1.0);
    vec2 previousUv=previousClip.xy/max(previousClip.w,0.0001)*0.5+0.5;
    bool inside=previousClip.w>0.0&&all(greaterThan(previousUv,vec2(0.002)))&&
        all(lessThan(previousUv,vec2(0.998)));
    float weight=0.0;
    vec3 history=vec3(0);
    float historyConfidence=0.0;
    if(inside&&gi.temporal.y>0.5&&gi.temporal.z>0.0){
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
            historyConfidence=min(accepted,1.0);
        }
    }
    ivec2 effectPixel=ivec2(gl_FragCoord.xy);
    // Only stationary reprojected receivers skip tracing. Fresh/moving pixels
    // always evaluate all cones, including disocclusions and revision changes.
    // Neighboring fragment lanes take the same branch; a pixel checkerboard
    // leaves half of every wave idle while still executing the full trace.
    ivec2 refreshTile=effectPixel/max(int(gi.traversal.z),1);
    bool refresh=((refreshTile.x+refreshTile.y+int(gi.sampling.x))&1)==0;
    vec2 drift=(previousUv-receiverUv)*vec2(textureSize(previousHistory,0));
    if(inside&&gi.sampling.y>0.5&&!refresh&&length(drift)<0.25){
        ivec2 size=textureSize(previousHistory,0);
        ivec2 tap=clamp(ivec2(previousUv*vec2(size)),ivec2(0),size-1);
        vec4 guide=texelFetch(previousSurfaceData,tap,0);
        if(giSurfaceWeight(surface,guide,length(world-gi.previousCameraWorld.xyz),0.008)>0.98){
            outEffects=vec4(max(texelFetch(previousHistory,tap,0).rgb,vec3(0)),gi.traversal.y>0.5?0.0:ao);return;
        }
    }
    float coverage=0.0;
    vec3 current=traceGi(world,normal,coverage)*receiver.rgb;
    if(historyConfidence>0.01&&coverage>0.05){
        float change=length(history-current)/max(length(current)+0.03,0.03);
        vec3 extent=max(vec3(0.03),abs(current)*0.45+vec3(0.04));
        history=clamp(history,current-extent,current+extent);
        weight=gi.temporal.z*coverage*historyConfidence*
            (1.0-smoothstep(0.15,0.60,change));
    }
    vec3 result=mix(current,history,clamp(weight,0.0,0.999));
    outEffects=vec4(max(result,vec3(0.0)),gi.traversal.y>0.5?1.0:ao);
}
