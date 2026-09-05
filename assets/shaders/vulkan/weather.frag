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

vec3 heavenPalette(float index){
    vec3 color=vec3(1.0,0.86,0.55);
    if(index<0.5)color=vec3(1.0,0.86,0.55);      // Dawn Meadow
    else if(index<1.5)color=vec3(0.62,0.95,0.55); // Skyroot Grove
    else if(index<2.5)color=vec3(1.0,0.72,0.30);  // Sunstone Heights
    else if(index<3.5)color=vec3(0.45,0.85,1.0);  // Starcrystal Garden
    else if(index<4.5)color=vec3(0.95,0.97,1.0);  // Cloudbloom Fields
    else if(index<5.5)color=vec3(0.78,0.80,0.86); // Skystone Barrens
    else if(index<6.5)color=vec3(0.40,1.0,0.80);  // Glimmer Fen
    else color=vec3(0.85,0.75,1.0);                // Moonpearl Terrace
    return color;
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
    }else if(kind<3.5){
        color=atlasFragment();
        color.a*=0.92;
    }else if(kind<5.5){
        vec2 centered=(uv-vec2(0.5))*vec2(2.0,2.8);
        float radius=length(centered);
        float inner=0.26+phase*0.34;
        float ring=smoothstep(inner-0.09,inner,radius)*
            (1.0-smoothstep(inner+0.02,inner+0.13,radius));
        float crown=(1.0-smoothstep(0.18,0.46,abs(centered.x)))*
            smoothstep(-0.20,0.46,centered.y)*
            (1.0-smoothstep(0.46,0.82,centered.y));
        color=vec4(0.52,0.76,0.94,
            (ring*0.72+crown*0.25)*(1.0-phase));
    }else if(kind<6.5){
        vec2 centered=uv-vec2(0.5);
        float radius=length(centered*vec2(2.0));
        float glow=1.0-smoothstep(0.05,0.72,radius);
        float pulse=0.72+0.28*sin(frame.cameraUpIntensity.w*4.0+phase*6.283);
        vec3 dayColor=vec3(1.0,0.84,0.38),nightColor=vec3(0.40,0.58,1.0);
        vec3 tint=mix(nightColor,dayColor,clamp(textureIndex,0.0,1.0));
        tint=mix(tint,vec3(0.78,0.42,1.0),0.5+0.5*sin(phase*9.0));
        color=vec4(tint,
            glow*pulse*0.72*frame.cameraUpIntensity.w);
    }else if(kind<7.5){
        // Heaven pollen: a soft biome-tinted tuft drifting on the air.
        vec2 centered=uv-vec2(0.5);
        float radius=length(centered*vec2(1.6));
        float blob=1.0-smoothstep(0.10,0.62,radius);
        float pulse=0.82+0.18*sin(frame.cameraUpIntensity.w*3.0+phase*6.283);
        color=vec4(heavenPalette(textureIndex),
            blob*pulse*0.55*frame.cameraUpIntensity.w);
    }else if(kind<8.5){
        // Heaven sparkle: a narrow four-point star with a sharp twinkle.
        vec2 centered=uv-vec2(0.5);
        float diamond=abs(centered.x)+abs(centered.y);
        float square=max(abs(centered.x),abs(centered.y));
        float star=1.0-smoothstep(0.06,0.34,diamond);
        star*=smoothstep(0.12,0.42,square);
        float twinkle=0.5+0.5*sin(frame.cameraUpIntensity.w*6.0+phase*6.283);
        color=vec4(heavenPalette(textureIndex)*1.2,
            star*twinkle*0.85*frame.cameraUpIntensity.w);
    }else if(kind<9.5){
        // Critical hit: a warm four-point impact spark that fades quickly.
        vec2 centered=uv-vec2(0.5);
        float cross=min(abs(centered.x),abs(centered.y));
        float extent=max(abs(centered.x),abs(centered.y));
        float star=(1.0-smoothstep(0.055,0.13,cross))*
            (1.0-smoothstep(0.28,0.49,extent));
        color=vec4(1.0,0.72,0.16,star*(1.0-phase));
    }else if(kind<10.5){
        // Sweep attack: a broad crescent arc around the primary target.
        vec2 centered=(uv-vec2(0.5))*vec2(2.0,1.35);
        float radius=length(centered);
        float arc=smoothstep(0.52,0.64,radius)*
            (1.0-smoothstep(0.76,0.90,radius));
        arc*=smoothstep(-0.50,0.12,centered.y);
        color=vec4(0.92,0.94,1.0,arc*(1.0-phase));
    }else if(kind<11.5){
        vec2 centered=uv-vec2(0.5);
        float mote=1.0-smoothstep(0.08,0.48,length(centered*vec2(1.4)));
        float shimmer=0.78+0.22*sin(frame.cameraUpIntensity.w*2.1+phase*6.283);
        color=vec4(1.0,0.88,0.58,mote*shimmer*0.34);
    }else{
        vec2 centered=uv-vec2(0.5);
        float glow=1.0-smoothstep(0.04,0.52,length(centered*2.0));
        float pulse=0.55+0.45*sin(frame.cameraUpIntensity.w*3.2+phase*6.283);
        color=vec4(0.72,1.0,0.30,glow*pulse*0.70);
    }
    if(color.a<0.01)discard;
    if(frame.atlasParams.y>0.5)
        color.rgb=pow(max(color.rgb,vec3(0.0)),vec3(1.0/2.2));
    outColor=color;
}
