#pragma once
#include "Config.h"
#include "world/RedstoneBlocks.h"
#include <map>
#include <set>

namespace Redstone {
// Blocks in flight are owned by this transaction, not by an AIR voxel. The
// journal overlays the whole write set on load until all chunk files are saved.
struct PistonMotion {
    uint64_t id=0;
    Position base;
    State piston{Kind::Piston};
    bool extending=true;
    uint8_t age=0;
    std::vector<Move> moves;
    std::optional<Position> destroyed;
    std::vector<Position> additionalDestroy;
    Position head() const {return base+offset(piston.facing);}
    BlockId headBlock() const {
        return worldBlock(static_cast<NativeBlock>(unsigned(NativeBlock::PISTON_HEAD_FIRST)+unsigned(piston.facing)+(piston.alternate?6:0)));
    }
    std::map<Position,BlockId> edits() const {
        std::map<Position,BlockId> result;
        for(const auto& move:moves){result[move.from]=BlockId::AIR;result[move.to]=BlockId::AIR;}
        if(destroyed)result[*destroyed]=BlockId::AIR;
        for(const auto p:additionalDestroy)result[p]=BlockId::AIR;
        result[head()]=BlockId::AIR;
        auto state=piston;state.power=extending || age<2?15:0;
        result[base]=block(state);
        if(age==2) {
            for(const auto& move:moves)result[move.to]=static_cast<BlockId>(move.block);
            if(extending)result[head()]=headBlock();
        }
        return result;
    }
    bool contains(Position p) const {
        if(p==base || p==head() || (destroyed && p==*destroyed))return true;
        for(const auto& move:moves)if(p==move.from || p==move.to)return true;
        for(const auto other:additionalDestroy)if(p==other)return true;
        return false;
    }
};
struct PistonSweep {glm::dvec3 minimum,maximum;glm::dvec3 delta;};
inline bool validMotion(const PistonMotion& motion) {
    if((motion.piston.power!=0 && motion.piston.power!=15) || motion.piston.delay!=1 || motion.piston.locked)return false;
    if(!motion.id || motion.age>2 || motion.piston.kind!=Kind::Piston || !motion.piston.valid() || motion.moves.size()>12)return false;
    const auto direction=offset(motion.extending?motion.piston.facing:opposite(motion.piston.facing));
    std::set<Position> sources,targets;
    for(const auto& move:motion.moves)
        if(move.to!=move.from+direction || !move.block ||
           !sources.insert(move.from).second || !targets.insert(move.to).second)return false;
    if(motion.additionalDestroy.size()>2 || (!motion.destroyed && !motion.additionalDestroy.empty()))return false;
    std::set<Position> extras;
    for(const auto p:motion.additionalDestroy) {
        const auto origin=*motion.destroyed;
        if(std::abs(int64_t(p.x)-origin.x)+std::abs(int64_t(p.y)-origin.y)+std::abs(int64_t(p.z)-origin.z)!=1 ||
           p==motion.base || sources.count(p) || !extras.insert(p).second)return false;
    }
    if(!motion.extending && (motion.moves.size()>1 || motion.destroyed))return false;
    std::set<Position> expected;
    if(motion.extending) {
        auto p=motion.head();
        for(size_t i=0;i<motion.moves.size();++i,p=p+offset(motion.piston.facing))expected.insert(p);
        if(motion.destroyed && *motion.destroyed!=p)return false;
    } else if(!motion.moves.empty())expected.insert(motion.head()+offset(motion.piston.facing));
    if(sources!=expected)return false;
    for(const auto& edit:motion.edits())
        if(!Config::isValidWorldY(edit.first.y) || std::abs(int64_t(edit.first.x))>30000000 || std::abs(int64_t(edit.first.z))>30000000)return false;
    return true;
}
// Minimal displacement to the leading face of a swept box. All axes use the
// same half-open overlap rules; simulation invokes this once per half-block step.
inline glm::dvec3 sweptPush(const PistonSweep& sweep,glm::dvec3 position,glm::dvec3 size) {
    const auto lo=position-glm::dvec3(size.x*.5,0,size.z*.5),hi=lo+size;
    const auto endLo=sweep.minimum+sweep.delta,endHi=sweep.maximum+sweep.delta;
    const auto sweptLo=glm::min(sweep.minimum,endLo),sweptHi=glm::max(sweep.maximum,endHi);
    for(int axis=0;axis<3;++axis)if(hi[axis]<=sweptLo[axis] || lo[axis]>=sweptHi[axis])return {};
    glm::dvec3 result(0);
    for(int axis=0;axis<3;++axis) {
        if(sweep.delta[axis]>0)result[axis]=std::max(0.0,endHi[axis]-lo[axis]+1e-5);
        if(sweep.delta[axis]<0)result[axis]=std::min(0.0,endLo[axis]-hi[axis]-1e-5);
    }
    return result;
}
} // namespace Redstone
