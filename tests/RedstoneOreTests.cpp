#include "world/OreGenerator.h"
#include "world/Noise.h"
#include "world/RedstoneBlocks.h"
#include <cstdlib>
#include <iostream>

void check(bool condition,const char* message) {if(!condition){std::cerr<<message<<'\n';std::exit(1);}}
bool redstone(BlockId id) {return Redstone::native(id)&&Redstone::ore(Redstone::nativeSlot(id));}
int main() {
    static_assert(WorldGenContext::GENERATION_VERSION==23);
    check(!WorldGenContext::canLoadGeneration(22)&&WorldGenContext::canLoadGeneration(23)&&!WorldGenContext::canLoadGeneration(24),"generation admission");
    Noise noise(42);OreGenerator first(noise,42),same(noise,42),other(noise,43);
    unsigned found=0,variation=0;
    for(int z=-24;z<24;++z)for(int x=-24;x<24;++x)for(int y=-64;y<16;++y) {
        const auto a=first.getOre(float(x),float(y),float(z),BlockId::STONE);
        const auto b=same.getOre(float(x),float(y),float(z),BlockId::STONE);
        const auto c=other.getOre(float(x),float(y),float(z),BlockId::STONE);
        check(a==b,"same-seed ore output changed");
        if(redstone(a)) {
            ++found;check(a==Redstone::worldBlock(Redstone::NativeBlock::REDSTONE_ORE),"ordinary host ore variant");
            const auto deep=first.getOre(float(x),float(y),float(z),BlockId::DEEPSLATE);
            check(deep==Redstone::worldBlock(Redstone::NativeBlock::DEEPSLATE_REDSTONE_ORE),"deepslate host ore variant");
            variation+=redstone(c)?0:1;
        }
    }
    check(found>50 && variation>25,"redstone distribution or seed separation");
    // Reverse traversal exercises cache eviction and negative region boundaries.
    for(int z=23;z>=-24;--z)for(int x=23;x>=-24;--x) {
        check(first.getOre(float(x),-48,float(z),BlockId::STONE)==same.getOre(float(x),-48,float(z),BlockId::STONE),"ore query-order dependence");
        check(!redstone(first.getOre(float(x),16,float(z),BlockId::STONE)),"redstone above height range");
        check(first.getOre(float(x),-48,float(z),BlockId::BEDROCK)==BlockId::AIR,"redstone replaced bedrock");
    }
    std::cout<<"Redstone ore determinism passed: "<<found<<" samples\n";
}
