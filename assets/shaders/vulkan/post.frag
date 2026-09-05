#version 450

layout(set=0,binding=0) uniform sampler2D sceneColor;
layout(set=0,binding=1) uniform sampler2D bloomHalf;
layout(set=0,binding=2) uniform sampler2D bloomQuarter;
layout(set=0,binding=3) uniform sampler2D bloomEighth;
layout(set=0,binding=4) uniform sampler2D bloomSixteenth;
layout(location=0) in vec2 vUv;
layout(location=0) out vec4 outColor;

layout(push_constant) uniform PostConstants {
    vec4 exposureBloom;
    vec4 effects;
    vec4 texelTime;
    vec4 environment;
    vec4 celestial;
} post;

vec3 pbrNeutral(vec3 color){
    const float startCompression=0.8-0.04;
    const float desaturation=0.10;
    float x=min(color.r,min(color.g,color.b));
    float offset=x<0.08?x-6.25*x*x:0.04;
    color-=offset;
    float peak=max(color.r,max(color.g,color.b));
    if(peak<startCompression)return max(color,vec3(0.0));
    float newPeak=1.0-(1.0-startCompression)*(1.0-startCompression)/
        (peak+1.0-2.0*startCompression);
    color*=newPeak/peak;
    float amount=1.0-1.0/(desaturation*(peak-newPeak)+1.0);
    return mix(color,vec3(newPeak),amount);
}

vec3 brightSample(vec2 uv){
    vec3 color=texture(sceneColor,clamp(uv,vec2(0.0),vec2(1.0))).rgb;
    float peak=max(color.r,max(color.g,color.b));
    float weight=smoothstep(0.78,1.35,peak);
    return color*weight;
}

void main(){
    vec2 uv=vUv;
    float underwater=post.effects.x;
    if(underwater>0.001){
        vec2 wave=vec2(sin(uv.y*48.0+post.texelTime.z*1.7),
                       cos(uv.x*41.0-post.texelTime.z*1.3));
        uv+=wave*post.texelTime.xy*2.2*underwater;
    }
    vec3 hdr=texture(sceneColor,clamp(uv,vec2(0.0),vec2(1.0))).rgb;
    float bloomStrength=post.exposureBloom.y;
    int bloomLevels=int(post.texelTime.w+0.5);
    if(post.effects.w>0.001&&bloomLevels>0){
        vec3 bloom=texture(bloomHalf,vUv).rgb*0.45;
        if(bloomLevels>1)bloom+=texture(bloomQuarter,vUv).rgb*0.30;
        if(bloomLevels>2)bloom+=texture(bloomEighth,vUv).rgb*0.17;
        if(bloomLevels>3)bloom+=texture(bloomSixteenth,vUv).rgb*0.08;
        hdr+=bloom*bloomStrength;
    }else if(bloomStrength>0.0){
        vec2 texel=post.texelTime.xy*post.exposureBloom.z;
        vec3 bloom=brightSample(uv)*0.18;
        const vec2 directions[12]=vec2[12](
            vec2(1,0),vec2(-1,0),vec2(0,1),vec2(0,-1),
            vec2(.707,.707),vec2(-.707,.707),vec2(.707,-.707),vec2(-.707,-.707),
            vec2(2,0),vec2(-2,0),vec2(0,2),vec2(0,-2));
        int taps=int(post.exposureBloom.w+0.5);
        for(int i=0;i<12;++i)if(i<taps)
            bloom+=brightSample(uv+directions[i]*texel)/(float(taps)+5.0);
        hdr+=bloom*bloomStrength;
    }
    vec3 color=hdr*post.exposureBloom.x;
    if(underwater>0.001){
        float edge=1.0-smoothstep(0.28,0.72,length(vUv-0.5));
        vec3 waterTint=vec3(0.055,0.31,0.42);
        color=mix(color,waterTint*(0.55+dot(color,vec3(.2126,.7152,.0722))),
                  underwater*(0.30+0.22*(1.0-edge)));
    }
    float hurt=post.effects.y;
    if(hurt>0.001){
        float edge=smoothstep(0.24,0.72,length(vUv-0.5));
        color=mix(color,vec3(max(color.r,0.28),color.g*0.72,color.b*0.72),
                  edge*hurt*0.32);
    }
    // Rain slightly compresses saturation without tinting UI, which is drawn
    // after this pass.
    float luminance=dot(color,vec3(.2126,.7152,.0722));
    color=mix(color,vec3(luminance),post.environment.x*0.06);
    float enhanced=post.effects.w;
    if(enhanced>0.001){
        float twilight=(1.0-smoothstep(0.08,0.42,abs(post.celestial.y)))*
            (1.0-post.environment.x);
        vec3 warm=vec3(1.025,1.005,0.965);
        vec3 cool=vec3(0.975,1.005,1.030);
        vec3 grade=mix(vec3(1.0),warm,twilight*0.72);
        float storm=max(post.environment.x,post.environment.y);
        grade=mix(grade,cool,(1.0-post.environment.w)*0.32+storm*0.28);
        grade=mix(grade,vec3(1.015,1.018,1.025),
            post.environment.z*0.22);
        grade=mix(grade,vec3(1.018,1.008,0.982),post.celestial.w*0.36);
        color*=mix(vec3(1.0),grade,enhanced);
        luminance=dot(color,vec3(.2126,.7152,.0722));
        float saturation=0.06*enhanced*(1.0-post.environment.x*0.5);
        color=mix(vec3(luminance),color,1.0+saturation);
    }
    color=pbrNeutral(max(color,vec3(0.0)));
    if(enhanced>0.001){
        float edge=smoothstep(0.34,0.76,length(vUv-0.5));
        float feedback=max(underwater,hurt);
        color*=1.0-edge*0.05*enhanced*(1.0-feedback);
    }
    if(post.effects.z>0.5)color=pow(color,vec3(1.0/2.2));
    outColor=vec4(color,1.0);
}
