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
} frame;
layout(set=0,binding=0) uniform sampler2D blockAtlas;
layout(set=1,binding=0) uniform ChunkEnvironment {
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 directColorIntensity;
    vec4 ambientColorIntensity;
    vec4 fogColorDistance;
    vec4 materialParams;
    mat4 shadowMatrices[4];
    vec4 shadowSplits;
    vec4 shadowOptions;
} environment;
layout(set=1,binding=1) uniform sampler2D shadowMap;
layout(location=0) out vec4 outColor;

float shadowVisibility(vec3 position,vec3 normal){
    int count=int(environment.shadowOptions.x+0.5);
    if(count==0)return 1.0;
    float cameraDistance=length(position-environment.cameraPosition.xyz);
    int cascade=0;
    if(count>1&&cameraDistance>environment.shadowSplits.x)cascade=1;
    if(count>2&&cameraDistance>environment.shadowSplits.y)cascade=2;
    if(count>3&&cameraDistance>environment.shadowSplits.z)cascade=3;
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
    float visible=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
        visible+=projected.z-bias<=texture(shadowMap,uv+vec2(x,y)*texel).r?1.0:0.0;
    visible/=9.0;
    float split=cascade==0?environment.shadowSplits.x:cascade==1?
        environment.shadowSplits.y:cascade==2?environment.shadowSplits.z:
        environment.shadowSplits.w;
    float fade=smoothstep(split*0.90,split,cameraDistance);
    return mix(mix(0.35,1.0,visible),1.0,fade);
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

void main() {
    float tiles=frame.atlasAndLighting.x;
    float slot=floor(tile+0.5);
    vec2 origin=vec2(mod(slot,tiles),floor(slot/tiles));
    vec2 local=mix(vec2(0.5/16.0),vec2(15.5/16.0),fract(tileUv));
    vec2 uv=(origin+local)/tiles;
    vec2 dx=dFdx(tileUv)*(15.0/16.0)/tiles;
    vec2 dy=dFdy(tileUv)*(15.0/16.0)/tiles;
    vec4 texel=textureGrad(blockAtlas,uv,dx,dy);
    texel.a*=lighting.w;
    if(texel.a<frame.atlasAndLighting.z)discard;
    float packed=floor(fract(tile)*512.0+0.5);
    vec2 flatLight=vec2(floor(packed/16.0),mod(packed,16.0))/15.0;
    vec2 light=mix(flatLight,lighting.yz,frame.atlasAndLighting.y);
    float ao=mix(1.0,mix(0.42,1.0,clamp(lighting.x,0.0,1.0)),frame.atlasAndLighting.y);
    float skyLight=pow(clamp(light.x,0.0,1.0),1.35);
    float blockLight=pow(clamp(light.y,0.0,1.0),1.35);
    vec3 normal=faceNormal(face);
    float diffuse=face>5.5?0.32:
        max(dot(normal,normalize(environment.lightDirection.xyz)),0.0);
    vec3 illumination=environment.ambientColorIntensity.rgb*
        environment.ambientColorIntensity.a*skyLight*0.68;
    illumination+=environment.directColorIntensity.rgb*
        environment.directColorIntensity.a*diffuse*skyLight*0.46*
        shadowVisibility(worldPosition,normal);
    illumination=max(illumination,vec3(1.0,0.72,0.38)*blockLight*1.15);
    illumination=max(illumination*ao,vec3(0.025));

    if(abs(slot-environment.materialParams.x)<0.25)
        illumination=max(illumination,vec3(1.15,0.52,0.16));

    if(abs(slot-environment.materialParams.y)<0.25&&face<0.5){
        vec3 viewDir=normalize(environment.cameraPosition.xyz-worldPosition);
        vec3 halfDir=normalize(viewDir+normalize(environment.lightDirection.xyz));
        float sparkle=pow(max(dot(normal,halfDir),0.0),48.0);
        illumination+=environment.directColorIntensity.rgb*sparkle*
            lighting.y*0.24;
    }

    vec3 color=texel.rgb*frame.tint.rgb*illumination;
    float distanceToCamera=length(worldPosition-environment.cameraPosition.xyz);
    float fog=smoothstep(environment.fogColorDistance.a*
        environment.materialParams.z,environment.fogColorDistance.a,
        distanceToCamera);
    vec3 localFog=mix(environment.ambientColorIntensity.rgb*
        environment.ambientColorIntensity.a*0.34,
        environment.fogColorDistance.rgb,skyLight);
    color=mix(color,localFog,fog);
    if(environment.materialParams.w>0.5)
        color=pow(max(color,vec3(0.0)),vec3(1.0/2.2));
    outColor=vec4(color,texel.a*frame.tint.a);
}
