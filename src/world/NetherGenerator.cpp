#include "world/NetherGenerator.h"
#include "world/Chunk.h"
#include "world/WorldGenContext.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace {
int floorDiv(int a, int b) { const int q=a/b; return q-(a%b<0); }
}

NetherBiome NetherGenerator::biomeAt(int x, int z) const {
    // Jittered Voronoi cells avoid world-axis stripes and preserve negatives.
    const int cx=floorDiv(x,192), cz=floorDiv(z,192);
    double best=1e30;
    uint64_t chosen=0;
    for(int dz=-1;dz<=1;++dz) for(int dx=-1;dx<=1;++dx) {
        const auto h=WorldGenContext::hashPosition(m_seed^0x42494f4d45ULL,cx+dx,0,cz+dz);
        const double px=(cx+dx)*192.0+32+double(h%128);
        const double pz=(cz+dz)*192.0+32+double((h>>16)%128);
        const double d=(x-px)*(x-px)+(z-pz)*(z-pz);
        if(d<best) { best=d;chosen=h; }
    }
    return static_cast<NetherBiome>((chosen>>32)%5);
}

std::vector<NetherStructure> NetherGenerator::structuresAt(int x,int z) const {
    std::vector<NetherStructure> out;
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
        const int cx=floorDiv(x,256)+dx,cz=floorDiv(z,256)+dz;
        const auto h=WorldGenContext::hashPosition(m_seed^0x535452554354ULL,cx,0,cz);
        const int ax=cx*256+64+int((h>>8)%128),az=cz*256+64+int((h>>24)%128);
        if(std::abs(x-ax)>48||std::abs(z-az)>48)continue;
        const bool fortress=h%5==0;
        if(!fortress&&biomeAt(ax,az)==NetherBiome::BasaltDeltas)continue;
        out.push_back({fortress?NetherStructureKind::Fortress:static_cast<NetherStructureKind>(1+(h>>40)%4),{ax,fortress?64:48,az},h});
    }
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
        const int cx=floorDiv(x,96)+dx,cz=floorDiv(z,96)+dz;
        const auto h=WorldGenContext::hashPosition(m_seed^0x464f5353494cULL,cx,0,cz);
        const int ax=cx*96+32+int((h>>8)%32),az=cz*96+32+int((h>>24)%32);
        if(std::abs(x-ax)>8||std::abs(z-az)>8)continue;
        const auto biome=biomeAt(ax,az);
        if(h%8==0&&biome==NetherBiome::SoulSandValley)out.push_back({NetherStructureKind::Fossil,{ax,42,az},h});
        else if(h%11==0)out.push_back({NetherStructureKind::RuinedPortal,{ax,40,az},h});
    }
    return out;
}

std::optional<NetherStructure> NetherGenerator::locate(const std::string& key,int x,int z,int radius) const {
    if(radius<0||radius>8192||std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000)return {};
    std::optional<NetherStructure> best;
    int64_t distance=int64_t(radius)*radius;
    auto consider=[&](int ax,int az) {
        for(const auto& s:structuresAt(ax,az)) {
            if(key!=s.key())continue;
            const int64_t dx=int64_t(s.anchor.x)-x,dz=int64_t(s.anchor.z)-z,d=dx*dx+dz*dz;
            if(d>distance)continue;
            if(d==distance&&best&&std::tie(s.anchor.x,s.anchor.z)>=std::tie(best->anchor.x,best->anchor.z))continue;
            distance=d;best=s;
        }
    };
    for(int cell:{256,96})for(int cz=floorDiv(z-radius,cell);cz<=floorDiv(z+radius,cell);++cz)
        for(int cx=floorDiv(x-radius,cell);cx<=floorDiv(x+radius,cell);++cx) {
            const auto h=WorldGenContext::hashPosition(m_seed^(cell==256?0x535452554354ULL:0x464f5353494cULL),cx,0,cz);
            const int inset=cell==256?64:32,jitter=cell==256?128:32;
            consider(cx*cell+inset+int((h>>8)%jitter),cz*cell+inset+int((h>>24)%jitter));
        }
    return best;
}

bool NetherGenerator::solid(int x, int y, int z) const {
    if(y<0||y>127) return false;
    const auto h=WorldGenContext::hashPosition(m_seed,x,0,z);
    if(y<=int(h%5)||y>=127-int((h>>8)%5))return true;
    const float shelf=39.f+11.f*m_noise.octave2D(x*.012f,z*.012f,2);
    const float cave=m_noise.octave3D(x*.024f,y*.035f,z*.024f,2);
    // A solid lower shelf and ceiling enclose wide, connected lava caverns.
    const float lower=(shelf-y)/24.f;
    const float upper=(y-105.f)/18.f;
    return cave*.9f+std::max(lower,upper)>.20f;
}

BlockId NetherGenerator::base(int x,int y,int z) const {
    if(y<0||y>127)return BlockId::AIR;
    const auto h=WorldGenContext::hashPosition(m_seed,x,0,z);
    if(y<=int(h%5)||y>=127-int((h>>8)%5))return BlockId::BEDROCK;
    if(!solid(x,y,z))return y<=31?BlockId::LAVA:BlockId::AIR;
    const auto ore=WorldGenContext::hashPosition(m_seed^0x4f524553ULL,x,y,z);
    const auto vein=[&](uint64_t domain,int chance,int radius) {
        const int cx=floorDiv(x,8),cy=floorDiv(y,8),cz=floorDiv(z,8);
        const auto seed=WorldGenContext::hashPosition(m_seed^domain,cx,cy,cz);
        if(seed%uint64_t(chance)!=0)return false;
        const int dx=x-(cx*8+2+int((seed>>8)%4));
        const int dy=y-(cy*8+2+int((seed>>16)%4));
        const int dz=z-(cz*8+2+int((seed>>24)%4));
        return dx*dx+dy*dy+dz*dz<=radius*radius;
    };
    if(y>=8&&y<=24&&vein(0x444542524953ULL,24,1)&&solid(x,y+1,z)&&solid(x,y-1,z))return BlockId::ANCIENT_DEBRIS;
    if(vein(0x51554152545aULL,3,2))return BlockId::NETHER_QUARTZ_ORE;
    if(vein(0x474f4c44ULL,5,1))return BlockId::NETHER_GOLD_ORE;
    if(y>=8&&y<=24&&ore%1400==0&&solid(x,y+1,z)&&solid(x,y-1,z))return BlockId::ANCIENT_DEBRIS;
    if(ore%97==0)return BlockId::NETHER_QUARTZ_ORE;
    if(ore%173==0)return BlockId::NETHER_GOLD_ORE;
    const auto biome=biomeAt(x,z);
    const bool exposed=!solid(x,y+1,z);
    if(exposed&&y>=30&&y<100) {
        switch(biome) {
            case NetherBiome::SoulSandValley:return ore%3?BlockId::SOUL_SAND:BlockId::SOUL_SOIL;
            case NetherBiome::CrimsonForest:return BlockId::CRIMSON_NYLIUM;
            case NetherBiome::WarpedForest:return BlockId::WARPED_NYLIUM;
            case NetherBiome::BasaltDeltas:return ore%4?BlockId::BASALT:BlockId::BLACKSTONE;
            default:break;
        }
        if(y<35&&ore%5==0)return BlockId::MAGMA_BLOCK;
    }
    if(biome==NetherBiome::BasaltDeltas&&y<65)return ore%4?BlockId::BASALT:BlockId::BLACKSTONE;
    if(y>50&&!solid(x,y-1,z)&&ore%9==0)return BlockId::GLOWSTONE;
    return BlockId::NETHERRACK;
}

BlockId NetherGenerator::sample(int x,int y,int z) const {
    if(y<0||y>127)return BlockId::AIR;
    return column(x,z)[Config::worldYToStorageY(y)];
}

std::array<BlockId,Config::CHUNK_SIZE_Y> NetherGenerator::column(int x,int z) const {
    std::array<BlockId,Config::CHUNK_SIZE_Y> out{};
    for(int y=0;y<=127;++y)out[Config::worldYToStorageY(y)]=base(x,y,z);
    const auto original=out;
    const auto biome=biomeAt(x,z);
    const bool crimson=biome==NetherBiome::CrimsonForest;
    const bool warped=biome==NetherBiome::WarpedForest;
    if(crimson||warped) {
        const int cx=floorDiv(x,12),cz=floorDiv(z,12);
        auto place=[&](int y,BlockId block) {
            if(y<33||y>105)return;
            auto& cell=out[Config::worldYToStorageY(y)];
            if(cell==BlockId::AIR)cell=block;
        };
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            const auto h=WorldGenContext::hashPosition(m_seed^0x46554e4749ULL,cx+dx,0,cz+dz);
            if(h%4==0)continue;
            const int ax=(cx+dx)*12+3+int((h>>8)%6),az=(cz+dz)*12+3+int((h>>16)%6);
            if(std::abs(x-ax)>3||std::abs(z-az)>3)continue;
            const auto b=biomeAt(ax,az);
            if(b!=NetherBiome::CrimsonForest&&b!=NetherBiome::WarpedForest)continue;
            int floor=0;
            for(int sy=75;sy>=33;--sy)if(solid(ax,sy,az)&&!solid(ax,sy+1,az)){floor=sy;break;}
            if(!floor)continue;
            const int top=floor+5+int((h>>24)%5);
            if(x==ax&&z==az)for(int y=floor+1;y<=top;++y)
                place(y,b==NetherBiome::CrimsonForest?BlockId::CRIMSON_STEM:BlockId::WARPED_STEM);
            if(b==NetherBiome::CrimsonForest && std::abs(x-ax)+std::abs(z-az)<=4) {
                const auto vine=WorldGenContext::hashPosition(h,x,top,z);
                if(vine%5==0)for(int y=top-2;y>=top-3-int((vine>>8)%4);--y)place(y,BlockId::WEEPING_VINES);
            }
            if(std::abs(x-ax)+std::abs(z-az)<=4)for(int y=top-1;y<=top+1;++y)
                place(y,(y==top-1&&(x+z)%3==0)?BlockId::SHROOMLIGHT:
                    b==NetherBiome::CrimsonForest?BlockId::NETHER_WART_BLOCK:BlockId::WARPED_WART_BLOCK);
        }
        for(int y=33;y<=105;++y) {
            const auto under=original[Config::worldYToStorageY(y-1)];
            const auto h=WorldGenContext::hashPosition(m_seed^0x504c414e54ULL,x,y,z);
            if(under==BlockId::WARPED_NYLIUM && h%19==0)
                for(int v=0;v<2+int((h>>8)%4);++v)place(y+v,BlockId::TWISTING_VINES);
            if((under==BlockId::CRIMSON_NYLIUM||under==BlockId::WARPED_NYLIUM)&&h%7==0)
                place(y,h%3==0?(crimson?BlockId::CRIMSON_FUNGUS:BlockId::WARPED_FUNGUS):
                    (crimson?BlockId::CRIMSON_ROOTS:BlockId::WARPED_ROOTS));
        }
    }
    const auto decoration=WorldGenContext::hashPosition(m_seed^0x4e4554484552ULL,x,0,z);
    if(biome==NetherBiome::BasaltDeltas && decoration%7<2) {
        for(int y=80;y>=32;--y)if(isSolid(original[Config::worldYToStorageY(y)]) && original[Config::worldYToStorageY(y+1)]==BlockId::AIR) {
            for(int dy=1;dy<=2+int((decoration>>8)%8);++dy) {
                auto& cell=out[Config::worldYToStorageY(y+dy)];if(cell!=BlockId::AIR)break;cell=BlockId::BASALT;
            }
            break;
        }
    }
    if(biome==NetherBiome::SoulSandValley && decoration%89==0) {
        for(int y=33;y<100;++y)if(out[Config::worldYToStorageY(y)]==BlockId::AIR &&
            (out[Config::worldYToStorageY(y-1)]==BlockId::SOUL_SAND||out[Config::worldYToStorageY(y-1)]==BlockId::SOUL_SOIL)) {
            out[Config::worldYToStorageY(y)]=BlockId::SOUL_FIRE;break;
        }
    }
    if(decoration%769==0) {
        for(int y=115;y>=40;--y)if(out[Config::worldYToStorageY(y)]==BlockId::AIR && isSolid(out[Config::worldYToStorageY(y+1)])) {
            out[Config::worldYToStorageY(y)]=BlockId::LAVA;
            for(int below=y-1;below>31;--below) {
                auto& cell=out[Config::worldYToStorageY(below)];if(cell!=BlockId::AIR)break;cell=BlockId::FALLING_LAVA;
            }
            break;
        }
    }
    for(const auto& structure:structuresAt(x,z))for(int y=1;y<123;++y)
        if(const auto block=structure.block(x,y,z))out[Config::worldYToStorageY(y)]=*block;
    return out;
}

std::vector<NetherSpan> NetherGenerator::spans(int x,int z) const {
    const auto blocks=column(x,z);
    std::vector<NetherSpan> result;
    for(int y=0;y<128;) {
        const auto block=blocks[Config::worldYToStorageY(y)];
        const int bottom=y++;
        while(y<128&&blocks[Config::worldYToStorageY(y)]==block)++y;
        if(block!=BlockId::AIR)result.push_back({bottom,y-1,block});
    }
    return result;
}

int NetherGenerator::surface(int x,int z) const {
    const auto blocks=column(x,z);
    for(int y=90;y>=32;--y)if(isSolid(blocks[Config::worldYToStorageY(y)])&&blocks[Config::worldYToStorageY(y+1)]==BlockId::AIR&&blocks[Config::worldYToStorageY(y+2)]==BlockId::AIR)return y;
    return -1;
}

glm::ivec3 NetherGenerator::spawnBlock() const {
    for(int radius=0;radius<=128;radius+=4)
        for(int z=-radius;z<=radius;z+=4)for(int x=-radius;x<=radius;x+=4) {
            if(std::max(std::abs(x),std::abs(z))!=radius)continue;
            const int y=surface(x,z);
            if(y>=32&&sample(x,y,z)!=BlockId::MAGMA_BLOCK)return {x,y+1,z};
        }
    throw std::runtime_error("Nether has no safe natural spawn in search radius");
}

void NetherGenerator::generate(Chunk& chunk) const {
    chunk.pluginStructures.clear();
    for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
        const auto blocks=column(chunk.worldX()+x,chunk.worldZ()+z);
        for(int sy=0;sy<Config::CHUNK_SIZE_Y;++sy)
            chunk.blockAt(x,Config::WORLD_MIN_Y+sy,z)=static_cast<uint16_t>(blocks[sy]);
        chunk.setColumnMaxY(x,z,127);
    }
    chunk.pluginStructures=structureBlocks(chunk.worldX(),chunk.worldZ(),16,16);
    chunk.finishBulkBlockEdit();
    chunk.generated=true;chunk.generationInProgress=false;
}

std::vector<RegionGenerationData::PendingBlock> NetherGenerator::structureBlocks(int originX,int originZ,int width,int depth) const {
    std::vector<RegionGenerationData::PendingBlock> out;
    for(int z=0;z<depth;++z)for(int x=0;x<width;++x) {
        for(const auto& structure:structuresAt(originX+x,originZ+z))for(int y=1;y<123;++y) {
            const auto block=structure.block(originX+x,y,originZ+z);
            if(!block||(*block!=BlockId::CHEST&&*block!=BlockId::BLAZE_SPAWNER&&*block!=BlockId::MAGMA_SPAWNER))continue;
            RegionGenerationData::PendingBlock p;
            p.worldX=originX+x;p.worldY=y;p.worldZ=originZ+z;
            p.id=*block;p.overwrite=true;p.needsBlockEntity=true;p.lootProfile=structure.loot();
            p.lootSeed=WorldGenContext::hashPosition(structure.seed,p.worldX,y,p.worldZ);
            p.structureKey=structure.key();p.structureRevision="1";
            out.push_back(std::move(p));
        }
    }
    return out;
}
