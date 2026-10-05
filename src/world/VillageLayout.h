#pragma once

#include "world/StructureGenerator.h"
#include <array>
#include <algorithm>
#include <tuple>

// This plan is shared by validation, geometry and population. No loaded-world
// query or mutable RNG participates in seeded village output.
struct VillageBuilding {
    int x, z, base;
    BlockId workstation;
    std::array<glm::ivec3, 2> beds;
    std::array<glm::dvec3, 2> spawns;
    int rotation = 0;
    glm::ivec3 point(int dx,int y,int dz) const {
        return rotation ? glm::ivec3(x-dz,y,z+dx) : glm::ivec3(x+dx,y,z+dz);
    }
    BedDirection bedDirection() const {return rotation?BedDirection::East:BedDirection::North;}

};
struct VillageLayout {
    int tier = 0;
    int radius = 48;
    int population = 20;
    glm::ivec3 center{0};
    std::vector<VillageBuilding> buildings;
    std::vector<glm::ivec3> roads;
    std::vector<glm::ivec3> farms;

    static VillageLayout create(const StructurePlacement& p,
                                const StructureGenerator::SurfaceSampler& surface = {},
                                bool sampleRoadHeights = true) {
        VillageLayout layout;
        layout.tier = static_cast<int>(p.variant % 3);
        layout.radius = 48 + layout.tier * 16;
        layout.population = 20 + layout.tier * 10;
        const int cx = p.minX + (p.maxX - p.minX) / 2;
        const int cz = p.minZ + (p.maxZ - p.minZ) / 2;
        layout.center = {cx,p.baseY,cz};
        const auto height = [&](int x,int z) { return surface ? surface(x,z) : p.baseY; };
        constexpr std::array<BlockId,13> stations{{BlockId::COMPOSTER,
            BlockId::FLETCHING_TABLE,BlockId::LOOM,BlockId::CAULDRON,
            BlockId::BLAST_FURNACE,BlockId::SMITHING_TABLE,BlockId::GRINDSTONE,
            BlockId::BARREL,BlockId::LECTERN,BlockId::CARTOGRAPHY_TABLE,
            BlockId::BREWING_STAND,BlockId::SMOKER,BlockId::STONECUTTER}};
        const int rows = (12 + layout.tier * 6 + 3) / 4;
        const int count = 12 + layout.tier * 6;
        const int professions = layout.tier == 0 ? 7 : layout.tier == 1 ? 10 : 13;
        std::array<int,6> rowZ{};
        for(int row=1;row<rows;++row) {
            const auto h=WorldGenContext::hashPosition(p.variant,cx,row,cz);
            rowZ[row]=rowZ[row-1]+20+static_cast<int>(h%7);
        }
        const int middle=rowZ[rows-1]/2;
        for (int i=0;i<count;++i) {
            const auto h=WorldGenContext::hashPosition(p.variant,cx+i,17,cz);
            const int column=i%4;
            const int x=cx+(column==0?-35+static_cast<int>((h>>8)%11):
                column==1?-15+static_cast<int>((h>>8)%6):
                column==2?10+static_cast<int>((h>>8)%6):25+static_cast<int>((h>>8)%11));
            const int z=cz+rowZ[i/4]-middle+static_cast<int>((h>>24)%9)-4;
            const int base=height(x,z);
            const BlockId station=i<professions ? stations[static_cast<size_t>(i)] :
                (i%4==((i/4)%2 ? 3 : 0) ? BlockId::COMPOSTER : BlockId::AIR);
            VillageBuilding b{x,z,base,station,
                {{{x-2,base+1,z},{x+2,base+1,z}}},
                {{{x-.5,base+1.0,z+.5},{x+1.5,base+1.0,z+.5}}}};
            b.rotation=static_cast<int>((h>>40)&1u);
            if(b.rotation) {
                for(auto& bed:b.beds)bed=b.point(bed.x-x,bed.y,bed.z-z);
                for(auto& spawn:b.spawns) {
                    const double dx=spawn.x-(x+.5),dz=spawn.z-(z+.5);
                    spawn={x+.5-dz,spawn.y,z+.5+dx};
                }
            }
            layout.buildings.push_back(b);
        }
        const auto road=[&](int ax,int az,int bx,int bz) {
            const int steps=std::max(std::abs(bx-ax),std::abs(bz-az));
            for(int step=0;step<=steps;++step) {
                const int x=ax+(steps?(bx-ax)*step/steps:0);
                const int z=az+(steps?(bz-az)*step/steps:0);
                // A full 3x3 sweep keeps bends connected without diagonal gaps.
                for(int dx=-1;dx<=1;++dx)for(int dz=-1;dz<=1;++dz)
                    layout.roads.push_back({x+dx,p.baseY,z+dz});
            }
        };
        for(const auto& b:layout.buildings)if(b.rotation)road(b.x-5,b.z,b.x-5,b.z+5);
        int previousX=cx,previousZ=cz;
        for(int row=0;row<rows;++row) {
            const auto h=WorldGenContext::hashPosition(p.variant,cx,row,cz);
            const int z=cz+rowZ[row]-middle+5;
            const int spineX=cx+static_cast<int>((h>>32)%3)-1;
            road(previousX,previousZ,spineX,z);
            previousX=spineX;previousZ=z;
            int lastX=cx-42,lastZ=z;
            const int end=std::min(count,row*4+4);
            for(int i=row*4;i<end;++i) {
                const auto& b=layout.buildings[static_cast<size_t>(i)];
                road(lastX,lastZ,b.x-5,b.z+5);
                road(b.x-5,b.z+5,b.x+5,b.z+5);
                lastX=b.x+5;lastZ=b.z+5;
            }
            road(lastX,lastZ,cx+42,z);
            // Fields stagger beside the lane instead of lining up with houses.
            const int fx=cx+(row%2?42:-42),fz=z-6;
            layout.farms.push_back({fx,height(fx,fz),fz});
        }
        std::sort(layout.roads.begin(),layout.roads.end(),[](const auto& a,const auto& b){
            return std::tie(a.x,a.z)<std::tie(b.x,b.z);
        });
        layout.roads.erase(std::unique(layout.roads.begin(),layout.roads.end(),[](const auto& a,const auto& b){
            return a.x==b.x && a.z==b.z;
        }),layout.roads.end());
        if(sampleRoadHeights)for(auto& r:layout.roads)r.y=height(r.x,r.z);
        return layout;
    }
};
