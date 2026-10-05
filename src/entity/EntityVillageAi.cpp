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
    const auto terrain=navigationTerrain(nullptr,true);
    std::vector<glm::dvec3> positions;
    static constexpr int offsets[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
    for (const auto& offset:offsets) {
        const auto position=GroundNavigation::stand(terrain,entitySize(entity),
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
    const auto terrain=navigationTerrain(nullptr,true);
    if (!GroundNavigation::clear(terrain,entitySize(entity),entity.position)) return false;
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
    if (glm::distance(entity.position,glm::dvec3(poi)+glm::dvec3(.5))>Config::VILLAGE_POI_RADIUS ||
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
            if (bed ? entity.villager.hasBed : entity.villager.hasWorkstation || !entity.villager.adult()) continue;
            for (const auto& poi:bed ? indexed.beds : indexed.workstations) {
                const auto failed=ai.failedPois.find({poi.x,poi.y,poi.z});
                if (failed!=ai.failedPois.end() && failed->second>m_aiTime) continue;
                const double distance=glm::distance(entity.position,glm::dvec3(poi)+glm::dvec3(.5));
                if (distance<=Config::VILLAGE_POI_RADIUS) candidates.push_back({poi,bed,distance});
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
        if(glm::distance(entity.position,goal->position)>40) {
            const auto delta=goal->position-entity.position;
            const auto step=entity.position+delta*(32.0/glm::length(delta));
            if(auto stand=GroundNavigation::stand(navigationTerrain(nullptr,true),entitySize(entity),
                std::floor(step.x)+.5,std::floor(step.z)+.5,step.y,8,8)) {
                ai.destination={*stand,.5,.6};ai.hasDestination=true;ai.speed=.75f;
                requestNavigation(entity,ai.destination,NavigationPurpose::Move);
            }
            return;
        }
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
        if (v.hasBed && m_aiChunks.count({World::worldToChunkX(v.claimedBed.x),World::worldToChunkZ(v.claimedBed.z)})) {
            const auto valid=m_world.validBedFoot(v.claimedBed);
            if (!valid || *valid!=v.claimedBed || !poiStand(entity,v.claimedBed) ||
                !beds.emplace(v.claimedBed.x,v.claimedBed.y,v.claimedBed.z).second) v.hasBed=false;
        }
        if (v.hasWorkstation && m_aiChunks.count({World::worldToChunkX(v.claimedWorkstation.x),World::worldToChunkZ(v.claimedWorkstation.z)})) {
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

void EntityManager::changeVillageReputation(Entity& victim,int amount) {
    auto change=[&](Entity& v) {
        v.villager.reputation=static_cast<int16_t>(std::clamp<int>(v.villager.reputation+amount,-100,100));
        m_dirtyEntityChunks.insert({World::worldToChunkX(v.position.x),World::worldToChunkZ(v.position.z)});
    };
    change(victim);
    for(const auto& village:m_logicalVillages) {
        if(std::find(village.members.begin(),village.members.end(),victim.id)==village.members.end())continue;
        for(uint64_t id:village.members) {
            auto found=std::find_if(m_entities.begin(),m_entities.end(),[&](const Entity& e){return e.id==id;});
            Entity* other=found==m_entities.end()?nullptr:&*found;
            if(other && id!=victim.id && glm::distance(other->position,victim.position)<=16 &&
                aiClearSight(other->position+glm::dvec3(0,1,0),victim.position+glm::dvec3(0,1,0)))change(*other);
        }
    }
}

void EntityManager::tickVillageLife(float dt,uint64_t worldTick) {
    const auto day=static_cast<uint32_t>(worldTick/24000);
    for(auto& e:m_entities)if(e.health>0 &&
        (e.type==EntityType::Villager || e.type==EntityType::ZombieVillager))
        advanceVillagerLife(e.villager,dt,day);
    m_villageLifeTimer+=dt;
    if(m_villageLifeTimer<1 || m_entities.empty())return;
    m_villageLifeTimer=0;
    const auto terrain=navigationTerrain(nullptr,true);
    const bool grief=m_world.gameRules().boolean(GameRuleId::MobGriefing);
    const bool spawning=m_world.gameRules().boolean(GameRuleId::SpawnMobs);
    const auto tick=worldTick%24000;
    const bool working=(tick>=2000 && tick<4000)||(tick>=9000 && tick<11000);
    std::vector<uint64_t> actors;
    const size_t count=m_entities.size();
    for(size_t n=0;n<count && actors.size()<Config::VILLAGE_ACTIONS_PER_SECOND;++n) {
        const auto& e=m_entities[(m_villageLifeCursor+n)%count];
        if(e.type==EntityType::Villager && e.health>0)actors.push_back(e.id);
        if(n+1==count || actors.size()==Config::VILLAGE_ACTIONS_PER_SECOND)m_villageLifeCursor=(m_villageLifeCursor+n+1)%count;
    }
    // Mutations that append entities happen here, after the main update loop.
    // Reacquire by ID following each append; vector references are never retained.
    for(uint64_t id:actors) {
        ++m_aiStats.villageActions;
        Entity* e=aiEntity(id);
        if(!e || !e->villager.adult() || e->sleeping || e->ai.panicUntil>m_aiTime)continue;
        auto& v=e->villager;
        if(grief) {
            for(auto otherId:nearbyEntities(e->position,1.6)) {
                Entity* item=aiEntity(otherId);
                if(!item || item->type!=EntityType::Item || item->actionCooldown>0 ||
                   glm::distance(item->position,e->position)>1.6)continue;
                const int left=addVillagerFood(v,item->item);
                item->item.count=static_cast<uint8_t>(left);
                if(left==0){item->item.clear();item->health=0;}
            }
        }
        if(working && v.profession==VillagerProfession::Farmer && v.hasWorkstation) {
            if(grief) {
            // A fixed scan slice bounds farm discovery even with many residents.
            for(int scan=0;scan<static_cast<int>(Config::VILLAGE_FARM_SCAN_SLICE);++scan) {
                ++m_aiStats.farmBlocks;
                const unsigned cell=(e->ai.sequence++ % (33*33));
                const int x=v.claimedWorkstation.x+static_cast<int>(cell%33)-16;
                const int z=v.claimedWorkstation.z+static_cast<int>(cell/33)-16;
                const Chunk* chunk=m_world.getChunk(World::worldToChunkX(x),World::worldToChunkZ(z));
                if(!chunk || !chunk->generated.load())continue;
                const int y=chunk->getColumnMaxY((x%16+16)%16,(z%16+16)%16);
                const auto crop=m_world.getBlock(x,y,z);
                const bool harvest=crop==BlockId::WHEAT_7;
                const bool plant=isFarmland(crop) && m_world.getBlock(x,y+1,z)==BlockId::AIR &&
                    villagerFoodCount(v,ItemId::WHEAT_SEEDS)>0;
                if(!harvest && !plant)continue;
                const glm::ivec3 p(x,harvest?y:y+1,z);
                const auto goal=poiGoal(*e,p);
                if(!goal)continue;
                if(glm::distance(e->position,glm::dvec3(p)+glm::dvec3(.5))>1.8) {
                    e->ai.behavior=EntityBehavior::Farm;e->ai.destination=*goal;
                    e->ai.hasDestination=true;e->ai.speed=.75f;break;
                }
                if(!interactablePoi(*e,p))continue;
                auto candidate=v;
                if(harvest) {
                    if(addVillagerFood(candidate,{ItemId::WHEAT,1,0}) ||
                       addVillagerFood(candidate,{ItemId::WHEAT_SEEDS,2,0}))break;
                    if(m_world.getBlock(x,p.y,z)!=BlockId::WHEAT_7)break;
                    m_world.setBlock(x,p.y,z,BlockId::AIR);
                }
                if(consumeVillagerFood(candidate,ItemId::WHEAT_SEEDS,1))
                    m_world.setBlock(x,p.y,z,BlockId::WHEAT_0);

                v=candidate;break;
            }
            }
            if(villagerFoodCount(v,ItemId::WHEAT)>=3) {
                auto candidate=v;
                consumeVillagerFood(candidate,ItemId::WHEAT,3);
                if(addVillagerFood(candidate,{ItemId::BREAD,1,0})==0)v=candidate;
            }
            for(auto otherId:nearbyEntities(e->position,3)) {
                Entity* other=aiEntity(otherId);
                if(!other || otherId==id || other->type!=EntityType::Villager ||
                   glm::distance(other->position,e->position)>3 ||
                   villagerFoodCount(other->villager,ItemId::BREAD)>=3 ||
                   villagerFoodCount(v,ItemId::BREAD)<=3)continue;
                auto candidate=other->villager;
                if(addVillagerFood(candidate,{ItemId::BREAD,1,0})==0) {
                    consumeVillagerFood(v,ItemId::BREAD,1);other->villager=candidate;
                }
                break;
            }
        }
        if(spawning && willingToBreed(v) && v.hasBed) {
            uint64_t partnerId=0;
            for(auto otherId:nearbyEntities(e->position,16)) {
                const auto* other=aiEntity(otherId);
                if(other && otherId!=id && other->type==EntityType::Villager &&
                   other->villager.hasBed && willingToBreed(other->villager) &&
                   !other->sleeping && other->ai.panicUntil<=m_aiTime &&
                   glm::distance(e->position,other->position)<=16) {partnerId=otherId;break;}
            }
            if(partnerId) {
                const auto* partner=aiEntity(partnerId);
                if(glm::distance(e->position,partner->position)>3) {
                    e->ai.behavior=EntityBehavior::Breed;
                    e->ai.destination={partner->position,2,.6};e->ai.hasDestination=true;e->ai.speed=.75f;
                    continue;
                }
                if(!aiClearSight(e->position+glm::dvec3(0,1,0),partner->position+glm::dvec3(0,1,0)))continue;
                std::optional<glm::ivec3> reserved;
                std::optional<glm::dvec3> birth;
                for(const auto& entry:m_poiChunks) {
                    if(!entry.second.complete)continue;
                    for(const auto& bed:entry.second.beds) {
                        if((v.hasBed && v.claimedBed==bed) ||
                           glm::distance(e->position,glm::dvec3(bed))>8 || !poiAvailable(*e,bed,true))continue;
                        const auto stand=poiStand(*e,bed);
                        if(stand && GroundNavigation::traverse(terrain,entitySize(*e),e->position,*stand)) {
                            reserved=bed;birth=stand;break;
                        }
                    }
                    if(reserved)break;
                }
                if(reserved && birth && spawnMob(EntityType::Villager,*birth)) {
                    auto& child=m_entities.back();child.villager.growthSeconds=Config::VILLAGER_GROWTH_SECONDS;
                    child.villager.hasBed=true;child.villager.claimedBed=*reserved;
                    // Claim commits with the birth, so subsequent couples cannot reuse it.
                    auto* first=aiEntity(id);auto* second=aiEntity(partnerId);
                    if(first && second) {consumeBreedingFood(first->villager);consumeBreedingFood(second->villager);}
                }
            }
        }
    }
    // One village checked per second; missing chunks defer rather than recreate defenders.
    if(!spawning || !m_naturalSpawningEnabled || m_logicalVillages.empty())return;
    const auto& village=m_logicalVillages[static_cast<size_t>(day+worldTick/20)%m_logicalVillages.size()];
    std::vector<uint64_t> adults;
    glm::dvec3 center(0);
    for(uint64_t id:village.members)if(const auto* e=aiEntity(id)) {
        if(e->villager.adult() && e->villager.hasBed) {adults.push_back(id);center+=glm::dvec3(e->villager.claimedBed);}
    }
    if(adults.size()<5)return;
    center/=static_cast<double>(adults.size());
    for(int x=village.minimumSubchunk.x;x<=village.maximumSubchunk.x;++x)
        for(int z=village.minimumSubchunk.z;z<=village.maximumSubchunk.z;++z)
            if(!m_aiChunks.count({x,z}))return;
    for(const auto& e:m_entities)if(e.type==EntityType::IronGolem && e.health>0 &&
        glm::distance(e.position,center)<96)return;
    for(auto id:adults)if(const auto* e=aiEntity(id))if(e->villager.defenseCooldown>0)return;
    const auto size=renderSize(EntityType::IronGolem);
    for(int radius=2;radius<=8;radius+=2)for(int direction=0;direction<4;++direction) {
        static constexpr int offsets[4][2]={{1,0},{0,1},{-1,0},{0,-1}};
        const auto position=GroundNavigation::stand(navigationTerrain(),size,
            std::floor(center.x)+.5+offsets[direction][0]*radius,
            std::floor(center.z)+.5+offsets[direction][1]*radius,center.y,8,8);
        if(position && spawnMob(EntityType::IronGolem,*position)) {
            m_entities.back().villager.hasBed=true;
            m_entities.back().villager.claimedBed=glm::ivec3(glm::floor(center));
            for(auto id:adults)if(auto* e=aiEntity(id))e->villager.defenseCooldown=Config::VILLAGE_DEFENSE_COOLDOWN;
            return;
        }
    }
}
