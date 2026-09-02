#include "entity/GroundNavigation.h"
#include "world/Chunk.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <tuple>

namespace {
void require(bool condition,const char* message) {
    if (!condition) { std::cerr<<"FAILED: "<<message<<'\n'; std::exit(1); }
}
struct TerrainFixture {
    std::map<std::tuple<int,int,int>,BlockId> edits;
    int minimum=-32, maximum=32;
    GroundNavigation::Terrain terrain() {
        return {[this](int x,int y,int z) {
            auto found=edits.find({x,y,z});
            return found!=edits.end() ? found->second : y==0 ? BlockId::STONE : BlockId::AIR;
        },[this](int x,int z) { return x>=minimum && x<maximum && z>=minimum && z<maximum; }};
    }
    void wall(int x,int z1,int z2) {
        for (int z=z1;z<=z2;++z) for (int y=1;y<=3;++y) edits[{x,y,z}]=BlockId::STONE;
    }
};
const glm::vec3 human(.62f,1.8f,.48f);
GroundNavigation::Search solve(TerrainFixture& f,glm::dvec3 from,glm::dvec3 to,
                               glm::vec3 size=human) {
    GroundNavigation::Search search(f.terrain(),size,from,{to,.35,.6});
    int calls=0;
    while (search.status()==GroundNavigation::Status::Pending && calls++<256)
        require(search.advance(f.terrain(),17)<=17,"incremental search exceeded supplied budget");
    require(calls<256,"search did not terminate within its total node budget");
    return search;
}
}
int main() {
    {
        TerrainFixture f; f.wall(3,-3,3);
        auto search=solve(f,{.5,1,.5},{6.5,1,.5});
        require(search.status()==GroundNavigation::Status::Succeeded,"wall detour was not found");
        require(search.path().size()>10,"wall route did not detour");
        for (size_t i=1;i<search.path().size();++i)
            require(GroundNavigation::traverse(f.terrain(),human,search.path()[i-1],search.path()[i]),
                    "returned route contains an invalid edge");
        f.wall(-2,-3,3);
        for (int x=-2;x<=3;++x) for (int y=1;y<=3;++y) f.edits[{x,y,-3}]=BlockId::STONE;
        require(solve(f,{.5,1,.5},{.5,1,-5.5}).status()==GroundNavigation::Status::Succeeded,
                "concave obstacle did not route through its opening");
    }
    {
        TerrainFixture f;
        for(int x=-4;x<=4;++x) for(int y=1;y<=3;++y) {
            f.edits[{x,y,-1}]=BlockId::STONE; f.edits[{x,y,1}]=BlockId::STONE;
        }
        require(solve(f,{.5,1,.5},{3.5,1,.5}).status()==GroundNavigation::Status::Succeeded,
                "human failed one-block corridor");
        require(solve(f,{.5,1,.5},{3.5,1,.5},{1.2f,.55f,1.2f}).status()==GroundNavigation::Status::Unreachable,
                "wide spider fit through a one-block corridor");
        f.edits[{1,2,0}]=BlockId::STONE;
        require(!GroundNavigation::clear(f.terrain(),human,{1.5,1,.5}),"low ceiling admitted tall mob");
        require(GroundNavigation::clear(f.terrain(),{.45f,.65f,.45f},{1.5,1,.5}),"chicken cannot fit low tunnel");
    }
    {
        TerrainFixture f;
        f.edits[{1,1,0}]=BlockId::PLANKS_SLAB_BOTTOM;
        auto slab=GroundNavigation::stand(f.terrain(),human,1.5,.5,1);
        require(slab && slab->y==1.5,"slab support height is incorrect");
        require(GroundNavigation::traverse(f.terrain(),human,{.5,1,.5},*slab),"half step not traversable");
        f.edits[{1,1,0}]=BlockId::PLANKS_STAIRS_BOTTOM_EAST;
        auto stairs=GroundNavigation::stand(f.terrain(),human,1.5,.5,1);
        require(stairs && stairs->y==2,"stair collision footprint lost upper tread");
        f.edits[{1,1,0}]=BlockId::STONE;
        require(GroundNavigation::traverse(f.terrain(),human,{.5,1,.5},{1.5,2,.5}),"one block jump failed");
        f.edits[{0,3,0}]=BlockId::STONE;
        require(!GroundNavigation::traverse(f.terrain(),human,{.5,1,.5},{1.5,2,.5}),"jump ignored start headroom");
        require(!GroundNavigation::traverse(f.terrain(),human,{.5,4,.5},{2.5,1,.5}),"unsafe drop was accepted");
    }
    {
        for (BlockId danger:{BlockId::WATER,BlockId::LAVA,BlockId::FIRE}) {
            TerrainFixture f; f.edits[{1,1,0}]=danger;
            require(!GroundNavigation::stand(f.terrain(),human,1.5,.5,1),"hazard accepted as standing location");
        }
        TerrainFixture f;
        require(solve(f,{-17.5,1,-.5},{-14.5,1,-.5}).status()==GroundNavigation::Status::Succeeded,
                "negative chunk boundary route failed");
        f.maximum=1;
        require(!GroundNavigation::clear(f.terrain(),human,{.9,1,.5}),"footprint crossed unloaded boundary");
        require(solve(f,{.5,1,.5},{2.5,1,.5}).status()==GroundNavigation::Status::Unreachable,
                "path entered missing chunks");
    }
    {
        Chunk chunk(-1,0);
        auto revision=chunk.blockRevision();
        chunk.setSkyLight(0,1,0,15); chunk.setBlockLight(0,1,0,3); chunk.clearLight();
        require(chunk.blockRevision()==revision,"lighting invalidates block navigation revision");
        chunk.setBlock(0,0,0,BlockId::STONE);
        require(chunk.blockRevision()>revision,"block edit did not invalidate navigation");
        revision=chunk.blockRevision(); chunk.setBlock(0,0,0,BlockId::STONE);
        require(chunk.blockRevision()==revision,"identical block write changed navigation revision");
        chunk.finishBulkBlockEdit(); require(chunk.blockRevision()>revision,"bulk edit failed to publish block revision");
    }
    std::cout<<"Ground navigation tests passed\n";
}
