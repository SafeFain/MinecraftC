#pragma once

#include "model/ModelAsset.h"
#include "renderer/RenderDevice.h"
#include <algorithm>
#include <cmath>

namespace model {
inline TextureSamplerDesc textureSampler(const Sampler& source) {
    TextureSamplerDesc result;
    switch(source.minFilter) {
    case 9729: result.minFilter=TextureFilter::Linear; break;
    case 9984: result.minFilter=TextureFilter::NearestMipmapNearest; break;
    case 9985: result.minFilter=TextureFilter::LinearMipmapNearest; break;
    case 9986: result.minFilter=TextureFilter::NearestMipmapLinear; break;
    case 9987: result.minFilter=TextureFilter::LinearMipmapLinear; break;
    default: result.minFilter=TextureFilter::Nearest; break;
    }
    result.magFilter=source.magFilter==9729?TextureFilter::Linear:TextureFilter::Nearest;
    const auto wrap=[](int value) { return value==33071?TextureAddressMode::ClampToEdge:
        value==33648?TextureAddressMode::MirroredRepeat:TextureAddressMode::Repeat; };
    result.addressU=wrap(source.wrapS); result.addressV=wrap(source.wrapT);
    return result;
}

inline TextureData modelTexture(const ImageData& source, const Sampler& sampler) {
    TextureData result;
    result.width=static_cast<uint32_t>(source.width);result.height=static_cast<uint32_t>(source.height);
    result.pixels=source.pixels;
    if(sampler.minFilter<9984 || sampler.minFilter>9987) return result;
    uint32_t width=result.width,height=result.height;
    const std::vector<uint8_t>* previous=&result.pixels;
    const auto linear=[](float v){ return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f); };
    const auto srgb=[](float v){ return v<=.0031308f?v*12.92f:1.055f*std::pow(v,1.0f/2.4f)-.055f; };
    while(width>1 || height>1) {
        TextureData::MipLevel level;
        level.width=std::max(1u,width/2);level.height=std::max(1u,height/2);
        level.pixels.resize(static_cast<size_t>(level.width)*level.height*4);
        for(uint32_t y=0;y<level.height;++y) for(uint32_t x=0;x<level.width;++x) {
            glm::vec3 color(0);float alpha=0;uint32_t samples=0;
            for(uint32_t sy=y*height/level.height;sy<(y+1)*height/level.height;++sy)
                for(uint32_t sx=x*width/level.width;sx<(x+1)*width/level.width;++sx) {
                    const auto offset=(static_cast<size_t>(sy)*width+sx)*4;
                    const float a=(*previous)[offset+3]/255.0f;alpha+=a;++samples;
                    for(int c=0;c<3;++c) color[c]+=linear((*previous)[offset+c]/255.0f)*a;
                }
            const auto offset=(static_cast<size_t>(y)*level.width+x)*4;
            for(int c=0;c<3;++c) level.pixels[offset+c]=static_cast<uint8_t>(std::clamp(std::lround(srgb(alpha>0?color[c]/alpha:0)*255),0l,255l));
            level.pixels[offset+3]=static_cast<uint8_t>(std::lround(alpha/samples*255));
        }
        width=level.width;height=level.height;
        result.mipLevels.push_back(std::move(level));previous=&result.mipLevels.back().pixels;
    }
    return result;
}
} // namespace model
