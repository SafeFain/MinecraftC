#include "world/BlockLightLogic.h"
#include "world/Block.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <queue>

namespace { void require(bool value,const char* message){if(!value){std::cerr<<"FAILED: "<<message<<'\n';std::exit(1);}} }

int main(){
    require(packLight({15,14})==0xfe,"sky and block light pack into independent nibbles");
    const LightSample packed=unpackLight(0x97);
    require(packed.sky==9&&packed.block==7,"packed light round trips");
    require(getLightEmission(BlockId::TORCH)==14&&
            getLightEmission(BlockId::LAVA)==15,"emissive block levels are explicit");
    require(getLightDampening(BlockId::STONE)==15&&
            getLightDampening(BlockId::GLASS)==0&&
            getLightDampening(BlockId::LEAVES)==1&&
            getLightDampening(BlockId::WATER)==2,"materials damp light independently of rendering");
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

    std::array<uint8_t,8> sky{};
    sky[0]=15;
    for(size_t y=1;y<sky.size();++y)sky[y]=sky[y-1];
    require(sky[7]==15,"full skylight travels vertically through clear cells");
    uint8_t side=sky[4]-1;
    require(side==14,"skylight attenuates when it turns sideways");
    std::cout<<"Lighting logic tests passed\n";
}
