#include "entity/EntityManager.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

GroundNavigation::Terrain EntityManager::navigationTerrain(
    std::map<std::pair<int,int>,uint64_t>* revisions) const {
    auto chunkAt=[this,revisions](int x,int z) -> const Chunk* {
        const auto key=std::make_pair(World::worldToChunkX(x),World::worldToChunkZ(z));
        const auto found=m_aiChunks.find(key);
        if (found==m_aiChunks.end()) {
            if (revisions) revisions->emplace(key,0);
            return nullptr;
        }
        if (revisions) revisions->emplace(key,found->second->blockRevision());
        return found->second;
    };
    return {
        [chunkAt](int x,int y,int z) {
            const Chunk* chunk=chunkAt(x,z);
            return chunk ? chunk->getBlock(x-chunk->worldX(),y,z-chunk->worldZ()) : BlockId::AIR;
        },
        [chunkAt](int x,int z) { return chunkAt(x,z)!=nullptr; }
    };
}

bool EntityManager::revisionsCurrent(
    const std::map<std::pair<int,int>,uint64_t>& revisions) const {
    for (const auto& [key,revision]:revisions) {
        const auto found=m_aiChunks.find(key);
        if (revision==0) {
            if (found!=m_aiChunks.end()) return false;
        } else if (found==m_aiChunks.end() || found->second->blockRevision()!=revision) return false;
    }
    return true;
}

void EntityManager::prepareAiFrame(float dt) {
    m_aiTime+=dt;
    m_aiChunks.clear();
    for (const Chunk* chunk:m_world.getActiveChunks())
        if (chunk->generated.load()) m_aiChunks.emplace(std::make_pair(chunk->cx,chunk->cz),chunk);
    m_aiEntityIndices.clear();
    m_aiBuckets.clear();
    for (size_t i=0;i<m_entities.size();++i) {
        Entity& entity=m_entities[i];
        m_aiEntityIndices.emplace(entity.id,i);
        if (entity.type==EntityType::Item || entity.type==EntityType::Arrow ||
            entity.type==EntityType::PrimedTnt || entity.health<=0) continue;
        m_aiBuckets[{static_cast<int>(std::floor(entity.position.x/16)),
                     static_cast<int>(std::floor(entity.position.y/16)),
                     static_cast<int>(std::floor(entity.position.z/16))}].push_back(entity.id);
        if (!entity.ai.initialized) {
            entity.ai.initialized=true;
            entity.ai.nextDecision=m_aiTime+(entity.behaviorSeed%100)*.001;
            entity.ai.progressOrigin=entity.position;
            entity.ai.progressSince=m_aiTime;
        }
        if (!revisionsCurrent(entity.ai.pathRevisions)) {
            cancelNavigation(entity);
            entity.ai.failedPois.clear();
            entity.ai.nextPath=m_aiTime;
        }
    }
    for (auto it=m_navigation.begin();it!=m_navigation.end();) {
        Entity* entity=aiEntity(it->first);
        if (!entity || !revisionsCurrent(it->second.revisions)) {
            if (entity) {
                entity->ai.pathStatus=GroundNavigation::Status::Unreachable;
                entity->ai.nextPath=m_aiTime;
            }
            it=m_navigation.erase(it);
        } else ++it;
    }
}

Entity* EntityManager::aiEntity(uint64_t id) {
    const auto found=m_aiEntityIndices.find(id);
    if (found==m_aiEntityIndices.end() || found->second>=m_entities.size()) return nullptr;
    Entity& entity=m_entities[found->second];
    return entity.id==id && entity.health>0 ? &entity : nullptr;
}

std::vector<uint64_t> EntityManager::nearbyEntities(const glm::dvec3& p,double radius) const {
    std::vector<uint64_t> result;
    for (int y=static_cast<int>(std::floor((p.y-radius)/16));
         y<=static_cast<int>(std::floor((p.y+radius)/16));++y)
        for (int z=static_cast<int>(std::floor((p.z-radius)/16));
             z<=static_cast<int>(std::floor((p.z+radius)/16));++z)
            for (int x=static_cast<int>(std::floor((p.x-radius)/16));
                 x<=static_cast<int>(std::floor((p.x+radius)/16));++x) {
                const auto found=m_aiBuckets.find({x,y,z});
                if (found!=m_aiBuckets.end())
                    result.insert(result.end(),found->second.begin(),found->second.end());
            }
    return result;
}

void EntityManager::cancelNavigation(Entity& entity) {
    m_navigation.erase(entity.id);
    entity.ai.path.clear();
    entity.ai.pathRevisions.clear();
    entity.ai.waypoint=0;
    entity.ai.pathStatus=GroundNavigation::Status::Unreachable;
}

void EntityManager::requestNavigation(Entity& entity,const GroundNavigation::Goal& goal,
                                      NavigationPurpose purpose) {
    auto& ai=entity.ai;
    const bool changed=ai.purpose!=purpose ||
        glm::distance(ai.pathGoal.position,goal.position)>2.0 ||
        ai.pathGoal.radius!=goal.radius;
    if (!changed && (ai.pathStatus==GroundNavigation::Status::Pending ||
        (ai.pathStatus==GroundNavigation::Status::Succeeded && ai.waypoint<ai.path.size()))) return;
    if (m_aiTime+1e-6<ai.nextPath) return;
    cancelNavigation(entity);
    ai.purpose=purpose;
    ai.pathGoal=goal;
    ai.pathStatus=GroundNavigation::Status::Pending;
    ai.nextPath=m_aiTime+Config::AI_REPATH_INTERVAL;
    NavigationRequest request;
    request.origin=entity.position;
    request.goal=goal;
    request.purpose=purpose;
    request.queuedAt=m_aiTime;
    m_navigation.emplace(entity.id,std::move(request));
}

void EntityManager::scheduleNavigation(const glm::dvec3& playerPosition) {
    size_t active=0;
    for (const auto& entry:m_navigation) if (entry.second.search) ++active;
    while (active<Config::AI_ACTIVE_SEARCH_LIMIT) {
        auto best=m_navigation.end();
        double priority=std::numeric_limits<double>::max();
        for (auto it=m_navigation.begin();it!=m_navigation.end();++it) {
            if (it->second.search) continue;
            // Aging eventually outweighs distance, including a stream of nearby requests.
            const double score=glm::distance(it->second.origin,playerPosition)-
                               32.0*(m_aiTime-it->second.queuedAt);
            if (score<priority) { best=it; priority=score; }
        }
        if (best==m_navigation.end()) break;
        Entity* entity=aiEntity(best->first);
        if (!entity) { m_navigation.erase(best); continue; }
        auto& request=best->second;
        request.origin=entity->position;
        request.search=std::make_unique<GroundNavigation::Search>(
            navigationTerrain(&request.revisions),renderSize(entity->type),
            request.origin,request.goal);
        ++active;
    }
    size_t remaining=Config::AI_PATH_NODES_PER_FRAME;
    while (remaining>0) {
        auto selected=m_navigation.end();
        auto next=m_navigation.upper_bound(m_searchCursor);
        for (size_t i=0;i<m_navigation.size();++i) {
            if (next==m_navigation.end()) next=m_navigation.begin();
            if (next->second.search) { selected=next; break; }
            ++next;
        }
        if (selected==m_navigation.end()) break;
        m_searchCursor=selected->first;
        auto& request=selected->second;
        const size_t used=request.search->advance(navigationTerrain(&request.revisions),
                                                 std::min(Config::AI_SEARCH_SLICE_NODES,remaining));
        remaining-=used;
        m_aiStats.pathNodes+=used;
        if (request.search->status()==GroundNavigation::Status::Pending) continue;
        if (Entity* entity=aiEntity(selected->first)) {
            auto& ai=entity->ai;
            ai.pathStatus=request.search->status();
            ai.pathRevisions=request.revisions;
            if (ai.pathStatus==GroundNavigation::Status::Succeeded) {
                ai.path=request.search->path();
                ai.waypoint=0;
                ai.retryDelay=Config::AI_RETRY_MIN_SECONDS;
                ai.progressOrigin=entity->position;
                ai.progressSince=m_aiTime;
                if (request.purpose!=NavigationPurpose::Move) {
                    const bool bed=request.purpose==NavigationPurpose::BedClaim;
                    if (poiAvailable(*entity,ai.candidatePoi,bed)) {
                        if (bed) {
                            entity->villager.hasBed=true;
                            entity->villager.claimedBed=ai.candidatePoi;
                        } else {
                            entity->villager.hasWorkstation=true;
                            entity->villager.claimedWorkstation=ai.candidatePoi;
                            entity->villager.profession=professionForWorkstation(m_world.getBlock(
                                ai.candidatePoi.x,ai.candidatePoi.y,ai.candidatePoi.z));
                        }
                    }
                    ai.path.clear();
                    ai.nextDecision=m_aiTime;
                }
            } else {
                if (request.purpose==NavigationPurpose::Move && entity->type==EntityType::Villager) {
                    if (ai.behavior==EntityBehavior::Work && entity->villager.hasWorkstation) {
                        const auto poi=entity->villager.claimedWorkstation;
                        ai.failedPois[{poi.x,poi.y,poi.z}]=m_aiTime+ai.retryDelay;
                        entity->villager.hasWorkstation=false;
                    } else if (ai.behavior==EntityBehavior::Sleep && entity->villager.hasBed) {
                        const auto poi=entity->villager.claimedBed;
                        ai.failedPois[{poi.x,poi.y,poi.z}]=m_aiTime+ai.retryDelay;
                        entity->villager.hasBed=false;
                        entity->sleeping=false;
                    }
                }
                ai.nextPath=m_aiTime+ai.retryDelay;
                ai.retryDelay=std::min(ai.retryDelay*2,static_cast<double>(Config::AI_RETRY_MAX_SECONDS));
                if (request.purpose!=NavigationPurpose::Move) {
                    ai.failedPois[{ai.candidatePoi.x,ai.candidatePoi.y,ai.candidatePoi.z}]=
                        ai.nextPath;
                    // Try another candidate without declaring the previous budget slice a failure.
                    ai.nextPath=m_aiTime+Config::AI_REPATH_INTERVAL;
                }
            }
        }
        m_navigation.erase(selected);
    }
    for (const auto& entry:m_navigation) if (entry.second.search) ++m_aiStats.activeSearches;
}

void EntityManager::followNavigation(Entity& entity,float dt) {
    auto& ai=entity.ai;
    if (!ai.hasDestination || ai.speed<=0) return;
    const auto terrain=navigationTerrain();
    const glm::vec3 size=renderSize(entity.type);
    if (GroundNavigation::reached(entity.position,ai.destination)) {
        cancelNavigation(entity);
        entity.stuckSeconds=0;
        return;
    }
    // Cheap direct travel is limited to short, fully swept segments.
    const double distance=glm::distance(entity.position,ai.destination.position);
    if (ai.path.empty() && distance<=1.5 &&
        GroundNavigation::traverse(terrain,size,entity.position,ai.destination.position)) {
        const glm::dvec3 delta=ai.destination.position-entity.position;
        const double horizontal=std::hypot(delta.x,delta.z);
        if (horizontal>.01) moveWithTerrain(entity,
            glm::vec3(delta.x/horizontal,0,delta.z/horizontal)*
            std::min(ai.speed,static_cast<float>(horizontal/std::max(dt,.00001f))),dt);
        return;
    }
    requestNavigation(entity,ai.destination);
    if (ai.purpose!=NavigationPurpose::Move || ai.pathStatus!=GroundNavigation::Status::Succeeded ||
        ai.waypoint>=ai.path.size()) return;
    while (ai.waypoint<ai.path.size() &&
           std::hypot(entity.position.x-ai.path[ai.waypoint].x,
                      entity.position.z-ai.path[ai.waypoint].z)<.12 &&
           std::abs(entity.position.y-ai.path[ai.waypoint].y)<.15) ++ai.waypoint;
    if (ai.waypoint>=ai.path.size()) return;
    const glm::dvec3 waypoint=ai.path[ai.waypoint];
    const glm::dvec3 delta=waypoint-entity.position;
    const double horizontal=std::hypot(delta.x,delta.z);
    if (ai.grounded && waypoint.y>entity.position.y+Config::AI_STEP_HEIGHT+.01) {
        if (!GroundNavigation::traverse(terrain,size,entity.position,waypoint)) {
            cancelNavigation(entity); return;
        }
        entity.velocity.y=Config::AI_JUMP_SPEED;
        ai.grounded=false;
    }
    if (horizontal>.025)
        moveWithTerrain(entity,glm::vec3(delta.x/horizontal,0,delta.z/horizontal)*
            std::min(ai.speed,static_cast<float>(horizontal/std::max(dt,.00001f))),dt);
    // Only actual progress counts. Zero movement on an unrequested axis cannot reset this timer.
    if (std::hypot(entity.position.x-ai.progressOrigin.x,
                   entity.position.z-ai.progressOrigin.z)>.08) {
        ai.progressOrigin=entity.position;
        ai.progressSince=m_aiTime;
        entity.stuckSeconds=0;
    } else if (ai.grounded) {
        entity.stuckSeconds=static_cast<float>(m_aiTime-ai.progressSince);
        if (entity.stuckSeconds>=Config::AI_STUCK_SECONDS) {
            cancelNavigation(entity);
            ai.nextPath=m_aiTime+Config::AI_REPATH_INTERVAL;
            ai.progressSince=m_aiTime;
        }
    }
}

void EntityManager::moveWithTerrain(Entity& entity,const glm::vec3& horizontal,float dt) {
    const auto terrain=navigationTerrain();
    const auto size=renderSize(entity.type);
    const glm::dvec3 start=entity.position;
    const int steps=sweptCollisionSteps(glm::length(horizontal)*dt,Config::AI_COLLISION_STEP);
    const glm::dvec3 delta=glm::dvec3(horizontal)*(static_cast<double>(dt)/steps);
    for (int i=0;i<steps;++i) {
        glm::dvec3 next=entity.position+delta;
        if (entity.ai.grounded) {
            auto support=GroundNavigation::stand(terrain,size,next.x,next.z,entity.position.y,
                                                  Config::AI_STEP_HEIGHT,Config::AI_MAX_DROP);
            if (!support) break;
            if (support->y>entity.position.y+1e-5) next.y=support->y;
        }
        if (!GroundNavigation::clear(terrain,size,next)) break;
        entity.position=next;
        entity.ai.grounded=GroundNavigation::supported(terrain,size,next);
    }
    entity.locomotionVelocity=autonomousHorizontalVelocity(start,entity.position,dt);
    const glm::vec3 displacement(entity.position-start);
    if (std::hypot(displacement.x,displacement.z)>.00001f)
        entity.facing=glm::normalize(glm::vec3(displacement.x,0,displacement.z));
}

void EntityManager::integrateVelocity(Entity& entity,float dt) {
    // Small substeps keep the gravity/contact solution consistent at 30/60/120 Hz.
    const int frames=std::max(1,static_cast<int>(std::ceil(dt*120.0f)));
    const float h=dt/frames;
    const auto terrain=navigationTerrain();
    const auto size=renderSize(entity.type);
    entity.ai.grounded=entity.velocity.y<=0 && GroundNavigation::supported(terrain,size,entity.position);
    if (entity.ai.grounded) entity.velocity.y=0;
    for (int frame=0;frame<frames;++frame) {
        glm::dvec3 total=glm::dvec3(entity.velocity)*static_cast<double>(h);
        if (!entity.ai.grounded) {
            total.y-=.5*Config::AI_GRAVITY*h*h;
            entity.velocity.y-=Config::AI_GRAVITY*h;
        }
        const int steps=sweptCollisionSteps(glm::length(total),Config::AI_COLLISION_STEP);
        const glm::dvec3 step=total/static_cast<double>(steps);
        for (int i=0;i<steps;++i) for (int axis : {0,1,2}) {
            if (std::abs(step[axis])<1e-12) continue;
            glm::dvec3 next=entity.position;
            next[axis]+=step[axis];
            if (!collides(entity,next)) {
                entity.position=next;
                if (axis!=1 && entity.ai.grounded)
                    entity.ai.grounded=GroundNavigation::supported(terrain,size,entity.position);
                continue;
            }
            // Resolve to the contact plane, rather than hovering a whole substep above it.
            double low=0,high=1;
            for (int iteration=0;iteration<12;++iteration) {
                const double mid=(low+high)*.5;
                next=entity.position;
                next[axis]+=step[axis]*mid;
                if (collides(entity,next)) high=mid; else low=mid;
            }
            entity.position[axis]+=step[axis]*low;
            entity.velocity[axis]=0;
            if (axis==1 && step.y<0) entity.ai.grounded=true;
        }
    }
    const float drag=std::pow(.12f,dt);
    entity.velocity.x*=drag;
    entity.velocity.z*=drag;
}
