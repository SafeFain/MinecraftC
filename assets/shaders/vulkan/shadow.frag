#version 450

layout(location=0) in vec2 atlasUv;
layout(location=1) flat in float alphaCutout;
layout(set=0,binding=0) uniform sampler2D blockAtlas;

void main() {
    if(alphaCutout>0.5&&texture(blockAtlas,atlasUv).a<0.35)discard;
}
