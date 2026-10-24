#include "world/NetherPortal.h"
#include "game/DimensionRules.h"
#include <queue>
#include <set>
#include <tuple>

std::optional<NetherPortalFrame> NetherPortal::find(
    const Read& read,glm::ivec3 p,bool alongZ,bool activeOnly) {
    const auto portal=alongZ?BlockId::NETHER_PORTAL_Z:BlockId::NETHER_PORTAL_X;
    auto interior=[&](glm::ivec3 q) {
        const auto b=read(q);
        return b==portal||(!activeOnly&&(b==BlockId::AIR||b==BlockId::FIRE));
    };
    if(!interior(p))return {};
    const glm::ivec3 u=alongZ?glm::ivec3(0,0,1):glm::ivec3(1,0,0);
    int down=0;
    while(down<21&&interior(p-glm::ivec3(0,1,0))) {--p.y;++down;}
    if(read(p-glm::ivec3(0,1,0))!=BlockId::OBSIDIAN)return {};
    int left=0;
    while(left<21&&interior(p-u)) {p-=u;++left;}
    if(read(p-u)!=BlockId::OBSIDIAN)return {};
    int width=0;
    while(width<22&&interior(p+u*width))++width;
    if(width<2||width>21||read(p+u*width)!=BlockId::OBSIDIAN)return {};
    NetherPortalFrame frame{p,width,0,alongZ};
    for(int x=0;x<width;++x)if(read(frame.at(x,-1))!=BlockId::OBSIDIAN)return {};
    for(int y=0;y<=21;++y) {
        bool top=true;
        for(int x=0;x<width;++x)if(read(frame.at(x,y))!=BlockId::OBSIDIAN)top=false;
        if(top) {
            if(y<3)return {};
            frame.height=y;return frame;
        }
        if(y==21||read(frame.at(-1,y))!=BlockId::OBSIDIAN||read(frame.at(width,y))!=BlockId::OBSIDIAN)return {};
        for(int x=0;x<width;++x)if(!interior(frame.at(x,y)))return {};
    }
    return {};
}

glm::dvec3 NetherPortal::scaledDestination(const glm::dvec3& p,DimensionId source) {
    const auto target=source==DimensionId::Nether?DimensionId::Overworld:DimensionId::Nether;
    const double factor=dimensionRules(source).coordinateScale/dimensionRules(target).coordinateScale;
    return {std::floor(p.x*factor)+.5,p.y,std::floor(p.z*factor)+.5};
}
