#include "entity/GroundNavigation.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GroundNavigation {
namespace {
constexpr double epsilon = 1e-5;
bool hazard(BlockId block) {
    return isWater(block) || isLava(block) || block == BlockId::FIRE;
}
bool overlapsXZ(const glm::dvec3& p, const glm::vec3& size,
                int x, int z, const BlockCollisionBox& box) {
    return p.x-size.x*.5 < x+box.max.x-epsilon &&
           p.x+size.x*.5 > x+box.min.x+epsilon &&
           p.z-size.z*.5 < z+box.max.z-epsilon &&
           p.z+size.z*.5 > z+box.min.z+epsilon;
}
}

bool clear(const Terrain& terrain, const glm::vec3& size,
           const glm::dvec3& p, bool avoidHazards) {
    if (p.y < Config::WORLD_MIN_Y || p.y+size.y > Config::WORLD_MAX_Y) return false;
    const int minX=static_cast<int>(std::floor(p.x-size.x*.5+epsilon));
    const int maxX=static_cast<int>(std::floor(p.x+size.x*.5-epsilon));
    const int minZ=static_cast<int>(std::floor(p.z-size.z*.5+epsilon));
    const int maxZ=static_cast<int>(std::floor(p.z+size.z*.5-epsilon));
    for (int z=minZ;z<=maxZ;++z) for (int x=minX;x<=maxX;++x) {
        if (!terrain.loaded(x,z)) return false;
        for (int y=static_cast<int>(std::floor(p.y+epsilon));
             y<=static_cast<int>(std::floor(p.y+size.y-epsilon));++y) {
            const BlockId block=terrain.block(x,y,z);
            if (avoidHazards && hazard(block)) return false;
            const auto boxes=blockCollisionBoxes(block);
            for (uint8_t i=0;i<boxes.count;++i) {
                const auto& box=boxes.boxes[i];
                if (overlapsXZ(p,size,x,z,box) && p.y < y+box.max.y-epsilon &&
                    p.y+size.y > y+box.min.y+epsilon) return false;
            }
        }
    }
    return true;
}

bool supported(const Terrain& terrain, const glm::vec3& size,
               const glm::dvec3& p) {
    const int y=static_cast<int>(std::floor(p.y-epsilon));
    for (int z=static_cast<int>(std::floor(p.z-size.z*.5+epsilon));
         z<=static_cast<int>(std::floor(p.z+size.z*.5-epsilon));++z)
        for (int x=static_cast<int>(std::floor(p.x-size.x*.5+epsilon));
             x<=static_cast<int>(std::floor(p.x+size.x*.5-epsilon));++x) {
            if (!terrain.loaded(x,z)) continue;
            const auto boxes=blockCollisionBoxes(terrain.block(x,y,z));
            for (uint8_t i=0;i<boxes.count;++i)
                if (overlapsXZ(p,size,x,z,boxes.boxes[i]) &&
                    std::abs(y+boxes.boxes[i].max.y-p.y) < .025) return true;
        }
    return false;
}

std::optional<glm::dvec3> stand(const Terrain& terrain, const glm::vec3& size,
                               double x, double z, double referenceY,
                               double up, double down) {
    const glm::dvec3 p(x,referenceY,z);
    std::vector<double> heights;
    for (int bz=static_cast<int>(std::floor(z-size.z*.5+epsilon));
         bz<=static_cast<int>(std::floor(z+size.z*.5-epsilon));++bz)
        for (int bx=static_cast<int>(std::floor(x-size.x*.5+epsilon));
             bx<=static_cast<int>(std::floor(x+size.x*.5-epsilon));++bx) {
            if (!terrain.loaded(bx,bz)) return std::nullopt;
            for (int by=std::max(Config::WORLD_MIN_Y,
                                static_cast<int>(std::floor(referenceY-down-1)));
                 by<=std::min(Config::WORLD_MAX_Y-1,
                              static_cast<int>(std::floor(referenceY+up)));++by) {
                const auto boxes=blockCollisionBoxes(terrain.block(bx,by,bz));
                for (uint8_t i=0;i<boxes.count;++i) {
                    const double height=by+boxes.boxes[i].max.y;
                    if (height>=referenceY-down-epsilon && height<=referenceY+up+epsilon &&
                        overlapsXZ(p,size,bx,bz,boxes.boxes[i])) heights.push_back(height);
                }
            }
        }
    // Prefer the closest support, then higher ground, independent of query order.
    std::sort(heights.begin(),heights.end(),[referenceY](double a,double b) {
        const double da=std::abs(a-referenceY), db=std::abs(b-referenceY);
        return da==db ? a>b : da<db;
    });
    heights.erase(std::unique(heights.begin(),heights.end()),heights.end());
    for (double y:heights) {
        const glm::dvec3 candidate(x,y,z);
        if (clear(terrain,size,candidate)) return candidate;
    }
    return std::nullopt;
}

bool traverse(const Terrain& terrain, const glm::vec3& size,
              const glm::dvec3& from, const glm::dvec3& to) {
    const double rise=to.y-from.y;
    if (rise>Config::AI_MAX_JUMP_HEIGHT+epsilon || rise < -Config::AI_MAX_DROP-epsilon ||
        !clear(terrain,size,from) || !clear(terrain,size,to) ||
        !supported(terrain,size,to)) return false;
    const bool jumping=rise>Config::AI_STEP_HEIGHT+epsilon;
    const double top=jumping ? from.y+Config::AI_JUMP_SPEED*Config::AI_JUMP_SPEED/
                                         (2.0*Config::AI_GRAVITY) : std::max(from.y,to.y);
    // Swept headroom on both sides of a jump; a clear destination alone is insufficient.
    if (jumping) {
        for (double y=from.y; y<=top+epsilon; y+=.1)
            if (!clear(terrain,size,{from.x,y,from.z})) return false;
    }
    const double length=std::hypot(to.x-from.x,to.z-from.z);
    const int steps=std::max(1,static_cast<int>(std::ceil(length/Config::AI_COLLISION_STEP)));
    for (int i=1;i<=steps;++i) {
        const double t=static_cast<double>(i)/steps;
        glm::dvec3 p=from+(to-from)*t;
        if (jumping) p.y=top;
        else {
            auto support=stand(terrain,size,p.x,p.z,from.y,
                               Config::AI_STEP_HEIGHT,Config::AI_MAX_DROP);
            if (!support) return false;
            p.y=std::max(from.y,support->y);
        }
        if (!clear(terrain,size,p)) return false;
    }
    // Descending along the destination column must also be clear.
    for (double y=to.y;y<top;y+=.1)
        if (!clear(terrain,size,{to.x,y,to.z})) return false;
    return true;
}

bool reached(const glm::dvec3& p, const Goal& goal) {
    const auto near=[&](const glm::dvec3& target) {
        return std::hypot(p.x-target.x,p.z-target.z)<=goal.radius &&
               std::abs(p.y-target.y)<=goal.verticalTolerance;
    };
    if (near(goal.position)) return true;
    return std::any_of(goal.alternatives.begin(),goal.alternatives.end(),near);
}

size_t Search::Hash::operator()(const Key& k) const {
    uint64_t value=static_cast<uint32_t>(k.x)*0x9e3779b185ebca87ULL;
    value^=static_cast<uint32_t>(k.z)*0xc2b2ae3d27d4eb4fULL;
    value^=static_cast<uint32_t>(k.y)*0x165667b19e3779f9ULL;
    return static_cast<size_t>(value^(value>>32));
}
Search::Key Search::key(const glm::dvec3& p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::llround(p.y*16)),
            static_cast<int>(std::floor(p.z))};
}
double Search::heuristic(const glm::dvec3& p) const {
    double result=std::hypot(p.x-m_goal.position.x,p.z-m_goal.position.z);
    for (const auto& target:m_goal.alternatives)
        result=std::min(result,std::hypot(p.x-target.x,p.z-target.z));
    return std::max(0.0,result-m_goal.radius);
}
Search::Search(const Terrain& terrain, const glm::vec3& size,
               const glm::dvec3& start, const Goal& goal)
    : m_size(size), m_start(start), m_goal(goal) {
    auto initial=stand(terrain,size,std::floor(start.x)+.5,std::floor(start.z)+.5,
                       start.y,.5,2.0);
    if (!initial || !traverse(terrain,size,start,*initial)) {
        m_status=Status::Unreachable;
        return;
    }
    m_nodes.reserve(Config::AI_PATH_NODE_LIMIT);
    m_lookup.reserve(Config::AI_PATH_NODE_LIMIT);
    m_nodes.push_back({*initial,0.0,0,false});
    m_lookup.emplace(key(*initial),0);
    m_open.push({heuristic(*initial),0.0,0});
}

size_t Search::advance(const Terrain& terrain, size_t budget) {
    size_t used=0;
    if (m_status==Status::Pending && m_expanded==0 && budget>0 &&
        glm::distance(m_nodes.front().position,m_goal.position)<=Config::AI_DIRECT_PATH_DISTANCE) {
        // Open ground does not need a graph search. Charge the bounded swept
        // segment to the same queue so simultaneous wander starts stay scheduled.
        ++used; ++m_expanded;
        auto destination=stand(terrain,m_size,m_goal.position.x,m_goal.position.z,
                               m_goal.position.y,.5,.5);
        if (destination && reached(*destination,m_goal) &&
            traverse(terrain,m_size,m_nodes.front().position,*destination)) {
            m_path={m_nodes.front().position,*destination};
            m_status=Status::Succeeded;
            return used;
        }
    }
    while (m_status==Status::Pending && used<budget && !m_open.empty()) {
        if (m_expanded>=Config::AI_PATH_NODE_LIMIT) {
            m_status=Status::Unreachable;
            break;
        }
        const Entry entry=m_open.top(); m_open.pop();
        // Charge stale heap entries too, bounding all expansion-loop work.
        ++used; ++m_expanded;
        Node& node=m_nodes[entry.index];
        if (node.closed || entry.cost!=node.cost) continue;
        node.closed=true;
        const glm::dvec3 p=node.position;
        const double cost=node.cost;
        if (reached(p,m_goal)) {
            size_t index=entry.index;
            for (;;) {
                m_path.push_back(m_nodes[index].position);
                if (index==0) break;
                index=m_nodes[index].parent;
            }
            std::reverse(m_path.begin(),m_path.end());
            m_status=Status::Succeeded;
            break;
        }
        if (m_expanded>=Config::AI_PATH_NODE_LIMIT) {
            m_status=Status::Unreachable;
            break;
        }
        static constexpr int directions[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
        for (const auto& d:directions) {
            auto next=stand(terrain,m_size,p.x+d[0],p.z+d[1],p.y);
            if (!next || glm::distance(*next,m_start)>Config::AI_PATH_RADIUS ||
                !traverse(terrain,m_size,p,*next)) continue;
            const double nextCost=cost+1.0+std::abs(next->y-p.y)*.25;
            const Key k=key(*next);
            auto found=m_lookup.find(k);
            size_t index;
            if (found==m_lookup.end()) {
                if (m_nodes.size()>=Config::AI_PATH_NODE_LIMIT) continue;
                index=m_nodes.size();
                m_nodes.push_back({*next,nextCost,entry.index,false});
                m_lookup.emplace(k,index);
            } else {
                index=found->second;
                if (m_nodes[index].closed || m_nodes[index].cost<=nextCost) continue;
                m_nodes[index].cost=nextCost;
                m_nodes[index].parent=entry.index;
            }
            m_open.push({nextCost+heuristic(*next),nextCost,index});
        }
    }
    if (m_status==Status::Pending && m_open.empty()) m_status=Status::Unreachable;
    return used;
}
}
