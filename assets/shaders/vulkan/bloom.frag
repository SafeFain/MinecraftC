#version 450

layout(set=0,binding=0) uniform sampler2D sourceColor;
layout(location=0) in vec2 vUv;
layout(location=0) out vec4 outColor;

layout(push_constant) uniform BloomConstants {
    vec4 sourceTexelAndExtract;
} bloom;

void main(){
    vec2 texel=bloom.sourceTexelAndExtract.xy;
    vec3 color=texture(sourceColor,vUv).rgb*0.28;
    color+=(texture(sourceColor,vUv+vec2(texel.x,0.0)).rgb+
            texture(sourceColor,vUv-vec2(texel.x,0.0)).rgb+
            texture(sourceColor,vUv+vec2(0.0,texel.y)).rgb+
            texture(sourceColor,vUv-vec2(0.0,texel.y)).rgb)*0.12;
    color+=(texture(sourceColor,vUv+texel).rgb+
            texture(sourceColor,vUv-texel).rgb+
            texture(sourceColor,vUv+vec2(texel.x,-texel.y)).rgb+
            texture(sourceColor,vUv+vec2(-texel.x,texel.y)).rgb)*0.06;
    if(bloom.sourceTexelAndExtract.z>0.5){
        float peak=max(color.r,max(color.g,color.b));
        float soft=clamp((peak-0.68)/0.42,0.0,1.0);
        soft=soft*soft*(3.0-2.0*soft);
        float contribution=max(peak-0.88,0.0)+soft*0.24;
        color*=contribution/max(peak,0.0001);
    }
    outColor=vec4(color,1.0);
}
