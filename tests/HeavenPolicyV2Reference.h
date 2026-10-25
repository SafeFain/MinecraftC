#pragma once
#include "world/StructureGenerator.h"
#include "reference/heaven_v9/FrozenHeavenV9.h"
#include "reference/heaven_v9/FrozenLoot.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"

namespace GenerationPolicyV2Integration {
struct HeavenStructureReference {
    WorldGenerator* generator=nullptr;
    MC_StructureInstance instance{};
    std::vector<MC_StructureBlockV2> blocks;
    std::string loot;
};
inline void heavenReference(const std::filesystem::path& root){
    // Immutable native algorithms are reference adapters, not a runtime provider
    // or evidence that the P1 content migration has been completed.
    for(uint64_t seed:{12ULL,123ULL,999ULL}){
        WorldGenerator native(seed,WorldType::Normal,DimensionId::Heaven);
        FrozenHeavenReference::FrozenHeavenV9 frozen(native.getSeed());
        auto& dimension=Plugins::mutableContentState().extensions.dimensions.at(1);
        dimension.spawnUserdata=&native;
        dimension.sampleSpawn=[](const MC_GenerationContext* c,double* p,void* u)->int32_t {
            const auto& g=*static_cast<WorldGenerator*>(u);if(c->seed!=g.getSeed())return -1;
            const auto a=g.heavenSpawnBlock();p[0]=a.x+.5;p[1]=a.y+1.01;p[2]=a.z+.5;return 0;
        };
        const auto spawn=native.heavenSpawnBlock();check(native.sampleDimensionSpawn()==glm::dvec3(spawn)+glm::dvec3(.5,1.01,.5),"Heaven v9 spawn and single seed derivation");
        dimension.sampleSpawn=nullptr;dimension.spawnUserdata=nullptr;
        std::vector<std::unique_ptr<HeavenStructureReference>> refs;
        auto add=[&](int x,int y,int z,uint64_t variant,StructureLootProfile profile){
            auto ref=std::make_unique<HeavenStructureReference>();ref->generator=&native;ref->instance={{x,y,z},variant,0};
            ref->loot="worldgen_policy_v2:reference_loot_"+std::to_string(unsigned(profile));
            refs.push_back(std::move(ref));return refs.back().get();
        };
        const auto origin=native.originHeavenSkyway();
        auto skyway=[&](const WorldGenerator::HeavenSkyway& a){
            const uint64_t v=WorldGenContext::hashPosition(native.getSeed()^0x534B595741595631ULL,a.worldX,a.mainFloorY,a.worldZ);
            auto* ref=add(a.worldX,a.mainFloorY,a.worldZ,v,a.origin?StructureLootProfile::OriginSkywayShrine:StructureLootProfile::SkywayShrine);
            auto write=[&](int x,int y,int z,BlockId block){ref->blocks.push_back({{x,y,z,uint16_t(block),block==BlockId::CHEST?ref->loot.c_str():nullptr},v});};
            for(int layer=0;layer<5;++layer){const int y=(layer-2)*44;
                for(int dz=-3;dz<=3;++dz)for(int dx=-3;dx<=3;++dx){write(dx,y,dz,((std::abs(dx)+std::abs(dz)+layer)&1)?BlockId::CLOUDSTONE:BlockId::SUNSTONE);
                    if(std::abs(dx)<3&&std::abs(dz)<3){write(dx,y+1,dz,BlockId::AIR);write(dx,y+2,dz,BlockId::AIR);}
                    const bool edge=std::abs(dx)==3||std::abs(dz)==3,door=(dx==0&&std::abs(dz)==3)||(dz==0&&std::abs(dx)==3);
                    if(edge&&!door)write(dx,y+1,dz,BlockId::CLOUDSTONE);
                }
                write(0,y+1,0,BlockId::STAR_CRYSTAL);if(layer==2)write(2,y+1,0,BlockId::CHEST);
            }
        };
        skyway(origin);
        bool ordinary=false;
        for(int z=-2;z<=1&&!ordinary;++z)for(int x=-2;x<=1&&!ordinary;++x)if(const auto a=native.heavenSkywayForCell(x,z)){
            const int dx=a->worldX-origin.worldX,dz=a->worldZ-origin.worldZ;
            if(dx*dx+dz*dz>=24*24){skyway(*a);ordinary=true;}
        }
        check(ordinary,"native regular Skyway fixture");
        for(const auto type:{StructureType::XiguangRuin,StructureType::StarCrystalGeode,StructureType::CloudspireTower}){
            const auto a=native.locateNearestHeavenStructure(type,-130,-130,4096);check(bool(a),"native ordinary landmark fixture");
            const int cell=type==StructureType::XiguangRuin?192:160;
            const int cx=int(std::floor(double(a->worldX)/cell)),cz=int(std::floor(double(a->worldZ)/cell));
            const uint64_t domain=type==StructureType::XiguangRuin?0x4845565255494E53ULL:type==StructureType::StarCrystalGeode?0x48455647454F4445ULL:0x4845565350495245ULL;
            const uint64_t variant=WorldGenContext::hashPosition(native.getSeed()^domain,cx,0,cz);
            auto* ref=add(a->worldX,a->baseY,a->worldZ,variant,structureLootProfile(type));
            const int r=type==StructureType::XiguangRuin?9:type==StructureType::StarCrystalGeode?5:6;
            StructureGenerator::build({0,0,a->baseY,type,variant,a->worldX-r,a->worldX+r,a->worldZ-r,a->worldZ+r},[&](int x,int y,int z,BlockId block){
                ref->blocks.push_back({{x-a->worldX,y-a->baseY,z-a->worldZ,uint16_t(block),block==BlockId::CHEST?ref->loot.c_str():nullptr},WorldGenContext::hashPosition(variant,x,y,z)});
            });
        }
        std::vector<std::string> keys;
        for(size_t index=0;index<refs.size();++index){auto& ref=*refs[index];
            const int ax=ref.instance.anchor[0],ay=ref.instance.anchor[1],az=ref.instance.anchor[2];
            const int cx=int(std::floor(double(ax)/16)),cz=int(std::floor(double(az)/16));
            // Capture original native block entities before installing the adapter.
            std::map<std::tuple<int,int,int>,std::pair<StructureLootProfile,uint64_t>> metadata;
            std::vector<std::unique_ptr<Chunk>> original;
            for(int z=cz-1;z<=cz+1;++z)for(int x=cx-1;x<=cx+1;++x){auto chunk=std::make_unique<Chunk>(x,z);
                FrozenHeavenReference::FrozenHeavenV9::populateHeaven(*chunk,frozen,[&](int wx,int wy,int wz,BlockId block,StructureLootProfile profile,uint64_t lootSeed){if(block==BlockId::CHEST)metadata[{wx,wy,wz}]={profile,lootSeed};});original.push_back(std::move(chunk));}
            Plugins::LootContent table;table.key=ref.loot;table.revision="9";
            auto entry=[&](ItemId item,uint32_t lo,uint32_t hi,uint32_t chance){table.entries.push_back({Plugins::itemKey(item),lo,hi,chance});};
            const auto profile=index==0?StructureLootProfile::OriginSkywayShrine:index==1?StructureLootProfile::SkywayShrine:
                index==2?StructureLootProfile::XiguangRuin:index==3?StructureLootProfile::StarCrystalGeode:StructureLootProfile::CloudspireTower;
            if(index<2){entry(ItemId::STARSTEP_SCEPTER,1,1,index==0?100:25);entry(ItemId::STAR_CRYSTAL,2,5,100);entry(ItemId::SUNSTONE,2,6,index==0?100:90);entry(ItemId::CLOUDSTONE,3,8,100);}
            else if(index==2){entry(ItemId::SUNSTONE,3,9,100);entry(ItemId::STAR_CRYSTAL,3,7,90);entry(ItemId::STARFLOWER,1,4,70);entry(ItemId::CLOUD_BLOOM,1,3,50);}
            else if(index==3){entry(ItemId::STAR_CRYSTAL,2,6,100);entry(ItemId::CLOUDSTONE,2,7,80);entry(ItemId::STARFLOWER,1,4,60);}
            else {entry(ItemId::CLOUDSTONE,2,6,100);entry(ItemId::SUNSTONE,1,4,70);entry(ItemId::STAR_CRYSTAL,1,2,35);entry(ItemId::CLOUD_BLOOM,1,3,45);}
            entry(ItemId::STAR_CRYSTAL_SHARD,2,5,85);entry(ItemId::CLOUDBERRY,2,4,70);
            entry(index==2?ItemId::SUNSTONE_BRICKS:index==3?ItemId::MOONSTONE_BRICKS:ItemId::CLOUDSTONE_BRICKS,3,8,80);
            Plugins::mutableContentState().extensions.loot[table.key]=table;
            Plugins::StructureContent s;s.key="worldgen_policy_v2:reference_"+std::to_string(index);keys.push_back(s.key);s.name=s.key;s.revision="9";s.dimension=1;s.cell=16;s.radius=9;s.minY=-88;s.maxY=90;s.userdata=&ref;s.namespaces={"minecraftc","worldgen_policy_v2"};s.order=1000;
            s.placements=[](const MC_StructureContext* c,int32_t x,int32_t z,MC_StructureInstance* out,uint32_t,uint32_t* count,void* u)->int32_t{
                const auto& r=*static_cast<HeavenStructureReference*>(u);if(c->generation.seed!=r.generator->getSeed())return -1;
                *count=0;if(int(std::floor(double(r.instance.anchor[0])/16))==x&&int(std::floor(double(r.instance.anchor[2])/16))==z){out[0]=r.instance;*count=1;}return 0;
            };
            s.buildV2=[](const MC_StructureContext*,MC_StructureBlockV2* out,uint32_t cap,uint32_t* count,void* u)->int32_t{
                const auto& r=*static_cast<HeavenStructureReference*>(u);if(cap<r.blocks.size())return -1;*count=uint32_t(r.blocks.size());std::copy(r.blocks.begin(),r.blocks.end(),out);return 0;
            };
            Plugins::mutableContentState().extensions.structures.emplace(s.key,s);
            const auto located=native.locatePluginStructure(s.key,ax-3,az-2,16);check(located&&located->anchor[0]==ax&&located->anchor[1]==ay&&located->anchor[2]==az&&located->variant==ref.instance.variant,"native Heaven locate/variant equivalence");
            std::vector<std::unique_ptr<Chunk>> adapted;std::vector<Chunk*> region;for(const auto& a:original){adapted.push_back(std::make_unique<Chunk>(a->cx,a->cz));region.push_back(adapted.back().get());}
            std::reverse(region.begin(),region.end());std::vector<RegionGenerationData::PendingBlock> pending;native.generateRegion(cx-1,cz-1,3,6,region,pending);
            bool checkedChest=false;
            for(size_t i=0;i<original.size();++i){std::vector<uint16_t> a,b;original[i]->copyRawBlocks(a);adapted[i]->copyRawBlocks(b);check(a==b,"Heaven v9 structure geometry/overwrite unchanged");
                auto single=std::make_unique<Chunk>(original[i]->cx,original[i]->cz);native.generate(*single);single->copyRawBlocks(a);check(a==b,"Heaven reference region/singleton equality");
                for(const auto& block:adapted[i]->pluginStructures)if(block.id==BlockId::CHEST&&block.structureKey==s.key){
                    const auto expected=metadata.at({block.worldX,block.worldY,block.worldZ});check(expected.first==profile&&expected.second==block.lootSeed,"native Heaven loot profile/seed equivalence");checkedChest=true;
                    ChunkStore afterStore;afterStore.get(0,0)->generated=true;
                    WorldPersistence after(afterStore);const uint32_t local=Config::worldYToStorageY(100)*256;
                    BlockEntity originalLoot;originalLoot.type=BlockEntityType::Chest;
                    FrozenHeavenReference::populateGeneratedLoot(originalLoot,profile,block.lootSeed);
                    after.registerGeneratedBlockEntityUnlocked(0,0,local,BlockId::CHEST,StructureLootProfile::None,block.lootSeed,&block);
                    const auto* left=&originalLoot;const auto* right=after.getBlockEntity({0,100,0});check(left&&right,"reference loot entities");
                    for(size_t slot=0;slot<left->chest.size();++slot)check(left->chest[slot].id==right->chest[slot].id&&left->chest[slot].count==right->chest[slot].count,"native Heaven chest content equivalence");
                    SaveStore save(root/"reference-save");save.saveBlockEntities(0,0,{{local,*right}});const auto loaded=save.loadBlockEntities(0,0);check(loaded.size()==1&&loaded[0].value.structureKey==s.key&&loaded[0].value.lootSeed==block.lootSeed&&loaded[0].value.lootRevision=="9","Heaven keyed metadata roundtrip");
                }
            }
            check(checkedChest,"reference chest compared");
            Plugins::mutableContentState().extensions.structures.erase(s.key);
        }
        for(const auto& r:refs)Plugins::mutableContentState().extensions.loot.erase(r->loot);
    }
}
}
