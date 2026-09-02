#include "entity/EntityManager.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>
#include <set>

void EntityManager::refreshPoiIndex() {
    std::set<std::pair<int,int>> wanted;
    for (const Entity& entity:m_entities) {
        if (entity.type!=EntityType::Villager || entity.health<=0) continue;
        const int cx=World::worldToChunkX(entity.position.x);
        const int cz=World::worldToChunkZ(entity.position.z);
        for (int x=cx-3;x<=cx+3;++x) for (int z=cz-3;z<=cz+3;++z)
            if (m_aiChunks.count({x,z})) wanted.emplace(x,z);
    }
    for (auto it=m_poiChunks.begin();it!=m_poiChunks.end();) {
        if (!wanted.count(it->first)) it=m_poiChunks.erase(it); else ++it;
    }
    bool changed=false;
    for (const auto& key:wanted) {
        auto& indexed=m_poiChunks[key];
        const Chunk* chunk=m_aiChunks.at(key);
        if (indexed.revision!=chunk->blockRevision()) {
            indexed={}; indexed.revision=chunk->blockRevision(); changed=true;
        }
    }
    if (changed) for (Entity& entity:m_entities)
        if (entity.type==EntityType::Villager) entity.ai.failedPois.clear();
    size_t budget=Config::AI_POI_BLOCKS_PER_FRAME;
    // Columns carry a cached top. Skip the empty tail without inspecting voxels.
    for (auto& [key,indexed]:m_poiChunks) {
        if (indexed.complete || budget==0) continue;
        const Chunk* chunk=m_aiChunks.at(key);
        while (indexed.cursor<Config::CHUNK_VOLUME && budget>0) {
            const int column=static_cast<int>(indexed.cursor/Config::WORLD_HEIGHT);
            const int x=column%16,z=column/16;
            const int y=Config::WORLD_MIN_Y+static_cast<int>(indexed.cursor%Config::WORLD_HEIGHT);
            if (y>chunk->getColumnMaxY(x,z)) {
                indexed.cursor=static_cast<size_t>(column+1)*Config::WORLD_HEIGHT; continue;
            }
            ++indexed.cursor; --budget; ++m_aiStats.poiBlocks;
            const BlockId block=chunk->getBlock(x,y,z);
            const glm::ivec3 position(chunk->worldX()+x,y,chunk->worldZ()+z);
            BedPart part; BedDirection direction;
            if (decodeBed(block,part,direction) && part==BedPart::Foot) indexed.beds.push_back(position);
            else if (isVillagerWorkstation(block)) indexed.workstations.push_back(position);
        }
        indexed.complete=indexed.cursor==Config::CHUNK_VOLUME;
    }
}

std::optional<GroundNavigation::Goal> EntityManager::poiGoal(
    const Entity& entity,const glm::ivec3& poi) const {
    const auto terrain=navigationTerrain();
    std::vector<glm::dvec3> positions;
    static constexpr int offsets[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
    for (const auto& offset:offsets) {
        const auto position=GroundNavigation::stand(terrain,renderSize(entity.type),
            poi.x+offset[0]+.5,poi.z+offset[1]+.5,poi.y,1,1);
        if (position) positions.push_back(*position);
    }
    if (positions.empty()) return std::nullopt;
    std::stable_sort(positions.begin(),positions.end(),[&](const auto& a,const auto& b) {
        return glm::distance(a,entity.position)<glm::distance(b,entity.position);
    });
    GroundNavigation::Goal goal{positions.front(),.25,.2};
    goal.alternatives.assign(positions.begin()+1,positions.end());
    return goal;
}

std::optional<glm::dvec3> EntityManager::poiStand(const Entity& entity,const glm::ivec3& poi) const {
    const auto goal=poiGoal(entity,poi);
    return goal ? std::optional<glm::dvec3>(goal->position) : std::nullopt;
}

bool EntityManager::interactablePoi(const Entity& entity,const glm::ivec3& poi) {
    const glm::dvec3 center=glm::dvec3(poi)+glm::dvec3(.5,.5,.5);
    if (glm::distance(entity.position,center)>=1.6 || !entity.ai.grounded) return false;
    const auto terrain=navigationTerrain();
    if (!GroundNavigation::clear(terrain,renderSize(entity.type),entity.position)) return false;
    // Stop at the near surface of the POI so the POI itself does not count as a wall.
    const glm::dvec3 origin=entity.position+glm::dvec3(0,.6,0);
    const glm::dvec3 delta=center-origin;
    const double distance=glm::length(delta);
    const auto hit=m_world.raycast(origin,glm::vec3(delta/std::max(distance,.001)),
                                    static_cast<float>(distance));
    ++m_aiStats.sightQueries;
    return !hit || hit->blockPos==poi;
}

bool EntityManager::poiAvailable(const Entity& entity,const glm::ivec3& poi,bool bed) const {
    if (glm::distance(entity.position,glm::dvec3(poi)+glm::dvec3(.5))>Config::AI_PATH_RADIUS ||
        !poiStand(entity,poi)) return false;
    if (bed) {
        const auto valid=m_world.validBedFoot(poi);
        if (!valid || *valid!=poi) return false;
    } else {
        const auto profession=professionForWorkstation(m_world.getBlock(poi.x,poi.y,poi.z));
        if (profession==VillagerProfession::Unemployed ||
            (entity.villager.professionLocked && entity.villager.profession!=profession)) return false;
    }
    for (const Entity& other:m_entities) {
        if (other.id==entity.id || other.type!=EntityType::Villager || other.health<=0) continue;
        if (bed ? other.villager.hasBed && other.villager.claimedBed==poi :
                  other.villager.hasWorkstation && other.villager.claimedWorkstation==poi) return false;
    }
    return true;
}

void EntityManager::requestPoiClaim(Entity& entity) {
    auto& ai=entity.ai;
    if (m_navigation.count(entity.id) || m_aiTime<ai.nextPath) return;
    struct Candidate { glm::ivec3 position; bool bed; double distance; };
    std::vector<Candidate> candidates;
    for (const auto& [key,indexed]:m_poiChunks) {
        (void)key;
        if (!indexed.complete) continue;
        for (bool bed:{true,false}) {
            if (bed ? entity.villager.hasBed : entity.villager.hasWorkstation) continue;
            for (const auto& poi:bed ? indexed.beds : indexed.workstations) {
                const auto failed=ai.failedPois.find({poi.x,poi.y,poi.z});
                if (failed!=ai.failedPois.end() && failed->second>m_aiTime) continue;
                const double distance=glm::distance(entity.position,glm::dvec3(poi)+glm::dvec3(.5));
                if (distance<=Config::AI_PATH_RADIUS) candidates.push_back({poi,bed,distance});
            }
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b) {
        if (a.bed!=b.bed) return a.bed;
        if (a.distance!=b.distance) return a.distance<b.distance;
        return std::tie(a.position.x,a.position.y,a.position.z)<std::tie(b.position.x,b.position.y,b.position.z);
    });
    for (const auto& candidate:candidates) {
        if (!poiAvailable(entity,candidate.position,candidate.bed)) continue;
        auto goal=poiGoal(entity,candidate.position);
        if (!goal) continue;
        ai.candidatePoi=candidate.position;
        requestNavigation(entity,*goal,candidate.bed ?
            NavigationPurpose::BedClaim : NavigationPurpose::WorkClaim);
        return;
    }
}

void EntityManager::refreshVillageClaims() {
    std::set<std::tuple<int,int,int>> beds,workstations;
    for (Entity& entity:m_entities) {
        if (entity.type!=EntityType::Villager || entity.health<=0) continue;
        auto& v=entity.villager;
        if (v.hasBed) {
            const auto valid=m_world.validBedFoot(v.claimedBed);
            if (!valid || *valid!=v.claimedBed || !poiStand(entity,v.claimedBed) ||
                !beds.emplace(v.claimedBed.x,v.claimedBed.y,v.claimedBed.z).second) v.hasBed=false;
        }
        if (v.hasWorkstation) {
            const auto profession=professionForWorkstation(m_world.getBlock(
                v.claimedWorkstation.x,v.claimedWorkstation.y,v.claimedWorkstation.z));
            if (profession==VillagerProfession::Unemployed ||
                (v.professionLocked && profession!=v.profession) ||
                !poiStand(entity,v.claimedWorkstation) ||
                !workstations.emplace(v.claimedWorkstation.x,v.claimedWorkstation.y,v.claimedWorkstation.z).second)
                v.hasWorkstation=false;
            else if (!v.professionLocked) v.profession=profession;
        }
        if (!v.hasWorkstation && !v.professionLocked) v.profession=VillagerProfession::Unemployed;
    }
    rebuildLogicalVillages();
}
