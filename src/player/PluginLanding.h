#pragma once
#include "world/World.h"
#include "plugins/ContentRegistry.h"
#include <algorithm>
#include <cmath>
#include <vector>

// A landing needs resident clear space and coverage of the entire foot rectangle.
inline bool pluginSafeLanding(const World& world, const glm::dvec3& p) {
    for (int axis=0;axis<3;++axis)
        if (!std::isfinite(p[axis]) || std::abs(p[axis])>30000000) return false;
    if (p.y<Config::WORLD_MIN_Y || p.y+Config::PLAYER_HEIGHT>=Config::WORLD_MAX_Y) return false;
    const glm::dvec3 lo=p-glm::dvec3(Config::PLAYER_WIDTH/2,0,Config::PLAYER_WIDTH/2);
    const glm::dvec3 hi=p+glm::dvec3(Config::PLAYER_WIDTH/2,Config::PLAYER_HEIGHT,Config::PLAYER_WIDTH/2);
    struct Support {double x0,x1,z0,z1;};
    for (int z=int(std::floor(lo.z));z<=int(std::floor(hi.z-1e-6));++z)
        for (int x=int(std::floor(lo.x));x<=int(std::floor(hi.x-1e-6));++x) {
            const double x0=std::max(lo.x,double(x)),x1=std::min(hi.x,double(x+1));
            const double z0=std::max(lo.z,double(z)),z1=std::min(hi.z,double(z+1));
            std::vector<Support> supports;
            bool full=false;
            for (int y=int(std::floor(p.y-.05));y<=int(std::floor(hi.y));++y) {
                const auto id=world.getLoadedBlock(x,y,z);
                if (!id || isFluid(*id) || *id==BlockId::FIRE) return false;
                if (const auto* behavior=Plugins::blockBehavior(*id))
                    if (behavior->damage>0) return false;
                const auto boxes=blockCollisionBoxes(*id);
                for (uint8_t i=0;i<boxes.count;++i) {
                    const auto min=glm::dvec3(x,y,z)+glm::dvec3(boxes.boxes[i].min);
                    const auto max=glm::dvec3(x,y,z)+glm::dvec3(boxes.boxes[i].max);
                    if (hi.x<=min.x || lo.x>=max.x || hi.z<=min.z || lo.z>=max.z) continue;
                    if (hi.y>min.y && lo.y<max.y-.0001) return false;
                    if (p.y>=max.y && p.y-max.y<.05) {
                        supports.push_back({std::max(x0,min.x),std::min(x1,max.x),
                                            std::max(z0,min.z),std::min(z1,max.z)});
                        full=full||(min.x<=x0&&max.x>=x1&&min.z<=z0&&max.z>=z1);
                    }
                }
            }
            if (full) continue;
            if (supports.empty()) return false;
            // Partition at all box edges, so small shapes cannot authorize an
            // unsupported gap, while adjacent coplanar boxes may cover it together.
            std::vector<double> xs{x0,x1},zs{z0,z1};
            for (const auto& s:supports) {xs.push_back(s.x0);xs.push_back(s.x1);zs.push_back(s.z0);zs.push_back(s.z1);}
            auto sort=[](auto& edges){std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());};
            sort(xs);sort(zs);
            for (size_t xi=0;xi+1<xs.size();++xi) for (size_t zi=0;zi+1<zs.size();++zi) {
                const double px=(xs[xi]+xs[xi+1])*.5,pz=(zs[zi]+zs[zi+1])*.5;
                bool covered=false;
                for (const auto& s:supports) covered=covered||(px>=s.x0&&px<=s.x1&&pz>=s.z0&&pz<=s.z1);
                if (!covered) return false;
            }
        }
    return true;
}
