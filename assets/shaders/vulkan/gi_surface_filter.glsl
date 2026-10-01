// Surface metadata is discrete. Filtering depth/normals across silhouettes
// invents receivers in empty space; only radiance is interpolated.
bool giReceiver(vec4 surface){return surface.z>0.01&&surface.w<1.9;}

float giSurfaceWeight(vec4 reference,vec4 candidate,float expectedDistance,
                      float relativeTolerance){
    if(!giReceiver(reference)||!giReceiver(candidate))return 0.0;
    float alignment=dot(decodeNormal(reference.xy),decodeNormal(candidate.xy));
    float tolerance=max(0.18,expectedDistance*relativeTolerance);
    float difference=abs(candidate.z-expectedDistance);
    if(alignment<0.82||difference>tolerance||
       abs(reference.w-candidate.w)>0.25)return 0.0;
    return pow(max(alignment,0.0),16.0)*exp(-difference/tolerance*2.0);
}

ivec2 giSurfacePixel(ivec2 effectPixel,ivec2 effectSize){
    ivec2 size=textureSize(surfaceData,0);
    vec2 uv=(vec2(effectPixel)+0.5)/vec2(effectSize);
    return clamp(ivec2(uv*vec2(size)),ivec2(0),size-1);
}
