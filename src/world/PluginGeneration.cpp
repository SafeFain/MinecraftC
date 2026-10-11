#include "world/WorldGenerator.h"
#include "plugins/ContentRegistry.h"
#include <algorithm>
#include <cmath>

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
    for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
        const int wx=chunk.cx*16+x,wz=chunk.cz*16+z;(void)pluginColumn(wx,wz);
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)chunk.setBlock(x,y,z,samplePluginBlock(wx,y,wz));

    }
    applyPluginModifiers(chunk);chunk.generated.store(true);
}
std::vector<MC_DecorationBlock> WorldGenerator::pluginModifierBlocks(int x,int z,int width,int depth) const {
    std::vector<MC_DecorationBlock> result;
    MC_GenerationContext context{sizeof(MC_GenerationContext),static_cast<uint32_t>(m_dimension),m_seed,m_pluginConfig.parameters.c_str()};
    // Canonical anchors and modifier order are independent of chunk/LOD batching.
    for(const auto& selected:m_pluginConfig.modifiers) {
        const auto& m=Plugins::content().extensions.modifiers.at(selected.first);
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
    }
    return result;
}
void WorldGenerator::applyPluginModifiers(Chunk& chunk) const {
    for(const auto& b:pluginModifierBlocks(chunk.worldX(),chunk.worldZ(),16,16))
        chunk.setBlock(b.x-chunk.worldX(),b.y,b.z-chunk.worldZ(),static_cast<BlockId>(b.block));
}
std::array<uint16_t,Config::CHUNK_SIZE_Y> WorldGenerator::samplePluginColumnBlocks(int x,int z) const {
    std::array<uint16_t,Config::CHUNK_SIZE_Y> blocks{};
    for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)
        blocks[static_cast<size_t>(y-Config::WORLD_MIN_Y)]=static_cast<uint16_t>(samplePluginBlock(x,y,z));
    for(const auto& b:pluginModifierBlocks(x,z))blocks[static_cast<size_t>(b.y-Config::WORLD_MIN_Y)]=b.block;
    return blocks;
}
