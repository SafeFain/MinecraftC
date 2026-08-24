#version 450

layout(location=0) in vec4 lighting;
layout(location=1) in vec2 tileUv;
layout(location=2) flat in float tile;
layout(location=3) flat in float face;
layout(location=4) in vec3 worldPosition;
layout(push_constant) uniform FrameUniforms {
    mat4 modelViewProjection;
    vec4 atlasAndLighting;
    vec4 chunkOrigin;
    vec4 tint;
    vec4 lodWorldOrigin;
} frame;
layout(set=0,binding=0) uniform sampler2D blockAtlas;
layout(set=0,binding=1) uniform sampler2D normalAtlas;
layout(set=0,binding=2) uniform sampler2D propertyAtlas;
layout(set=1,binding=0) uniform ChunkEnvironment {
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 directColorIntensity;
    vec4 ambientColorIntensity;
    vec4 fogColorDistance;
    vec4 materialParams;
    vec4 weatherParams;
    mat4 shadowMatrices[4];
    vec4 shadowSplits;
    vec4 shadowOptions;
} environment;
layout(set=1,binding=1) uniform sampler2D shadowMap;
layout(location=0) out vec4 outColor;

float sampleShadowCascade(vec3 position,vec3 normal,int cascade,int count){
    vec4 clip=environment.shadowMatrices[cascade]*vec4(position,1.0);
    vec3 projected=clip.xyz/clip.w;
    projected.xy=projected.xy*0.5+0.5;
    if(projected.z<=0.0||projected.z>=1.0||any(lessThan(projected.xy,vec2(0.0)))||
       any(greaterThan(projected.xy,vec2(1.0))))return 1.0;
    int columns=int(environment.shadowOptions.z+0.5);
    int row=cascade/columns;int column=cascade-row*columns;
    int rows=(count+columns-1)/columns;
    vec2 atlasSize=vec2(columns,rows);
    vec2 uv=(projected.xy+vec2(column,row))/atlasSize;
    float resolution=environment.shadowOptions.y;
    vec2 texel=1.0/(resolution*atlasSize);
    float bias=0.0008+0.002*(1.0-max(dot(normal,
        normalize(environment.lightDirection.xyz)),0.0));
    int taps=count==3?(cascade<2?4:1):count==2&&cascade==0?4:1;
    float visible=0.0;
    if(taps==1)visible=projected.z-bias<=texture(shadowMap,uv).r?1.0:0.0;
    else if(taps==4){
        for(int y=-1;y<=1;y+=2)for(int x=-1;x<=1;x+=2)
            visible+=projected.z-bias<=texture(shadowMap,uv+vec2(x,y)*texel*0.5).r?1.0:0.0;
        visible*=0.25;
    }else{
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
            visible+=projected.z-bias<=texture(shadowMap,uv+vec2(x,y)*texel).r?1.0:0.0;
        visible/=9.0;
    }
    return mix(0.35,1.0,visible);
}

float shadowVisibility(vec3 position,vec3 normal){
    int count=int(environment.shadowOptions.x+0.5);
    if(count==0)return 1.0;
    float cameraDistance=length(position-environment.cameraPosition.xyz);
    int cascade=0;
    if(count>1&&cameraDistance>environment.shadowSplits.x)cascade=1;
    if(count>2&&cameraDistance>environment.shadowSplits.y)cascade=2;
    if(count>3&&cameraDistance>environment.shadowSplits.z)cascade=3;
    float visibility=sampleShadowCascade(position,normal,cascade,count);
    float split=cascade==0?environment.shadowSplits.x:cascade==1?
        environment.shadowSplits.y:cascade==2?environment.shadowSplits.z:
        environment.shadowSplits.w;
    float fade=smoothstep(split*0.88,split,cameraDistance);
    if(cascade+1<count)visibility=mix(visibility,
        sampleShadowCascade(position,normal,cascade+1,count),fade);
    else visibility=mix(visibility,1.0,fade);
    return visibility;
}

vec3 faceNormal(float value) {
    if(value<0.5)return vec3(0.0,1.0,0.0);
    if(value<1.5)return vec3(0.0,-1.0,0.0);
    if(value<2.5)return vec3(0.0,0.0,-1.0);
    if(value<3.5)return vec3(0.0,0.0,1.0);
    if(value<4.5)return vec3(1.0,0.0,0.0);
    if(value<5.5)return vec3(-1.0,0.0,0.0);
    return vec3(0.0,0.55,0.0);
}

vec3 detailNormal(vec3 baseNormal,vec3 tangentNormal,float strength) {
    vec3 tangent = abs(baseNormal.y) > 0.5 ? vec3(1.0, 0.0, 0.0) :
                   abs(baseNormal.x) > 0.5 ? vec3(0.0, 0.0, 1.0) :
                                             vec3(1.0, 0.0, 0.0);
    vec3 bitangent = normalize(cross(baseNormal, tangent));
    tangentNormal.xy*=strength;
    tangentNormal=normalize(tangentNormal);
    return normalize(tangent*tangentNormal.x+bitangent*tangentNormal.y+
        normalize(baseNormal)*tangentNormal.z);
}

float valueNoise(vec2 point) {
    vec2 cell = floor(point);
    vec2 local = fract(point);
    local = local * local * (3.0 - 2.0 * local);
    vec2 hash = fract(sin(vec2(dot(cell, vec2(127.1, 311.7)),
        dot(cell + vec2(1.0), vec2(269.5, 183.3)))) * 43758.5453);
    float a = hash.x;
    float d = hash.y;
    float b = fract(sin(dot(cell + vec2(1.0, 0.0),
        vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(cell + vec2(0.0, 1.0),
        vec2(127.1, 311.7))) * 43758.5453);
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

void main() {
    bool isLod=frame.atlasAndLighting.w>0.0;
    if(isLod){
        float lodDistance=length(worldPosition.xz);
        float inner=frame.atlasAndLighting.w;
        float outer=frame.chunkOrigin.w;
        float innerFade=max(16.0,inner*0.04);
        float outerFade=max(16.0,outer*0.04);
        float innerCoverage=smoothstep(inner-innerFade,inner+innerFade,lodDistance);
        float outerProgress=smoothstep(outer-outerFade,outer+outerFade,lodDistance);
        vec2 localPosition=worldPosition.xz-frame.chunkOrigin.xz;
        vec2 stableWorldPosition=localPosition+frame.lodWorldOrigin.xz;
        float dither=fract(sin(dot(floor(stableWorldPosition),
            vec2(12.9898,78.233)))*43758.5453);
        // Adjacent rings use opposite halves of the same stable threshold.
        // Their union is complete instead of both discarding the same pixels.
        if(dither>innerCoverage||dither<outerProgress)discard;
    }
    float tiles=frame.atlasAndLighting.x;
    float slot=floor(tile+0.5);
    vec2 origin=vec2(mod(slot,tiles),floor(slot/tiles));
    vec2 localPixel=mix(vec2(0.5),vec2(15.5),fract(tileUv));
    vec2 uv=(origin+localPixel/16.0)/tiles;
    vec2 dx=dFdx(tileUv)*(15.0/16.0)/tiles;
    vec2 dy=dFdy(tileUv)*(15.0/16.0)/tiles;
    vec4 texel=textureGrad(blockAtlas,uv,dx,dy);
    vec3 tangentNormal=textureGrad(normalAtlas,uv,dx,dy).xyz*2.0-1.0;
    vec4 properties=textureGrad(propertyAtlas,uv,dx,dy);
    texel.a*=lighting.w;
    if(texel.a<frame.atlasAndLighting.z)discard;
    float packed=floor(fract(tile)*512.0+0.5);
    vec2 flatLight=vec2(floor(packed/16.0),mod(packed,16.0))/15.0;
    vec2 light=mix(flatLight,lighting.yz,frame.atlasAndLighting.y);
    float aoWeight=frame.atlasAndLighting.y*environment.weatherParams.z;
    float ao=mix(1.0,mix(0.52,1.0,clamp(lighting.x,0.0,1.0)),aoWeight);
    float skyLight=pow(clamp(light.x,0.0,1.0),1.20);
    float blockLight=pow(clamp(light.y,0.0,1.0),1.35);
    bool isLava=abs(slot-environment.materialParams.x)<0.25;
    bool isWater=abs(slot-environment.materialParams.y)<0.25;
    vec3 geometricNormal=faceNormal(face);
    float detailStrength=face>5.5?0.30:isWater?0.0:isLava?0.46:1.0;
    vec3 normal=detailNormal(geometricNormal,tangentNormal,
                             detailStrength*environment.materialParams.w);
    if(isWater&&face<0.5){
        float time=environment.weatherParams.x;
        vec2 wave=vec2(sin(worldPosition.x*0.72+time*1.35)+
                       sin(worldPosition.z*1.31-time*0.84),
                       cos(worldPosition.z*0.67-time*1.08)+
                       cos(worldPosition.x*1.17+time*0.73));
        normal=normalize(geometricNormal+vec3(wave.x,0.0,wave.y)*0.055);
    }
    float diffuse=face>5.5?0.32:
        max(dot(normal,normalize(environment.lightDirection.xyz)),0.0);
    float topSurface=max(geometricNormal.y,0.0);
    float wetness=environment.weatherParams.y*skyLight*topSurface;
    float cloudNoise=valueNoise(worldPosition.xz*0.010+
        vec2(environment.weatherParams.x*0.004,0.0));
    cloudNoise=0.58+0.42*cloudNoise;
    float cloudShadow=mix(1.0,cloudNoise,
        environment.weatherParams.w*(0.45+0.55*environment.weatherParams.y));
    float visibility=(isLod?1.0:shadowVisibility(worldPosition,normal))*cloudShadow;
    vec3 illumination=environment.ambientColorIntensity.rgb*
        environment.ambientColorIntensity.a*skyLight*0.72;
    illumination+=environment.directColorIntensity.rgb*
        environment.directColorIntensity.a*diffuse*skyLight*0.52*visibility;
    illumination=max(illumination,vec3(1.0,0.72,0.38)*blockLight*1.15);
    illumination=max(illumination*ao,vec3(0.025));

    if(isLava||properties.b>0.01)
        illumination=max(illumination,vec3(2.15,0.72,0.16)*
            max(properties.b,isLava?1.0:0.0));

    float roughness=isWater?0.08:mix(properties.r,0.82,step(5.5,face));
    roughness=mix(roughness,0.16,wetness*0.82);
    if(face<5.5){
        vec3 viewDir=normalize(environment.cameraPosition.xyz-worldPosition);
        vec3 halfDir=normalize(viewDir+normalize(environment.lightDirection.xyz));
        float exponent=mix(128.0,10.0,roughness*roughness);
        float fresnel=0.04+(1.0-0.04)*pow(1.0-
            max(dot(normal,viewDir),0.0),5.0);
        float specular=pow(max(dot(normal,halfDir),0.0),exponent)*
            mix(0.16,1.0,fresnel)*(1.0-roughness*0.45);
        illumination+=environment.directColorIntensity.rgb*specular*
            skyLight*visibility*(isWater?0.82:0.28);
    }

    vec3 albedo=texel.rgb*mix(1.0,0.78,wetness);
    if(isWater)albedo=mix(albedo,vec3(0.055,0.26,0.34),0.16);
    vec3 color=albedo*frame.tint.rgb*illumination;
    float distanceToCamera=length(worldPosition-environment.cameraPosition.xyz);
    float fogStart=environment.fogColorDistance.a*environment.materialParams.z;
    float fogRange=max(environment.fogColorDistance.a-fogStart,1.0);
    float fogCoordinate=max(distanceToCamera-fogStart,0.0)/fogRange;
    float fog=1.0-exp(-3.0*fogCoordinate*fogCoordinate);
    vec3 localFog=mix(environment.ambientColorIntensity.rgb*
        environment.ambientColorIntensity.a*0.34,
        environment.fogColorDistance.rgb,skyLight);
    color=mix(color,localFog,fog);
    outColor=vec4(color,texel.a*frame.tint.a);
}
