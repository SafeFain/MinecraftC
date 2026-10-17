#pragma once
#include "plugins/HeavenSkywayPolicy.h"
#include "plugins/ContentRegistry.h"
#include <limits>
namespace Plugins {
inline int32_t skywayTarget(const MC_ActionViewV2* v,MC_ActionTargetV2* out,void*){
    if(v->base.dimension!=1||!v->builtin_generation||!v->hit_valid||v->source!=1||v->hit_block!=static_cast<uint16_t>(BlockId::STAR_CRYSTAL))return 0;
    struct Column {bool present=false;int top=0;};bool failed=false;
    auto sample=[&](int x,int z){if(std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000)return Column{};MC_ActionLayer layers[5]{};uint32_t count=0;if(v->layers(v->base.reader,x,z,layers,5,&count)||count!=5){failed=true;return Column{};}return Column{layers[2].present!=0,layers[2].top};};
    const auto spawn=glm::ivec3(v->spawn[0],v->spawn[1],v->spawn[2]);
    const auto origin=HeavenPolicy::findOrigin(spawn,sample);
    const auto cell=[&](int x,int z){return HeavenPolicy::forCell(v->generation.seed,x,z,sample);};
    const glm::ivec3 core(v->hit[0],v->hit[1],v->hit[2]);
    const auto up=HeavenPolicy::destination(core,true,origin,cell),down=HeavenPolicy::destination(core,false,origin,cell);
    if(failed)return -1;
    out->handled=up.has_value()||down.has_value();const auto target=v->sneaking?down:up;
    if(target){out->base.valid=1;for(int i=0;i<3;++i)out->base.position[i]=(*target)[i];}
    return 0;
}
inline int32_t starstepTarget(const MC_ActionViewV2* v,MC_ActionTargetV2* out,void*){
    out->handled=1;if(v->base.dimension!=1||!v->hit_valid)return 0;
    double best=std::numeric_limits<double>::max();
    const auto flags=[&](int x,int y,int z,uint32_t& result){uint16_t block=0;return !v->base.block(v->base.reader,x,y,z,&block)&&!v->block_flags(v->base.reader,block,&result);};
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
        const int x=v->hit[0]+dx,z=v->hit[2]+dz;int top=v->hit[1];uint32_t f=0;
        if(!flags(x,top,z,f)||!(f&1))continue;
        bool loaded=true;while(top+1<Config::WORLD_MAX_Y){if(!flags(x,top+1,z,f)){loaded=false;break;}if(!(f&1))break;++top;}
        const int stand=top+1;
        if(!loaded||stand+1>=Config::WORLD_MAX_Y||!flags(x,stand,z,f)||!(f&2)||!flags(x,stand+1,z,f)||!(f&2))continue;
        const glm::dvec3 candidate(x+.5,stand+.01,z+.5),eye(v->base.eye[0],v->base.eye[1],v->base.eye[2]);
        const double distance=glm::distance(eye,candidate);if(distance>96||distance>=best)continue;
        best=distance;out->base.valid=1;for(int i=0;i<3;++i)out->base.position[i]=candidate[i];
    }
    return 0;
}
}
