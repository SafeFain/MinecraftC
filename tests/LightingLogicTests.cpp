#include "world/BlockLightLogic.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <queue>

namespace { void require(bool value,const char* message){if(!value){std::cerr<<"FAILED: "<<message<<'\n';std::exit(1);}} }

int main(){
    std::array<uint8_t,40> light{};
    std::array<bool,40> blocked{};
    std::queue<BlockLightNode> queue;
    light[15]=14;queue.push({15,0,0,14});
    propagateBlockLight(queue,
        [&](int x,int y,int z){return y==0&&z==0&&x>=0&&x<40&&!blocked[x];},
        [&](int x,int,int){return light[x];},
        [&](int x,int,int,uint8_t value){light[x]=value;});
    require(light[16]==13&&light[17]==12,"light crosses the 15/16 chunk boundary");
    require(light[28]==1&&light[29]==0,"torch light stops after fourteen levels");
    light.fill(0);blocked[16]=true;light[15]=14;queue.push({15,0,0,14});
    propagateBlockLight(queue,
        [&](int x,int y,int z){return y==0&&z==0&&x>=0&&x<40&&!blocked[x];},
        [&](int x,int,int){return light[x];},
        [&](int x,int,int,uint8_t value){light[x]=value;});
    require(light[16]==0&&light[17]==0,"opaque boundary blocks propagation");
    std::cout<<"Lighting logic tests passed\n";
}
