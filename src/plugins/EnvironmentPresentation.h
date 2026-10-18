#pragma once
#include "plugins/ContentRegistry.h"
#include "renderer/RenderEnvironment.h"
#include <algorithm>
#include <cmath>

namespace Plugins {
inline MC_EnvironmentParameters environmentParameters(const RenderEnvironment& e,uint32_t dim){
    MC_EnvironmentParameters p{};p.size=sizeof(p);p.dimension=dim;p.sky_style=static_cast<uint32_t>(e.skyStyle);p.clouds=1;p.day_phase=e.dayPhase;p.daylight=e.daylight;p.star_intensity=e.starIntensity;p.rain=e.rainIntensity;p.thunder=e.thunderIntensity;p.lightning=e.lightningFlash;p.sun_size=e.sunSize;p.moon_size=e.moonSize;
    auto copy=[](float* out,const glm::vec3& v){for(int i=0;i<3;++i)out[i]=v[i];};copy(p.sun_direction,e.sunDirection);copy(p.moon_direction,e.moonDirection);copy(p.light_direction,e.lightDirection);copy(p.colors.zenith,e.zenithColor);copy(p.colors.horizon,e.horizonColor);copy(p.colors.fog,e.fogColor);copy(p.colors.ambient_color,e.ambientColor);copy(p.colors.direct_color,e.directColor);p.colors.ambient_intensity=e.ambientIntensity;p.colors.direct_intensity=e.directIntensity;return p;
}
inline bool validEnvironment(const MC_EnvironmentParameters& p){
    if(p.size<sizeof(p)||p.sky_style>1||p.clouds>1)return false;
    auto bounded=[](float value,float lo,float hi){return std::isfinite(value)&&value>=lo&&value<=hi;};
    for(float v:{p.day_phase,p.daylight,p.star_intensity,p.rain,p.thunder,p.lightning})if(!bounded(v,0,1))return false;
    for(float v:{p.sun_size,p.moon_size})if(!bounded(v,0,8))return false;
    for(const auto* values:{p.colors.zenith,p.colors.horizon,p.colors.fog,p.colors.ambient_color,p.colors.direct_color})for(int i=0;i<3;++i)if(!bounded(values[i],0,16))return false;
    if(!bounded(p.colors.ambient_intensity,0,16)||!bounded(p.colors.direct_intensity,0,16))return false;
    for(const auto* values:{p.sun_direction,p.moon_direction,p.light_direction}){double length=0;for(int i=0;i<3;++i){if(!bounded(values[i],-1,1))return false;length+=double(values[i])*values[i];}if(length<.000001)return false;}
    return true;
}
inline RenderEnvironment applyEnvironment(RenderEnvironment e,uint32_t dim,bool& clouds){
    const auto found=content().extensions.environments.find(dim);if(found==content().extensions.environments.end())return e;
    const auto& profile=found->second;e.particleIntensity=profile.particleIntensity;e.skyFlags=profile.skyFlags;e.gradeStrength=profile.gradeStrength;e.gradeColor={profile.gradeColor[0],profile.gradeColor[1],profile.gradeColor[2]};for(size_t i=0;i<e.skyLayers.size();++i)for(int j=0;j<4;++j)e.skyLayers[i][j]=profile.skyLayers[i][size_t(j)];auto p=environmentParameters(e,dim);const auto& base=profile.parameters;p.sky_style=base.sky_style;p.clouds=base.clouds;p.sun_size=base.sun_size;p.moon_size=base.moon_size;
    if(profile.overrides&1)p.colors=base.colors;
    if(profile.overrides&2){p.day_phase=base.day_phase;p.daylight=base.daylight;p.star_intensity=base.star_intensity;std::copy(std::begin(base.sun_direction),std::end(base.sun_direction),p.sun_direction);std::copy(std::begin(base.moon_direction),std::end(base.moon_direction),p.moon_direction);std::copy(std::begin(base.light_direction),std::end(base.light_direction),p.light_direction);}
    if(profile.overrides&4){p.rain=base.rain;p.thunder=base.thunder;p.lightning=base.lightning;}
    if(profile.callback)profile.callback(p);
    if(!validEnvironment(p)||p.dimension!=dim){content().runtimeFault="Invalid environment output: "+profile.key;return e;}
    auto v=[](const float* p){return glm::vec3(p[0],p[1],p[2]);};e.skyStyle=static_cast<RenderSkyStyle>(p.sky_style);clouds=clouds&&p.clouds;e.sunSize=p.sun_size;e.moonSize=p.moon_size;e.dayPhase=p.day_phase;e.daylight=p.daylight;e.starIntensity=p.star_intensity;e.sunDirection=glm::normalize(v(p.sun_direction));e.moonDirection=glm::normalize(v(p.moon_direction));e.lightDirection=glm::normalize(v(p.light_direction));e.zenithColor=v(p.colors.zenith);e.horizonColor=v(p.colors.horizon);e.fogColor=v(p.colors.fog);e.ambientColor=v(p.colors.ambient_color);e.directColor=v(p.colors.direct_color);e.ambientIntensity=p.colors.ambient_intensity;e.directIntensity=p.colors.direct_intensity;e.rainIntensity=p.rain;e.thunderIntensity=p.thunder;e.lightningFlash=p.lightning;return e;
}
inline uint16_t materialTile(const std::string& key){const auto found=content().materials.find(key);if(found!=content().materials.end())return found->second.tile;for(uint16_t i=0;i<static_cast<uint16_t>(BlockTexture::Count);++i)if(key==getBlockTextureAssetName(static_cast<BlockTexture>(i)))return static_cast<uint16_t>(getAtlasTextureIndex(static_cast<BlockTexture>(i)));throw std::runtime_error("Unknown particle material");}
} // namespace Plugins
