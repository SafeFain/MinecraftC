#pragma once

#include <cstdint>
#include <queue>

struct BlockLightNode { int x=0,y=0,z=0; uint8_t light=0; };

struct LightSample {
    uint8_t sky = 0;
    uint8_t block = 0;
};

struct SmoothLightSample {
    float sky = 0.0f;
    float block = 0.0f;
};

inline constexpr uint8_t packLight(LightSample light) {
    return static_cast<uint8_t>(((light.sky & 0x0f) << 4) | (light.block & 0x0f));
}

inline constexpr LightSample unpackLight(uint8_t packed) {
    return {static_cast<uint8_t>(packed >> 4), static_cast<uint8_t>(packed & 0x0f)};
}

template<typename Passable, typename GetLight, typename SetLight>
void propagateBlockLight(std::queue<BlockLightNode>& queue, Passable&& passable,
                         GetLight&& getLight, SetLight&& setLight) {
    constexpr int directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    while(!queue.empty()) {
        const auto node=queue.front(); queue.pop();
        if(node.light<=1) continue;
        for(const auto& direction:directions) {
            const int x=node.x+direction[0],y=node.y+direction[1],z=node.z+direction[2];
            if(!passable(x,y,z)) continue;
            const uint8_t next=static_cast<uint8_t>(node.light-1);
            if(getLight(x,y,z)>=next) continue;
            setLight(x,y,z,next);
            queue.push({x,y,z,next});
        }
    }
}
