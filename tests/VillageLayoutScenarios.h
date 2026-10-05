#pragma once
#include "world/VillageLayout.h"
#include <queue>

inline void testVillageLayouts() {
    for(const auto type:{StructureType::Village,StructureType::DesertVillage,
        StructureType::TaigaVillage,StructureType::SnowVillage,StructureType::SavannaVillage}) {
        for(uint64_t tier=0;tier<3;++tier) {
            const int radius=48+static_cast<int>(tier)*16;
            StructurePlacement p{0,0,100,type,tier,-radius,radius,-radius,radius};
            const auto layout=VillageLayout::create(p);
            require(layout.buildings.size()==12+tier*6 && layout.population==20+static_cast<int>(tier)*10,
                "village tier has incorrect building/population count");
            std::set<int> entranceRows;
            for(const auto& b:layout.buildings)entranceRows.insert(b.z);
            require(entranceRows.size()>(layout.buildings.size()+3)/4,
                "village lots reverted to perfectly aligned rows");
            std::map<std::tuple<int,int,int>,BlockId> blocks;
            StructureGenerator::build(p,[&](int x,int y,int z,BlockId b) {
                require(std::abs(x)<=radius && std::abs(z)<=radius,
                    "village writes outside its reservation");
                blocks[{x,y,z}]=b;
            });
            const auto at=[&](int x,int y,int z) {
                const auto it=blocks.find({x,y,z});
                return it==blocks.end() ? (y<=100 ? BlockId::STONE : BlockId::AIR) : it->second;
            };
            size_t beds=0;std::set<BlockId> stations;
            for(const auto& entry:blocks) {
                BedPart part;BedDirection direction;
                if(decodeBed(entry.second,part,direction) && part==BedPart::Foot)++beds;
                if(isVillagerWorkstation(entry.second))stations.insert(entry.second);
            }
            require(beds==layout.buildings.size()*2 && stations.size()==(tier==0?7u:tier==1?10u:13u),
                "generated village beds or professions disagree with shared layout");
            const auto clearForVillager=[&](BlockId id) {
                DoorState door;
                if(decodeDoor(id,door)) {
                    require(door.material!=DoorMaterial::Iron,"villages must have usable wooden doors");
                    door.open=true;
                    return !pointInsideBlockCollision(doorBlock(door),glm::vec3(.5f));
                }
                return !isSolid(id);
            };
            const auto walkable=[&](int x,int z) {
                return std::abs(x)<=radius && std::abs(z)<=radius &&
                    clearForVillager(at(x,101,z)) && clearForVillager(at(x,102,z)) && isSolid(at(x,100,z));
            };
            std::queue<std::pair<int,int>> queue;std::set<std::pair<int,int>> visited;
            queue.push({0,5});visited.insert({0,5});
            while(!queue.empty()) {
                const auto [x,z]=queue.front();queue.pop();
                for(const auto& d:std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                    const std::pair<int,int> n{x+d.first,z+d.second};
                    if(walkable(n.first,n.second) && visited.insert(n).second)queue.push(n);
                }
            }
            for(const auto& b:layout.buildings) {
                require(visited.count({b.point(0,0,3).x,b.point(0,0,3).z})>0,"village house has no connected entrance");
                for(const auto& spawn:b.spawns)require(visited.count({static_cast<int>(std::floor(spawn.x)),
                    static_cast<int>(std::floor(spawn.z))})>0,"village resident spawns inside blocked geometry");
                if(b.workstation!=BlockId::AIR)require(visited.count({b.point(0,0,-1).x,b.point(0,0,-1).z})>0,
                    "village workplace has no reachable approach");
                for(const auto& bed:b.beds)require(isBed(at(bed.x,bed.y,bed.z)) &&
                    isBed(at((bed+bedDirectionOffset(b.bedDirection())).x,bed.y,(bed+bedDirectionOffset(b.bedDirection())).z)),"layout bed does not match emitted bed pair");
            }
        }
    }
}
