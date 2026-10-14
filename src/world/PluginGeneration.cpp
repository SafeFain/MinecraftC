#include "world/WorldGenerator.h"
#include "plugins/ContentRegistry.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <memory>
#include <tuple>

namespace {
int cell(int x){const int q=x/16;return x%16<0?q-1:q;}
}
SurfaceColumn WorldGenerator::pluginColumn(int x,int z) const {
    const auto& g=Plugins::content().extensions.generators.at(m_pluginConfig.generator);
    MC_GenerationContext context{sizeof(MC_GenerationContext),static_cast<uint32_t>(m_dimension),m_seed,m_pluginConfig.parameters.c_str()};
    MC_SurfaceSample sample{sizeof(MC_SurfaceSample),g.height,g.water,static_cast<uint32_t>(Biome::PLAINS)};
    try{if(g.column&&g.column(&context,x,z,&sample,g.userdata))Plugins::failGeneration(g.key);}
    catch(...){Plugins::failGeneration(g.key);}
    if(sample.size<sizeof(sample)||sample.height<Config::WORLD_MIN_Y-1||sample.height>=Config::WORLD_MAX_Y||
        sample.water_level<Config::WORLD_MIN_Y-1||sample.water_level>=Config::WORLD_MAX_Y||sample.biome>=static_cast<uint32_t>(BIOME_COUNT))Plugins::failGeneration(g.key);
    SurfaceColumn column;column.height=column.nominalHeight=sample.height;column.waterLevel=sample.water_level;
    column.biome=static_cast<Biome>(sample.biome);column.densityMinY=Config::WORLD_MIN_Y;column.densityMaxY=sample.height;
    return column;
}
BlockId WorldGenerator::samplePluginBlock(int x,int y,int z) const {
    const auto& g=Plugins::content().extensions.generators.at(m_pluginConfig.generator);
    MC_GenerationContext context{sizeof(MC_GenerationContext),static_cast<uint32_t>(m_dimension),m_seed,m_pluginConfig.parameters.c_str()};
    uint16_t block=static_cast<uint16_t>(BlockId::AIR);
    try {
        if(g.voxel){if(g.voxel(&context,x,y,z,&block,g.userdata))Plugins::failGeneration(g.key);}
        else {const auto c=pluginColumn(x,z);block=y<c.height?g.fill:y==c.height?g.surface:y<=c.waterLevel?static_cast<uint16_t>(BlockId::WATER):0;}
    }catch(...){Plugins::failGeneration(g.key);}
    if(!Plugins::validBlock(static_cast<BlockId>(block)))Plugins::failGeneration(g.key);
    return static_cast<BlockId>(block);
}
void WorldGenerator::generatePluginChunk(Chunk& chunk) const {
    generatePluginChunks({&chunk});
}
void WorldGenerator::generatePluginChunks(const std::vector<Chunk*>& chunks) const {
    const auto& g=Plugins::content().extensions.generators.at(m_pluginConfig.generator);
    std::vector<Chunk*> targets;
    for(auto* chunk:chunks)if(chunk)targets.push_back(chunk);
    if(targets.empty())return;
    std::set<std::pair<int,int>> positions;
    int minX=targets.front()->cx,maxX=minX,minZ=targets.front()->cz,maxZ=minZ;
    for(auto* chunk:targets) {
        if(!positions.emplace(chunk->cx,chunk->cz).second)Plugins::failGeneration(g.key);
        minX=std::min(minX,chunk->cx);maxX=std::max(maxX,chunk->cx);
        minZ=std::min(minZ,chunk->cz);maxZ=std::max(maxZ,chunk->cz);
    }
    const int64_t width=(int64_t(maxX)-minX+1)*16,depth=(int64_t(maxZ)-minZ+1)*16;
    const bool rectangle=width<=MC_MAX_GENERATION_SIDE&&depth<=MC_MAX_GENERATION_SIDE&&
        int64_t(targets.size())==(width/16)*(depth/16);
    MC_GenerationContext context{sizeof(MC_GenerationContext),static_cast<uint32_t>(m_dimension),m_seed,m_pluginConfig.parameters.c_str()};
    auto sampleVolume=[&](int x,int z,uint32_t w,uint32_t d) {
        const uint32_t count=w*d*Config::CHUNK_SIZE_Y;
        // An unwritten slot is invalid, rather than silently becoming AIR.
        std::vector<uint16_t> blocks(count,MC_INVALID_ID);
        MC_GenerationVolume volume{sizeof(MC_GenerationVolume),x,Config::WORLD_MIN_Y,z,w,Config::CHUNK_SIZE_Y,d};
        try{if(g.batch(&context,&volume,blocks.data(),count,g.userdata))Plugins::failGeneration(g.key);}
        catch(...){Plugins::failGeneration(g.key);}
        for(auto b:blocks)if(!Plugins::validBlock(static_cast<BlockId>(b)))Plugins::failGeneration(g.key);
        return blocks;
    };
    std::vector<uint16_t> region;
    if(g.batch&&rectangle)region=sampleVolume(minX*16,minZ*16,static_cast<uint32_t>(width),static_cast<uint32_t>(depth));
    // Stage the complete request, including modifiers and metadata, before
    // publishing any target. Faults leave targets and their flags untouched.
    std::vector<std::unique_ptr<Chunk>> staged;
    for(auto* target:targets) {
        auto chunk=std::make_unique<Chunk>(target->cx,target->cz);
        if(g.batch) {
            const auto local=rectangle?std::vector<uint16_t>{}:sampleVolume(chunk->worldX(),chunk->worldZ(),16,16);
            for(int y=0;y<Config::CHUNK_SIZE_Y;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
                const size_t index=rectangle?size_t(chunk->worldX()-minX*16+x)+
                    size_t(chunk->worldZ()-minZ*16+z)*size_t(width)+size_t(y)*size_t(width)*size_t(depth):
                    size_t(x+z*16+y*256);
                chunk->blockAt(x,y+Config::WORLD_MIN_Y,z)=rectangle?region[index]:local[index];
            }
            chunk->finishBulkBlockEdit();
        } else {
            for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
                const int wx=chunk->worldX()+x,wz=chunk->worldZ()+z;(void)pluginColumn(wx,wz);
                for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
                    chunk->setBlock(x,y,z,samplePluginBlock(wx,y,wz));
            }
        }
        applyPluginModifiers(*chunk);staged.push_back(std::move(chunk));
    }
    for(size_t i=0;i<targets.size();++i) {
        std::vector<uint16_t> blocks;staged[i]->copyRawBlocks(blocks);
        targets[i]->loadRawBlocks(blocks);
        targets[i]->pluginStructures=std::move(staged[i]->pluginStructures);
    }
    for(auto* chunk:targets){chunk->generated.store(true);chunk->generationInProgress.store(false);}
}
namespace {
std::vector<MC_DecorationBlock> modifierBlocks(const Plugins::ModifierContent& m,
    const MC_GenerationContext& context,int x,int z,int width,int depth) {
    std::vector<MC_DecorationBlock> result;
    const int radius=static_cast<int>(m.radius);
    for(int az=cell(z-radius);az<=cell(z+depth-1+radius);++az)
        for(int ax=cell(x-radius);ax<=cell(x+width-1+radius);++ax) {
            std::array<MC_DecorationBlock,4096> output{};uint32_t count=0;
            try {
                if(m.decorate(&context,ax*16,az*16,output.data(),static_cast<uint32_t>(output.size()),&count,m.userdata))Plugins::failGeneration(m.key);
            }catch(...){Plugins::failGeneration(m.key);}
            if(count>output.size())Plugins::failGeneration(m.key);
            for(uint32_t i=0;i<count;++i) {
                const auto& b=output[i];
                if(std::abs(int64_t(b.x)-ax*16)>radius||std::abs(int64_t(b.z)-az*16)>radius||
                   !Config::isValidWorldY(b.y)||!Plugins::validBlock(static_cast<BlockId>(b.block)))Plugins::failGeneration(m.key);
                if(b.x>=x&&b.x<x+width&&b.z>=z&&b.z<z+depth)result.push_back(b);
            }
        }
    return result;
}
}
std::vector<MC_DecorationBlock> WorldGenerator::pluginModifierBlocks(int x,int z,int width,int depth,bool includeStructures) const {
    std::vector<MC_DecorationBlock> result;
    MC_GenerationContext context{sizeof(MC_GenerationContext),static_cast<uint32_t>(m_dimension),m_seed,m_pluginConfig.parameters.c_str()};
    // Canonical anchors and modifier order are independent of chunk/LOD batching.
    for(const auto& selected:m_pluginConfig.modifiers) {
        const auto& m=Plugins::content().extensions.modifiers.at(selected.first);
        const auto blocks=modifierBlocks(m,context,x,z,width,depth);
        result.insert(result.end(),blocks.begin(),blocks.end());
    }
    if(includeStructures)for(const auto& b:pluginStructureBlocks(x,z,width,depth))result.push_back({b.worldX,b.worldY,b.worldZ,static_cast<uint16_t>(b.id)});
    return result;
}
void WorldGenerator::applyPluginModifiers(Chunk& chunk) const {
    for(const auto& b:pluginModifierBlocks(chunk.worldX(),chunk.worldZ(),16,16,false))
        chunk.setBlock(b.x-chunk.worldX(),b.y,b.z-chunk.worldZ(),static_cast<BlockId>(b.block));
    chunk.pluginStructures=pluginStructureBlocks(chunk.worldX(),chunk.worldZ());
    for(const auto& b:chunk.pluginStructures)chunk.setBlock(b.worldX-chunk.worldX(),b.worldY,b.worldZ-chunk.worldZ(),b.id);
}
std::array<uint16_t,Config::CHUNK_SIZE_Y> WorldGenerator::samplePluginColumnBlocks(int x,int z) const {
    std::array<uint16_t,Config::CHUNK_SIZE_Y> blocks{};
    for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
        blocks[static_cast<size_t>(y-Config::WORLD_MIN_Y)]=static_cast<uint16_t>(samplePluginBlock(x,y,z));
    for(const auto& b:pluginModifierBlocks(x,z))blocks[static_cast<size_t>(b.y-Config::WORLD_MIN_Y)]=b.block;
    return blocks;
}

namespace {
int floorCell(int x,int size){const int q=x/size;return x%size<0?q-1:q;}
uint64_t keyHash(const std::string& key){uint64_t h=1469598103934665603ULL;for(unsigned char c:key){h^=c;h*=1099511628211ULL;}return h;}
struct PluginAnchor {int x=0,y=0,z=0;uint64_t variant=0;};
std::optional<PluginAnchor> candidate(const WorldGenerator& g,const Plugins::StructureContent& s,int cx,int cz){
    const uint64_t v=WorldGenContext::hashPosition(g.getSeed()^s.domain^keyHash(s.key),cx,0,cz);
    if(v%100>=s.chance)return {};
    const int x=cx*static_cast<int>(s.cell)+static_cast<int>((v>>8)%s.cell),z=cz*static_cast<int>(s.cell)+static_cast<int>((v>>32)%s.cell);
    const auto surface=g.sampleTerrainColumn(x,z);
    const int y=surface.height+1;
    if(y<=surface.waterLevel||y<Config::WORLD_MIN_Y||y+static_cast<int>(s.height)>Config::WORLD_MAX_Y)return {};
    int lo=surface.height,hi=lo;
    for(int dz:{-static_cast<int>(s.radius),static_cast<int>(s.radius)})for(int dx:{-static_cast<int>(s.radius),static_cast<int>(s.radius)}){const auto c=g.sampleTerrainColumn(x+dx,z+dz);if(c.height<=c.waterLevel)return {};lo=std::min(lo,c.height);hi=std::max(hi,c.height);}
    if(hi-lo>static_cast<int>(s.tolerance))return {};
    return PluginAnchor{x,y,z,v};
}
bool accepted(const WorldGenerator& g,const Plugins::StructureContent& s,const PluginAnchor& a){
    // A total priority order resolves reservations without scheduling or recursion.
    for(const auto& pair:Plugins::content().extensions.structures){const auto& other=pair.second;if(other.dimension!=s.dimension)continue;const int radius=static_cast<int>(s.radius+other.radius);
        for(int cz=floorCell(a.z-radius,other.cell);cz<=floorCell(a.z+radius,other.cell);++cz)for(int cx=floorCell(a.x-radius,other.cell);cx<=floorCell(a.x+radius,other.cell);++cx){const auto b=candidate(g,other,cx,cz);if(!b||std::abs(int64_t(b->x)-a.x)>radius||std::abs(int64_t(b->z)-a.z)>radius)continue;if(other.key==s.key&&b->x==a.x&&b->z==a.z)continue;
            if(std::tie(b->variant,other.key,b->x,b->z)<std::tie(a.variant,s.key,a.x,a.z))return false;}}
    return true;
}
}
std::vector<RegionGenerationData::PendingBlock> WorldGenerator::pluginStructureBlocks(int x,int z,int width,int depth) const{
    std::vector<RegionGenerationData::PendingBlock> result;
    for(const auto& pair:Plugins::content().extensions.structures){const auto& s=pair.second;if(s.dimension!=static_cast<uint32_t>(m_dimension))continue;const int r=static_cast<int>(s.radius);
        for(int cz=floorCell(z-r,s.cell);cz<=floorCell(z+depth-1+r,s.cell);++cz)for(int cx=floorCell(x-r,s.cell);cx<=floorCell(x+width-1+r,s.cell);++cx){const auto a=candidate(*this,s,cx,cz);if(!a||!accepted(*this,s,*a))continue;
            std::vector<Plugins::StructureContent::Block> blocks=s.blocks;
            if(s.build){std::set<std::tuple<int32_t,int32_t,int32_t>> positions;std::vector<MC_StructureBlock> out(MC_MAX_STRUCTURE_BLOCKS);uint32_t count=0;MC_StructureContext c{};c.size=sizeof(c);c.generation={sizeof(MC_GenerationContext),s.dimension,m_seed,m_pluginConfig.parameters.c_str()};c.anchor[0]=a->x;c.anchor[1]=a->y;c.anchor[2]=a->z;c.variant=a->variant;c.reader=const_cast<WorldGenerator*>(this);
                c.surface=[](void* reader,int32_t x,int32_t z,MC_SurfaceSample* out)->int32_t{if(!out||out->size<sizeof(*out)||std::abs(int64_t(x))>30000000||std::abs(int64_t(z))>30000000)return -1;const auto v=static_cast<WorldGenerator*>(reader)->sampleTerrainColumn(x,z);*out={sizeof(*out),v.height,v.waterLevel,static_cast<uint32_t>(v.biome)};return 0;};
                try{if(s.build(&c,out.data(),static_cast<uint32_t>(out.size()),&count,s.userdata)||count>out.size())Plugins::failGeneration(s.key);for(uint32_t i=0;i<count;++i){const auto& b=out[i];if(!positions.emplace(b.x,b.y,b.z).second)Plugins::failGeneration(s.key);const std::string loot=b.loot?b.loot:"";if(std::abs(int64_t(b.x))>r||std::abs(int64_t(b.z))>r||b.y<0||uint32_t(b.y)>=s.height||!Plugins::validBlock(static_cast<BlockId>(b.block))||(!loot.empty()&&(b.block!=static_cast<uint16_t>(BlockId::CHEST)||!Plugins::content().extensions.loot.count(loot))))Plugins::failGeneration(s.key);const auto key=Plugins::blockKey(static_cast<BlockId>(b.block));if(std::find(s.namespaces.begin(),s.namespaces.end(),key.substr(0,key.find(':')))==s.namespaces.end())Plugins::failGeneration(s.key);if(!loot.empty()&&std::find(s.namespaces.begin(),s.namespaces.end(),loot.substr(0,loot.find(':')))==s.namespaces.end())Plugins::failGeneration(s.key);blocks.push_back({b.x,b.y,b.z,b.block,loot});}}
                catch(...){Plugins::failGeneration(s.key);}}
            for(const auto& b:blocks){const int wx=a->x+b.x,wz=a->z+b.z;if(wx<x||wx>=x+width||wz<z||wz>=z+depth)continue;RegionGenerationData::PendingBlock p;p.worldX=wx;p.worldY=a->y+b.y;p.worldZ=wz;p.id=static_cast<BlockId>(b.id);p.overwrite=true;p.needsBlockEntity=p.id==BlockId::CHEST||p.id==BlockId::FURNACE;p.lootSeed=WorldGenContext::hashPosition(a->variant,wx,p.worldY,wz);p.lootKey=b.loot;if(!b.loot.empty())p.lootRevision=Plugins::content().extensions.loot.at(b.loot).revision;p.structureKey=s.key;p.structureRevision=s.revision;result.push_back(std::move(p));}
        }}
    return result;
}
std::optional<MC_LocatedStructure> WorldGenerator::locatePluginStructure(const std::string& key,int x,int z,uint32_t range) const{
    const auto found=Plugins::content().extensions.structures.find(key);if(found==Plugins::content().extensions.structures.end()||found->second.dimension!=static_cast<uint32_t>(m_dimension)||range>8192)return {};
    const auto& s=found->second;if(!s.chance)return {};std::optional<MC_LocatedStructure> best;int64_t distance=int64_t(range)*range;const int originX=floorCell(x,s.cell),originZ=floorCell(z,s.cell),rings=static_cast<int>(range/s.cell)+2;
    auto visit=[&](int dx,int dz){const auto a=candidate(*this,s,originX+dx,originZ+dz);if(!a)return;const int64_t ax=int64_t(a->x)-x,az=int64_t(a->z)-z,d=ax*ax+az*az;if(d>distance||!accepted(*this,s,*a))return;if(d==distance&&best&&std::tie(a->x,a->z)>=std::tie(best->anchor[0],best->anchor[2]))return;distance=d;MC_LocatedStructure v{};v.size=sizeof(v);v.dimension=s.dimension;v.anchor[0]=a->x;v.anchor[1]=a->y;v.anchor[2]=a->z;v.variant=a->variant;best=v;};
    for(int ring=0;ring<=rings;++ring){
        if(!ring)visit(0,0);
        else {for(int dx=-ring;dx<=ring;++dx){visit(dx,-ring);visit(dx,ring);}for(int dz=-ring+1;dz<ring;++dz){visit(-ring,dz);visit(ring,dz);}}
        // Unvisited cells lie beyond this conservative lower distance bound.
        const int64_t lower=int64_t(std::max(0,ring-1))*s.cell;if(best&&lower*lower>distance)break;}
    return best;
}
uint64_t WorldGenerator::pluginStructureFingerprint() const{
    uint64_t h=0;bool found=false;
    for(const auto& pair:Plugins::content().extensions.structures)if(pair.second.dimension==static_cast<uint32_t>(m_dimension)){
        found=true;const auto description=Plugins::content().networkDescription.find("structure/"+pair.first);
        h=WorldGenContext::mix(h^keyHash(pair.first)^keyHash(pair.second.revision)^pair.second.domain^0x7374727563747631ULL);
        if(description!=Plugins::content().networkDescription.end())h=WorldGenContext::mix(h^keyHash(description->second));
    }
    return found?(h?h:1):0;
}

uint64_t WorldGenerator::lodSamplingFingerprint() const {
    uint64_t h=Plugins::generationFingerprint(m_pluginConfig)^pluginStructureFingerprint();
    auto add=[&](const std::string& value){h=WorldGenContext::mix(h^keyHash(value));};
    // Invalidate plugin approximations from the previous builtin-only policy.
    if(customGenerator()||!m_pluginConfig.modifiers.empty())
        h=WorldGenContext::mix(h^0x6c6f6473616d7032ULL);
    if(customGenerator()) {
        const auto& g=Plugins::content().extensions.generators.at(m_pluginConfig.generator);
        if(g.batch||g.terrainLod||g.decorationLod) {
            add(g.lodRevision);h=WorldGenContext::mix(h^uint64_t(bool(g.batch))^
                (uint64_t(bool(g.terrainLod))<<1)^(uint64_t(bool(g.decorationLod))<<2));
        }
    }
    for(const auto& selected:m_pluginConfig.modifiers) {
        const auto& m=Plugins::content().extensions.modifiers.at(selected.first);
        if(m.decorationLod){add(m.key);add(m.lodRevision);h=WorldGenContext::mix(h^0x6c6f646d6f647632ULL);}
    }
    return h;
}
std::vector<MC_LodSpan> WorldGenerator::samplePluginLodColumn(int x,int z,int cellSize,
    const std::vector<MC_LodSpan>& base) const {
    if(cellSize<1||cellSize>8192||(cellSize&(cellSize-1)))Plugins::failGeneration("LOD cell");
    const int wx=x+cellSize/2,wz=z+cellSize/2;
    MC_GenerationContext context{sizeof(MC_GenerationContext),static_cast<uint32_t>(m_dimension),m_seed,m_pluginConfig.parameters.c_str()};
    MC_LodCell cellRequest{sizeof(MC_LodCell),x,z,static_cast<uint32_t>(cellSize)};
    // Compose sparse intervals directly. Work depends on spans/overwrites,
    // rather than world height, when all capabilities are provided.
    std::vector<MC_LodSpan> spans=base;
    auto overwrite=[&](const MC_LodSpan& b) {
        std::vector<MC_LodSpan> next;
        for(const auto& s:spans) {
            if(s.top<b.bottom||s.bottom>b.top)next.push_back(s);
            else {
                if(s.bottom<b.bottom)next.push_back({s.bottom,b.bottom-1,s.block});
                if(s.top>b.top)next.push_back({b.top+1,s.top,s.block});
            }
        }
        if(b.block)next.push_back(b);
        std::sort(next.begin(),next.end(),[](const auto& a,const auto& b){return a.bottom<b.bottom;});
        spans.clear();
        for(const auto& s:next) {
            if(!spans.empty()&&spans.back().block==s.block&&spans.back().top+1==s.bottom)spans.back().top=s.top;
            else spans.push_back(s);
        }
    };
    // Native approximate decoration may overlap its base. Canonicalize its
    // paint order before adding plugin overlays, just as the legacy dense
    // modifier composition did, without scanning all 384 heights.
    if(!customGenerator()) {
        auto native=std::move(spans);spans.clear();
        for(const auto& s:native)overwrite(s);
    }
    auto callback=[&](MC_LodCallback f,void* userdata,const std::string& key,bool terrain) {
        std::array<MC_LodSpan,MC_MAX_LOD_SPANS> out{};
        for(auto& s:out)s.block=MC_INVALID_ID;
        uint32_t count=UINT32_MAX;
        try{if(f(&context,&cellRequest,out.data(),static_cast<uint32_t>(out.size()),&count,userdata))Plugins::failGeneration(key);}
        catch(...){Plugins::failGeneration(key);}
        if(count>out.size())Plugins::failGeneration(key);
        for(uint32_t i=0;i<count;++i) {
            const auto& s=out[i];
            if(s.bottom>s.top||!Config::isValidWorldY(s.bottom)||!Config::isValidWorldY(s.top)||
                !Plugins::validBlock(static_cast<BlockId>(s.block))||
                (terrain&&(!s.block||(i&&out[i-1].top>=s.bottom))))Plugins::failGeneration(key);
        }
        if(terrain)spans.assign(out.begin(),out.begin()+count);
        else for(uint32_t i=0;i<count;++i)overwrite(out[i]);
    };
    if(customGenerator()) {
        const auto& g=Plugins::content().extensions.generators.at(m_pluginConfig.generator);
        spans.clear();
        if(g.terrainLod)callback(g.terrainLod,g.userdata,g.key,true);
        else {
            for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y) {
                const auto block=static_cast<uint16_t>(samplePluginBlock(wx,y,wz));
                if(!block)continue;
                if(!spans.empty()&&spans.back().block==block&&spans.back().top==y-1)spans.back().top=y;
                else spans.push_back({y,y,block});
            }
        }
        if(g.decorationLod)callback(g.decorationLod,g.userdata,g.key,false);
    }
    for(const auto& selected:m_pluginConfig.modifiers) {
        const auto& m=Plugins::content().extensions.modifiers.at(selected.first);
        if(m.decorationLod)callback(m.decorationLod,m.userdata,m.key,false);
        else for(const auto& b:modifierBlocks(m,context,wx,wz,1,1))overwrite({b.y,b.y,b.block});
    }
    for(const auto& b:pluginStructureBlocks(wx,wz,1,1))overwrite({b.worldY,b.worldY,static_cast<uint16_t>(b.id)});
    return spans;
}
