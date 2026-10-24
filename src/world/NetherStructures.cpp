#include "world/NetherStructures.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <cmath>

std::optional<BlockId> NetherStructure::block(int wx,int wy,int wz) const {
    const int x=wx-anchor.x,y=wy-anchor.y,z=wz-anchor.z;
    if(kind==NetherStructureKind::Fortress) {
        if(std::abs(x)>48||std::abs(z)>48||y<-24||y>9)return {};
        bool corridor=std::abs(x)<=3||std::abs(z)<=3||std::abs(std::abs(x)-32)<=3||std::abs(std::abs(z)-32)<=3;
        bool edge=false,room=false,wall=false;
        for(int rz=-1;rz<=1;++rz)for(int rx=-1;rx<=1;++rx) {
            const int dx=std::abs(x-rx*32),dz=std::abs(z-rz*32);
            if(dx<=6&&dz<=6){room=true;wall=(dx==6||dz==6)&&!(dx<=1||dz<=1);}
        }
        if(!corridor&&!room)return {};
        if(y<0) {
            const bool pillar=std::abs(x%32)<=1&&std::abs(z%32)<=1;
            return y>=-2||pillar?std::optional<BlockId>(BlockId::NETHER_BRICKS):std::nullopt;
        }
        if(y==0) {
            if(std::abs(x+32)<=3&&std::abs(z)<=3)return BlockId::SOUL_SAND;
            return BlockId::NETHER_BRICKS;
        }
        if(y==1&&std::abs(x+32)<=3&&std::abs(z)<=3)return BlockId::NETHER_WART;
        if(y==1&&((x==34&&z==2)||(x==-30&&z==-30)))return BlockId::CHEST;
        if(y==1&&x==0&&z==32)return BlockId::BLAZE_SPAWNER;
        if(room) {
            if(y==8||wall)return BlockId::NETHER_BRICKS;
            return y<8?std::optional<BlockId>(BlockId::AIR):std::nullopt;
        }
        edge=std::abs(x)%32==3||std::abs(z)%32==3;
        return y<=2&&edge?BlockId::NETHER_BRICKS:BlockId::AIR;
    }
    if(kind<=NetherStructureKind::BastionTreasure) {
        const bool bridge=kind==NetherStructureKind::BastionBridge&&std::abs(x)<=4&&z>=-48&&z<-20;
        if(bridge) {
            if(y>=-2&&y<=0)return BlockId::BLACKSTONE_BRICKS;
            if(y>0&&y<=2&&std::abs(x)==4)return BlockId::BLACKSTONE_BRICKS;
            if(y>0&&y<=5)return BlockId::AIR;
            return {};
        }
        if(std::abs(x)>20||std::abs(z)>20||y<-20||y>24)return {};
        const bool wall=std::abs(x)>=18||std::abs(z)>=18;
        if(y<0)return (wall||y>=-2)?std::optional<BlockId>(BlockId::BLACKSTONE):std::nullopt;
        if(y==1&&x==12&&z==12)return BlockId::CHEST;
        if(kind==NetherStructureKind::BastionTreasure&&std::abs(x)<=6&&std::abs(z)<=6) {
            if(y==0)return BlockId::BLACKSTONE_BRICKS;
            if(y<=3)return BlockId::LAVA;
            if(y==4&&std::abs(x)<=2&&std::abs(z)<=2)return BlockId::BLACKSTONE_BRICKS;
            if(y==5&&x==0&&z==0)return BlockId::MAGMA_SPAWNER;
        }
        // Two offset stairs link the courtyard and both upper galleries.
        // Clearance is part of the same coordinate authority as the platforms.
        for(int flight=0;flight<2;++flight) {
            const int start=flight?0:-8,step=z-start,center=flight?12:-12;
            if(std::abs(x-center)>2||step<0||step>8)continue;
            const int level=flight*8+step;
            if(y==level)return stairBlock(ArchitecturalMaterial::Blackstone,BlockHalf::Bottom,BedDirection::South);
            if(y>level&&y<=level+3)return BlockId::AIR;
        }
        if(wall) {
            if(z<=-18&&std::abs(x)<=2&&y>0&&y<5)return BlockId::AIR;
            const auto h=WorldGenContext::hashPosition(seed,wx,wy,wz);
            return y<16||h%5?BlockId::BLACKSTONE_BRICKS:BlockId::AIR;
        }
        if(y==0||((y==8||y==16)&&(std::abs(x)>=8||std::abs(z)>=8)))return BlockId::BLACKSTONE_BRICKS;
        if(kind==NetherStructureKind::BastionStable&&y>=1&&y<=2&&x%8==0&&std::abs(z)>4)return BlockId::BLACKSTONE;
        if(kind==NetherStructureKind::BastionHousing&&y>0&&y<16&&(std::abs(x)==12||std::abs(z)==12)&&std::abs(x)>2&&std::abs(z)>2)return BlockId::BLACKSTONE_BRICKS;
        return y<24?std::optional<BlockId>(BlockId::AIR):std::nullopt;
    }
    if(kind==NetherStructureKind::Fossil) {
        if(x>=-7&&x<=7&&z==0&&y==3)return BlockId::BONE_BLOCK;
        if(x>=-6&&x<=6&&x%3==0&&std::abs(z)<=3&&y>=0&&y<=3&&
           (y==3||std::abs(z)==3))return BlockId::BONE_BLOCK;
        return {};
    }
    // Deliberately incomplete, unlit frame with crying obsidian obstruction.
    if(std::abs(x)>5||std::abs(z)>4||y<-1||y>6)return {};
    if(y==-1&&std::abs(x)<=4&&std::abs(z)<=3)return BlockId::NETHERRACK;
    if(z==0&&x>=-1&&x<=2&&y>=0&&y<=4&&(x==-1||x==2||y==0||y==4)) {
        if(x==2&&y>=3)return BlockId::AIR;
        return x==-1&&y==2?BlockId::CRYING_OBSIDIAN:BlockId::OBSIDIAN;
    }
    if(x==4&&y==0&&z==1)return BlockId::CHEST;
    return {};
}

StructureLootProfile NetherStructure::loot() const {
    if(kind==NetherStructureKind::Fortress)return StructureLootProfile::NetherFortress;
    if(kind==NetherStructureKind::RuinedPortal)return StructureLootProfile::RuinedPortal;
    return StructureLootProfile::NetherBastion;
}

const char* NetherStructure::key() const {
    constexpr const char* keys[]={"minecraftc:nether_fortress","minecraftc:bastion_bridge",
        "minecraftc:bastion_stable","minecraftc:bastion_housing","minecraftc:bastion_treasure",
        "minecraftc:nether_fossil","minecraftc:ruined_portal"};
    return keys[static_cast<size_t>(kind)];
}
