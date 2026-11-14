#pragma once
#include "world/RedstoneBlocks.h"

namespace Redstone {
struct WireConnections {uint8_t mask=0,up=0;};
template<class Query>
WireConnections connections(Position p,bool dot,Query&& query) {
    WireConnections result;if(dot)return result;
    const auto above=query(p+Position{0,1,0});
    for(uint8_t f=2;f<6;++f) {
        const auto face=static_cast<Face>(f);const auto q=p+offset(face);
        const auto adjacent=query(q);const auto state=blockState(adjacent);
        bool connected=false;ButtonState button;
        if(decodeButton(adjacent,button))connected=true;
        if(state) {
            switch(state->kind) {
                case Kind::Dust:connected=!state->alternate;break;
                case Kind::Repeater:case Kind::Comparator:connected=state->facing==face || state->facing==opposite(face);break;
                case Kind::Observer:connected=state->facing==opposite(face);break;
                case Kind::Lever:case Kind::Torch:connected=true;break;
                default:break;
            }
        }
        if(!isFullCollisionBlock(above) && blockSupportsFace(adjacent,FaceDir::TOP)) {
            const auto upper=blockState(query(q+Position{0,1,0}));
            if(upper && upper->kind==Kind::Dust && !upper->alternate) {connected=true;result.up|=uint8_t(1u<<(f-2));}
        } else if(blockCollisionBoxes(adjacent).count==0) {
            const auto lower=blockState(query(q+Position{0,-1,0}));
            connected=connected || (lower && lower->kind==Kind::Dust && !lower->alternate);
        }
        if(connected)result.mask|=uint8_t(1u<<(f-2));
    }
    if(!result.mask)result.mask=15;
    else if((result.mask&(result.mask-1))==0) {
        for(uint8_t i=0;i<4;++i)if(result.mask==(1u<<i)){result.mask|=uint8_t(1u<<(i^1u));break;}
    }
    return result;
}
inline BlockCollisionBoxes wireGeometry(WireConnections connections) {
    BlockCollisionBoxes result;auto add=[&](glm::vec3 a,glm::vec3 b){result.boxes[result.count++]={a,b};};
    constexpr float low=.375f,high=.625f,height=.015625f;
    add({low,0,low},{high,height,high});
    if(connections.mask&1)add({low,0,0},{high,height,low});
    if(connections.mask&2)add({low,0,high},{high,height,1});
    if(connections.mask&4)add({high,0,low},{1,height,high});
    if(connections.mask&8)add({0,0,low},{low,height,high});
    if(connections.up&1)add({low,0,0},{high,1,height});
    if(connections.up&2)add({low,0,1-height},{high,1,1});
    if(connections.up&4)add({1-height,0,low},{1,1,high});
    if(connections.up&8)add({0,0,low},{height,1,high});
    return result;
}
} // namespace Redstone
