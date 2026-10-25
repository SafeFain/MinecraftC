#pragma once

#include "game/DimensionRules.h"
#include "world/NetherPortal.h"
#include "plugins/ContentQueries.h"
#include "plugins/ContentMutation.h"
#include <map>
#include <set>
#include <tuple>

void testNether() {
    // This scenario deliberately runs without installing any optional provider.
    Plugins::mutableContentState()=Plugins::ContentState{};
    require(Plugins::validDimension(DimensionId::Nether),"Nether missing with all plugins disabled");
    require(!Plugins::validDimension(DimensionId::Heaven),"plugin-off fixture unexpectedly retained Heaven");
    require(Plugins::resolveDimension("minecraftc:nether")==2,"native Nether handle changed");
    require(Plugins::dimensionDirectory(DimensionId::Nether)=="nether","Nether storage namespace");
    const auto rules=dimensionRules(DimensionId::Nether);
    require(!rules.skyLight&&!rules.weather&&rules.ultrawarm&&rules.bedsExplode&&rules.respawnAnchorsWork&&rules.contains(255)&&!rules.contains(256)&&!rules.contains(-1),"Nether native policies");
    WorldGenerator a(4242,WorldType::Normal,DimensionId::Nether);
    WorldGenerator b(4242,WorldType::Superflat,DimensionId::Nether);
    WorldGenerator different(4243,WorldType::Normal,DimensionId::Nether);
    require(!a.customGenerator()&&a.generationVersion()==1,"Nether entered plugin generator path");
    std::set<NetherBiome> biomes;
    for(int z=-1024;z<=1024;z+=64)for(int x=-1024;x<=1024;x+=64)biomes.insert(a.nether().biomeAt(x,z));
    require(biomes.size()==5,"five Nether biomes unavailable");
    bool varied=false,hasCavity=false;
    for(const auto coordinate:std::array<glm::ivec2,4>{{{-1,-1},{0,0},{7,-8},{-14,10}}}) {
        auto chunk=std::make_unique<Chunk>(coordinate.x,coordinate.y);
        auto singleton=std::make_unique<Chunk>(coordinate.x,coordinate.y);
        std::vector<Chunk*> chunks{chunk.get()};
        std::vector<RegionGenerationData::PendingBlock> pending;
        a.generateRegion(coordinate.x,coordinate.y,1,6,chunks,pending);
        b.generate(*singleton);
        std::vector<uint16_t> left,right;
        chunk->copyRawBlocks(left);singleton->copyRawBlocks(right);
        require(left==right,"Nether singleton/region/preset equivalence");
        for(int z=0;z<16;z+=5)for(int x=0;x<16;x+=5) {
            const int wx=chunk->worldX()+x,wz=chunk->worldZ()+z;
            const auto column=a.nether().column(wx,wz);
            require(column[Config::worldYToStorageY(0)]==BlockId::BEDROCK&&column[Config::worldYToStorageY(127)]==BlockId::BEDROCK,"Nether open bedrock envelope");
            for(int y=-64;y<320;++y)require(chunk->getBlock(x,y,z)==column[Config::worldYToStorageY(y)],"Nether point/column/chunk mismatch");
            require(a.samplePluginBlock(wx,48,wz)==column[Config::worldYToStorageY(48)],"native shared point query");
            const auto other=different.nether().column(wx,wz);
            varied=varied||column!=other;
            const auto spans=a.nether().spans(wx,wz);
            for(int y=0;y<128;++y) {
                BlockId reconstructed=BlockId::AIR;
                for(const auto& span:spans)if(y>=span.bottom&&y<=span.top)reconstructed=span.block;
                require(reconstructed==column[Config::worldYToStorageY(y)],"Nether cavity span lost occupancy or material");
                if(y>31&&y<110&&reconstructed==BlockId::AIR)hasCavity=true;
            }
        }
    }
    require(varied&&hasCavity,"Nether seed variation or air cavities absent");
    for(const char* key:{"minecraftc:nether_fortress","minecraftc:bastion_bridge","minecraftc:bastion_stable","minecraftc:bastion_housing","minecraftc:bastion_treasure","minecraftc:nether_fossil","minecraftc:ruined_portal"}) {
        const auto found=a.nether().locate(key,0,0,4096);
        require(found.has_value(),"Nether structure search missed a type");
        const auto repeat=a.nether().locate(key,0,0,4096);
        require(found->anchor==repeat->anchor&&found->seed==repeat->seed,"Nether structure locate scheduling dependence");
        const auto locations=a.nether().structuresAt(found->anchor.x,found->anchor.z);
        require(std::any_of(locations.begin(),locations.end(),[&](const auto& s){return s.anchor==found->anchor&&std::string(s.key())==key;}),"Nether locate/generation acceptance mismatch");
        if(found->kind>=NetherStructureKind::BastionBridge&&found->kind<=NetherStructureKind::BastionTreasure)
            require(a.nether().biomeAt(found->anchor.x,found->anchor.z)!=NetherBiome::BasaltDeltas,"bastion spawned in basalt delta");
        if(found->kind>=NetherStructureKind::BastionBridge&&found->kind<=NetherStructureKind::BastionTreasure) {
            for(int flight=0;flight<2;++flight)for(int step=0;step<=8;++step) {
                const auto p=found->anchor+glm::ivec3(flight?12:-12,flight*8+step,step+(flight?0:-8));
                ArchitecturalBlockState state;
                require(decodeArchitecturalBlock(a.nether().sample(p.x,p.y,p.z),state)&&state.material==ArchitecturalMaterial::Blackstone,"bastion gallery stairs missing");
                require(a.nether().sample(p.x,p.y+1,p.z)==BlockId::AIR&&a.nether().sample(p.x,p.y+2,p.z)==BlockId::AIR,"bastion stairs lack headroom");
            }
        }
        auto region=std::vector<std::unique_ptr<Chunk>>{};std::vector<Chunk*> regionView;
        const int cx=static_cast<int>(std::floor(found->anchor.x/16.0)),cz=static_cast<int>(std::floor(found->anchor.z/16.0));
        for(int z=0;z<2;++z)for(int x=0;x<2;++x){region.push_back(std::make_unique<Chunk>(cx+x,cz+z));regionView.push_back(region.back().get());}
        std::vector<RegionGenerationData::PendingBlock> boundary;a.generateRegion(cx,cz,2,6,regionView,boundary);
        for(const auto& chunk:region) {
            auto singleton=std::make_unique<Chunk>(chunk->cx,chunk->cz);a.generate(*singleton);
            std::vector<uint16_t> one,two;chunk->copyRawBlocks(one);singleton->copyRawBlocks(two);
            require(one==two,"located native structure differs across region boundaries");
        }
        const auto hostQuery=a.locatePluginStructure(key,0,0,4096);
        require(hostQuery&&hostQuery->anchor[0]==found->anchor.x&&hostQuery->anchor[2]==found->anchor.z,"native structure command authority differs");
    }
    const auto spawn=a.nether().spawnBlock();
    require(spawn==b.nether().spawnBlock()&&spawn.y>31&&spawn.y<127,"Nether deterministic safe spawn");
    require(isSolid(a.nether().sample(spawn.x,spawn.y-1,spawn.z))&&a.nether().sample(spawn.x,spawn.y,spawn.z)==BlockId::AIR,"Nether spawn support/headroom");
    for(bool alongZ:{false,true}) {
        std::map<std::tuple<int,int,int>,BlockId> blocks;
        const NetherPortalFrame frame{{-17,40,-33},2,3,alongZ};
        auto write=[&](glm::ivec3 p,BlockId block){blocks[{p.x,p.y,p.z}]=block;};
        auto read=[&](glm::ivec3 p){const auto i=blocks.find({p.x,p.y,p.z});return i==blocks.end()?BlockId::AIR:i->second;};
        for(int y=-1;y<=3;++y)for(int x=-1;x<=2;++x)
            if((x==-1||x==2)!=(y==-1||y==3))write(frame.at(x,y),BlockId::OBSIDIAN);
        const auto found=NetherPortal::find(read,frame.at(1,1),alongZ);
        require(found&&found->origin==frame.origin&&found->width==2&&found->height==3,"cornerless negative-coordinate portal frame");
        require(!NetherPortal::find(read,frame.origin,alongZ,true),"unlit portal treated as active");
        for(int y=0;y<3;++y)for(int x=0;x<2;++x)write(frame.at(x,y),frame.block());
        require(NetherPortal::find(read,frame.origin,alongZ,true).has_value(),"active portal validation");
        write(frame.at(-1,1),BlockId::AIR);
        require(!NetherPortal::find(read,frame.origin,alongZ,true),"broken portal remained active");
    }
    for(bool alongZ:{false,true})for(const auto size:std::array<glm::ivec2,6>{{{2,3},{21,21},{1,3},{22,3},{2,2},{2,22}}}) {
        const NetherPortalFrame frame{{-17,40,-33},size.x,size.y,alongZ};
        std::map<std::tuple<int,int,int>,BlockId> blocks;
        for(int y=-1;y<=size.y;++y)for(int x=-1;x<=size.x;++x) {
            const auto p=frame.at(x,y);blocks[{p.x,p.y,p.z}]=(x==-1||x==size.x||y==-1||y==size.y)?BlockId::OBSIDIAN:frame.block();
        }
        const auto read=[&](glm::ivec3 p){const auto it=blocks.find({p.x,p.y,p.z});return it==blocks.end()?BlockId::AIR:it->second;};
        require(NetherPortal::find(read,frame.origin,alongZ,true).has_value()==(size.x>=2&&size.x<=21&&size.y>=3&&size.y<=21),"native portal minimum/maximum exterior sizes");
    }
    const auto scaled=NetherPortal::scaledDestination({-1,64,-9},DimensionId::Overworld);
    require(scaled==glm::dvec3(-.5,64,-1.5),"negative portal coordinate floor");
}
