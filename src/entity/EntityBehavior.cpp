#include "entity/EntityManager.h"
#include "entity/ProjectileLogic.h"
#include "game/NetherProgression.h"
#include "game/DimensionRules.h"
#include "player/Player.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace {
uint32_t aiHash(uint32_t value) {
    value^=value>>16; value*=0x7feb352dU;
    value^=value>>15; value*=0x846ca68bU;
    return value^(value>>16);
}
bool livingMob(const Entity& e) {
    return e.health>0 && e.type!=EntityType::Item && e.type!=EntityType::Arrow &&
           e.type!=EntityType::PrimedTnt && !nativeProjectile(e.type);
}
}

bool EntityManager::aiClearSight(const glm::dvec3& from,const glm::dvec3& to) {
    const double distance=glm::distance(from,to);
    if (distance<.001) return true;
    ++m_aiStats.sightQueries;
    return !m_world.raycast(from,glm::vec3((to-from)/distance),static_cast<float>(distance));
}

void EntityManager::chooseEscape(Entity& entity,const glm::dvec3& danger) {
    auto& ai=entity.ai;
    glm::dvec3 away=entity.position-danger;
    away.y=0;
    if (glm::length(away)<.01) away=glm::dvec3(entity.facing)*-1.0;
    if (glm::length(away)<.01) away={1,0,0};
    away=glm::normalize(away);
    const auto terrain=navigationTerrain(nullptr,entityTemplate(entity.type)==EntityType::Villager);
    const double offsets[]={0,.785398163,-.785398163,1.570796327,-1.570796327};
    ai.hasDestination=false;
    for (int i=0;i<5;++i) {
        const double angle=offsets[(i+ai.sequence)%5];
        const glm::dvec3 direction(away.x*std::cos(angle)-away.z*std::sin(angle),0,
                                   away.x*std::sin(angle)+away.z*std::cos(angle));
        const auto destination=GroundNavigation::stand(terrain,entitySize(entity),
            std::floor(entity.position.x+direction.x*6)+.5,
            std::floor(entity.position.z+direction.z*6)+.5,entity.position.y);
        if (!destination || glm::distance(*destination,danger)<=glm::distance(entity.position,danger))
            continue;
        ai.destination={*destination,.35,.6};
        ai.hasDestination=true;
        break;
    }
}

void EntityManager::decideBehavior(Entity& entity,Player& player,bool isDay,
                                   bool playerTargetable,uint64_t worldTick,bool thunderstorm) {
    ++m_aiStats.decisions;
    auto& ai=entity.ai;
    const auto previousBehavior=ai.behavior;
    const glm::dvec3 eye=entity.position+glm::dvec3(0,entitySize(entity).y*.8,0);
    if (entityTemplate(entity.type)==EntityType::Villager) {
        Entity* threat=nullptr;
        double nearest=Config::AI_VILLAGER_FEAR_DISTANCE;
        for (uint64_t id:nearbyEntities(entity.position,nearest)) {
            Entity* other=aiEntity(id);
            if (!other || !hostile(other->type)) continue;
            const double distance=glm::distance(entity.position,other->position);
            if (distance<nearest && aiClearSight(eye,other->position+
                                              glm::dvec3(0,entitySize(*other).y*.5,0))) {
                threat=other; nearest=distance;
            }
        }
        if (threat) {
            ai.danger=threat->position;
            ai.panicUntil=m_aiTime+Config::AI_PANIC_SECONDS;
        }
    }
    if (!hostile(entity.type) && entityTemplate(entity.type)!=EntityType::IronGolem && ai.panicUntil>m_aiTime) {
        if (previousBehavior!=EntityBehavior::Flee) {
            cancelNavigation(entity);
            ai.nextPath=m_aiTime;
            ai.hasDestination=false;
        }
        ai.behavior=EntityBehavior::Flee;
        ai.speed=entityTemplate(entity.type)==EntityType::Villager ? Config::AI_VILLAGER_PANIC_SPEED :
                                                   Config::AI_ANIMAL_PANIC_SPEED;
        entity.sleeping=false;
        if (!ai.hasDestination || GroundNavigation::reached(entity.position,ai.destination) ||
            (ai.pathStatus==GroundNavigation::Status::Unreachable && m_aiTime>=ai.nextPath)) {
            chooseEscape(entity,ai.danger);
            ++ai.sequence;
        }
        return;
    }
    ai.hasDestination=false;
    ai.speed=0;
    if (previousBehavior==EntityBehavior::Flee) {
        cancelNavigation(entity);
        ai.nextPath=m_aiTime;
        ai.actionUntil=m_aiTime;
    }
    if(entityTemplate(entity.type)==EntityType::IronGolem) {
        ai.targetVisible=false;
        Entity* target=nullptr;
        double nearest=Config::AI_SIGHT_DISTANCE;
        for(uint64_t id:nearbyEntities(entity.position,nearest)) {
            Entity* other=aiEntity(id);
            if(!other || other->health<=0 || !hostile(other->type))continue;
            const double distance=glm::distance(entity.position,other->position);
            if(distance<nearest && aiClearSight(eye,other->position+glm::dvec3(0,.9,0))) {
                target=other;nearest=distance;
            }
        }
        bool angry=ai.retaliating && ai.hasTarget && ai.targetId==0;
        for(uint64_t id:nearbyEntities(entity.position,16)) {
            const Entity* v=aiEntity(id);
            if(v && v->type==EntityType::Villager && v->villager.reputation<=-25)angry=true;
        }
        if(target) {
            ai.hasTarget=true;ai.targetId=target->id;ai.lastSeen=target->position;
            ai.targetVisible=true;ai.memoryUntil=m_aiTime+Config::AI_TARGET_MEMORY;
        } else if(playerTargetable && angry && glm::distance(entity.position,player.getPosition())<nearest &&
                  aiClearSight(eye,player.getEyePosition())) {
            ai.hasTarget=true;ai.targetId=0;ai.lastSeen=player.getPosition();ai.targetVisible=true;
            ai.memoryUntil=m_aiTime+Config::AI_TARGET_MEMORY;
        } else if(m_aiTime>=ai.memoryUntil || (!ai.targetId && (!angry || !playerTargetable)))ai.hasTarget=false;
        if(ai.hasTarget && entity.villager.hasBed &&
            glm::distance(ai.lastSeen,glm::dvec3(entity.villager.claimedBed))>32)ai.hasTarget=false;
        if(!ai.hasTarget && entity.villager.hasBed &&
            glm::distance(entity.position,glm::dvec3(entity.villager.claimedBed))>12) {
            if(auto destination=GroundNavigation::stand(navigationTerrain(nullptr,entityTemplate(entity.type)==EntityType::Villager),entitySize(entity),
                entity.villager.claimedBed.x+.5,entity.villager.claimedBed.z+.5,entity.position.y,8,8)) {
                ai.behavior=EntityBehavior::ReturnHome;ai.destination={*destination,3,.6};
                ai.hasDestination=true;ai.speed=.75f;return;
            }
        }
        if(ai.hasTarget) {
            ai.behavior=EntityBehavior::Chase;ai.speed=.9f;
            ai.destination={ai.lastSeen,1.8,.6};ai.hasDestination=true;return;
        }
    }
    if (hostile(entity.type)) {
        const double playerDistance=glm::distance(entity.position,player.getPosition());
        if (entityTemplate(entity.type)==EntityType::Spider && playerDistance>=Config::AI_SIGHT_DISTANCE)
            entity.spiderProvoked=false;
        const bool playerAllowed=playerTargetable &&
            (entityTemplate(entity.type)!=EntityType::Spider || !isDay || thunderstorm || entity.spiderProvoked);
        ai.targetVisible=false;
        if (ai.hasTarget) {
            Entity* other=ai.targetId ? aiEntity(ai.targetId) : nullptr;
            const bool valid=ai.targetId ? other && livingMob(*other) &&
                (ai.retaliating || !hostile(other->type)) : playerAllowed;
            if (!valid) ai.hasTarget=false;
            else {
                const glm::dvec3 position=other ? other->position : player.getPosition();
                const glm::dvec3 center=position+glm::dvec3(0,other ? entitySize(*other).y*.5 : .9,0);
                if (glm::distance(entity.position,position)<Config::AI_SIGHT_DISTANCE &&
                    aiClearSight(eye,center)) {
                    ai.targetVisible=true;
                    ai.lastSeen=position;
                    ai.memoryUntil=m_aiTime+Config::AI_TARGET_MEMORY;
                } else if (m_aiTime>=ai.memoryUntil) ai.hasTarget=false;
            }
        }
        if (!ai.hasTarget) {
            ai.retaliating=false;
            Entity* villager=nullptr;
            if (entityTemplate(entity.type)==EntityType::Zombie || entityTemplate(entity.type)==EntityType::ZombieVillager) {
                double nearest=Config::AI_SIGHT_DISTANCE;
                for (uint64_t id:nearbyEntities(entity.position,nearest)) {
                    Entity* candidate=aiEntity(id);
                    if (!candidate || candidate->type!=EntityType::Villager) continue;
                    const double distance=glm::distance(candidate->position,entity.position);
                    if (distance<nearest && aiClearSight(eye,candidate->position+glm::dvec3(0,.9,0))) {
                        villager=candidate; nearest=distance;
                    }
                }
            }
            if (villager || (playerAllowed && playerDistance<Config::AI_SIGHT_DISTANCE &&
                aiClearSight(eye,player.getPosition()+glm::dvec3(0,.9,0)))) {
                ai.hasTarget=true;
                ai.targetId=villager ? villager->id : 0;
                ai.lastSeen=villager ? villager->position : player.getPosition();
                ai.targetVisible=true;
                ai.memoryUntil=m_aiTime+Config::AI_TARGET_MEMORY;
            }
        }
        if (ai.hasTarget) {
            ai.behavior=EntityBehavior::Chase;
            ai.speed=entityTemplate(entity.type)==EntityType::Spider ? 3.0f : 2.0f;
            const double distance=glm::distance(entity.position,ai.lastSeen);
            if (entityTemplate(entity.type)==EntityType::Skeleton && ai.targetVisible &&
                distance<Config::AI_SKELETON_MIN_DISTANCE) {
                if (previousBehavior!=EntityBehavior::Chase || ai.path.empty() ||
                    GroundNavigation::reached(entity.position,ai.destination)) chooseEscape(entity,ai.lastSeen);
                else ai.hasDestination=true;
            } else if (entityTemplate(entity.type)==EntityType::Skeleton && ai.targetVisible &&
                       distance<=Config::AI_SKELETON_MAX_DISTANCE) {
                cancelNavigation(entity);
            } else {
                ai.destination={ai.lastSeen,ai.targetVisible ? 1.0 : .35,.6};
                ai.hasDestination=true;
            }
            return;
        }
        if (previousBehavior==EntityBehavior::Chase) {
            entity.attackPending=false;
            cancelNavigation(entity);
            ai.actionUntil=m_aiTime;
        }
    }
    if (entityTemplate(entity.type)==EntityType::Villager) {
        const auto& v=entity.villager;
        const uint32_t tick=static_cast<uint32_t>(worldTick%24000);
        if(v.adult() && ai.behavior==EntityBehavior::Breed && ai.hasDestination &&
            !GroundNavigation::reached(entity.position,ai.destination))return;
        const bool working=(tick>=2000 && tick<4000) || (tick>=9000 && tick<11000);
        if(working && v.adult() && entity.ai.behavior==EntityBehavior::Farm &&
           entity.ai.hasDestination && !GroundNavigation::reached(entity.position,entity.ai.destination))return;
        if(tick>=11000 && tick<12000 && v.hasBed) {
            for(const auto& village:m_logicalVillages)if(std::find(village.members.begin(),village.members.end(),entity.id)!=village.members.end()) {
                glm::dvec3 center(0);int count=0;
                for(auto id:village.members)if(const auto* other=aiEntity(id)) {center+=glm::dvec3(other->villager.claimedBed);++count;}
                if(count>0)if(auto destination=GroundNavigation::stand(navigationTerrain(nullptr,entityTemplate(entity.type)==EntityType::Villager),entitySize(entity),
                    std::floor(center.x/count)+.5,std::floor(center.z/count)+.5,entity.position.y,8,8)) {
                    ai.behavior=EntityBehavior::Gather;ai.destination={*destination,3,.6};
                    ai.hasDestination=true;ai.speed=.65f;entity.sleeping=false;return;
                }
            }
        }
        std::optional<glm::ivec3> poi;
        if (!isDay && v.hasBed) { poi=v.claimedBed; ai.behavior=EntityBehavior::Sleep; }
        else if (working && v.hasWorkstation && v.adult()) { poi=v.claimedWorkstation; ai.behavior=EntityBehavior::Work; }
        if (poi) {
            entity.sleeping=ai.behavior==EntityBehavior::Sleep && interactablePoi(entity,*poi);
            if (auto destination=poiGoal(entity,*poi)) {
                ai.destination=*destination;
                ai.hasDestination=!entity.sleeping;
                ai.speed=.75f;
            }
            return;
        }
        entity.sleeping=false;
        if (!v.hasBed || (!v.hasWorkstation && v.adult())) {
            requestPoiClaim(entity);
            if (ai.pathStatus==GroundNavigation::Status::Pending &&
                ai.purpose!=NavigationPurpose::Move) { ai.behavior=EntityBehavior::Idle; return; }
        }
        if (v.hasBed && v.hasWorkstation) {
            const glm::dvec3 center=(glm::dvec3(v.claimedBed)+glm::dvec3(v.claimedWorkstation))*.5;
            if (glm::distance(entity.position,center)>12) {
                if (auto destination=GroundNavigation::stand(navigationTerrain(nullptr,entityTemplate(entity.type)==EntityType::Villager),entitySize(entity),
                        std::floor(center.x)+.5,std::floor(center.z)+.5,entity.position.y)) {
                    ai.behavior=EntityBehavior::ReturnHome;
                    ai.destination={*destination,1,.6};
                    ai.hasDestination=true; ai.speed=.75f;
                    return;
                }
            }
        }
    }
    // Seeded short walks alternate with rests, independent of render frame count.
    if (m_aiTime>=ai.actionUntil) {
        const uint32_t roll=aiHash(entity.behaviorSeed+ ++ai.sequence);
        if (previousBehavior==EntityBehavior::Wander) {
            ai.behavior=EntityBehavior::Idle;
            ai.actionUntil=m_aiTime+2+(roll%300)*.01;
            cancelNavigation(entity);
        } else {
            ai.behavior=EntityBehavior::Wander;
            ai.actionUntil=m_aiTime+3+(roll%300)*.01;
            const double angle=(roll%6283)*.001;
            const double radius=3+(roll%4);
            if (auto destination=GroundNavigation::stand(navigationTerrain(nullptr,entityTemplate(entity.type)==EntityType::Villager),entitySize(entity),
                    std::floor(entity.position.x+std::cos(angle)*radius)+.5,
                    std::floor(entity.position.z+std::sin(angle)*radius)+.5,entity.position.y))
                ai.destination={*destination,.35,.6};
            else ai.behavior=EntityBehavior::Idle;
        }
    }
    if (ai.behavior==EntityBehavior::Wander) {
        ai.hasDestination=true;
        ai.speed=entityTemplate(entity.type)==EntityType::Villager ? .45f : .65f;
    }
}

void EntityManager::updateMobAi(Entity& entity,Player& player,float dt,bool isDay,
                               bool playerTargetable,uint64_t worldTick,bool thunderstorm) {
    auto& ai=entity.ai;
    double nearestPlayerDistance = glm::distance(entity.position, player.getPosition());
    for (const auto& view : m_framePlayers)
        nearestPlayerDistance = std::min(nearestPlayerDistance,
            glm::distance(entity.position, view.player->getPosition()));
    if (hostile(entity.type) && shouldHostileDespawn(
            static_cast<float>(nearestPlayerDistance),
            entity.ageSeconds,aiHash(entity.behaviorSeed+static_cast<uint32_t>(entity.ageSeconds)))) {
        entity.health=0;entity.despawning=true; cancelNavigation(entity); return;
    }
    // Invalidation is immediate even while expensive decisions are throttled.
    if (ai.hasTarget) {
        Entity* other=ai.targetId ? aiEntity(ai.targetId) : nullptr;
        const bool valid=ai.targetId ? other && livingMob(*other) &&
            (ai.retaliating || !hostile(other->type)) : playerTargetable;
        if (!valid || m_aiTime>=ai.memoryUntil) {
            ai.hasTarget=false; ai.targetVisible=false;
            entity.attackPending=false;
            ai.hasDestination=false;
            ai.nextDecision=m_aiTime;
            cancelNavigation(entity);
        }
    }
    if (m_aiTime+1e-6>=ai.nextDecision) {
        decideBehavior(entity,player,isDay,playerTargetable,worldTick,thunderstorm);
        if(const auto* e=Plugins::entityContent(static_cast<uint16_t>(entity.type)))ai.speed=e->speed;
        else if(nativeNetherMob(entity.type))ai.speed=nativeMobSpeed(entity.type);
        const float interval=nearestPlayerDistance<=Config::AI_NEAR_DISTANCE ?
            Config::AI_NEAR_INTERVAL : Config::AI_FAR_INTERVAL;
        ai.nextDecision=m_aiTime+interval;
    }
    auto target=[&]() -> std::optional<glm::dvec3> {
        if (!ai.hasTarget) return std::nullopt;
        if (!ai.targetId) return playerTargetable ? std::optional<glm::dvec3>(player.getPosition()) : std::nullopt;
        Entity* other=aiEntity(ai.targetId);
        return other && livingMob(*other) ? std::optional<glm::dvec3>(other->position) : std::nullopt;
    };
    const float baseDamage=Plugins::entityContent(static_cast<uint16_t>(entity.type))?Plugins::entityContent(static_cast<uint16_t>(entity.type))->damage:entityTemplate(entity.type)==EntityType::IronGolem ? 10 : nativeMobDamage(entity.type);
    const float meleeDamage=std::max(0.f,baseDamage+3*entity.effects.level(StatusEffectType::Strength)-4*entity.effects.level(StatusEffectType::Weakness));
    const auto events=m_modelRegistry.advance(entity.type,entity.id,dt);
    for (const auto& event:events) {
        if (!entity.attackPending) continue;
        if (event.name!="melee" && event.name!="shoot" && event.name!="explode") continue;
        entity.attackPending=false;
        auto position=target();
        if (!position) continue;
        const bool ranged=event.name=="shoot";
        const glm::dvec3 origin=entity.position+glm::dvec3(0,ranged ? 1.45 : 1.2,0);
        const glm::dvec3 center=!ai.targetId && ranged ? player.getEyePosition() : *position+glm::dvec3(0,.9,0);
        const double distance=glm::distance(origin,center);
        if (distance>=(ranged ? 14.0 : entityTemplate(entity.type)==EntityType::IronGolem ? 2.5 : 1.5) || !aiClearSight(origin,center)) continue;
        glm::vec3 direction=distance>.001 ? glm::vec3((center-origin)/distance) : glm::vec3(0);
        if (ranged) {
            auto velocity=lowArcBallisticVelocity(origin,center,bowLaunchSpeed(.9f),
                                                  entity.velocity+ai.previousLocomotion);
            if (velocity) m_aiArrows.push_back({origin+glm::dvec3(glm::normalize(*velocity))*.75,
                                                *velocity,2.0f,entity.id});
        } else if (event.name=="explode") {
            m_aiExplosions.push_back({entity.position,entity.behaviorSeed});
            entity.health=0;
        } else {
            glm::vec3 knockback(direction.x,0,direction.z);
            if (glm::length(knockback)>.001f) knockback=glm::normalize(knockback);
            if (!ai.targetId) {
                DamageSourceInfo source;
                source.amount=meleeDamage; source.cause=DamageCause::Melee;
                source.shieldBlockable=true; source.hasOrigin=true; source.origin=origin;
                source.impulse=knockback*4.0f+glm::vec3(0,2,0);
                const auto outcome=player.takeDamage(source);
                if(outcome.appliedDamage>0 && entity.type==EntityType::WitherSkeleton)player.statusEffects().apply(StatusEffectType::Wither,0,200);
            } else if (Entity* other=aiEntity(ai.targetId)) {
                damageEntity(*other,meleeDamage,knockback*3.0f+glm::vec3(0,entityTemplate(entity.type)==EntityType::IronGolem ? 5.0f : 1.5f,0),false,entity.position,entity.id);
                if (other->health<=0 && other->type==EntityType::Villager &&
                    (entityTemplate(entity.type)==EntityType::Zombie || entityTemplate(entity.type)==EntityType::ZombieVillager) &&
                    villagerInfectionConverts(player.difficulty(),aiHash(entity.behaviorSeed^
                        other->behaviorSeed^static_cast<uint32_t>(worldTick)))) {
                    other->type=EntityType::ZombieVillager; other->health=20;
                    other->villager.hasBed=false; other->villager.hasWorkstation=false;
                    other->sleeping=false; cancelNavigation(*other); other->ai={};
                    ai.hasTarget=false;
                }
            }
        }
    }
    if (entity.attackPending && !m_modelRegistry.playing(entity.type,entity.id,"attack"))
        entity.attackPending=false;
    if (entity.health<=0) return;
    if (!entity.attackPending && !entity.sleeping) followNavigation(entity,dt);
    if (auto position=target()) {
        const glm::vec3 horizontal(position->x-entity.position.x,0,position->z-entity.position.z);
        if (ai.targetVisible && glm::length(horizontal)>.001f) entity.facing=glm::normalize(horizontal);
        if (!entity.attackPending && entity.actionCooldown<=0 && ai.targetVisible) {
            const bool ranged=entityTemplate(entity.type)==EntityType::Skeleton;
            const glm::dvec3 origin=entity.position+glm::dvec3(0,ranged ? 1.45 : 1.2,0);
            const glm::dvec3 center=!ai.targetId && ranged ? player.getEyePosition() : *position+glm::dvec3(0,.9,0);
            if (glm::distance(origin,center)<(ranged ? 14.0 : entityTemplate(entity.type)==EntityType::IronGolem ? 2.5 : 1.5) && aiClearSight(origin,center)) {
                entity.actionCooldown=ranged ? 2.0f : 1.0f;
                entity.attackPending=m_modelRegistry.playAction(entity.type,entity.id,"attack");
            }
        }
    }
    if (entityTemplate(entity.type)==EntityType::Villager) {
        auto& v=entity.villager;
        const uint32_t day=static_cast<uint32_t>(worldTick/24000);
        if (v.lastRestockDay!=day) { v.lastRestockDay=day; v.restocksToday=0; }
        const uint32_t tick=static_cast<uint32_t>(worldTick%24000);
        const bool first=tick>=2000 && tick<4000, second=tick>=9000 && tick<11000;
        if (ai.behavior==EntityBehavior::Work && v.hasWorkstation &&
            ((first && v.restocksToday<1) || (second && v.restocksToday<2)) &&
            interactablePoi(entity,v.claimedWorkstation)) restockVillager(v,day,true);
    }
    m_modelRegistry.setLocomotion(entity.type,entity.id,
        std::hypot(entity.locomotionVelocity.x,entity.locomotionVelocity.z));
}


bool EntityManager::nativeTargetsPlayer(Entity& entity,const Player& player) const {
    if(entity.type==EntityType::Strider)return false;
    if(entity.type==EntityType::ZombifiedPiglin)return entity.nether.angerTicks>0;
    if(entity.type==EntityType::Piglin) {
        if(entity.nether.barterTicks)return false;
        if(entity.nether.angerTicks)return true;
        for(const auto& armor:player.inventory().armor())
            if(!armor.empty() && armor.id>=ItemId::GOLDEN_HELMET && armor.id<=ItemId::GOLDEN_BOOTS)return false;
    }
    if(entity.type==EntityType::Enderman && !entity.nether.angerTicks) {
        const auto eye=entity.position+glm::dvec3(0,2.65,0);
        const auto delta=eye-player.getEyePosition();const auto distance=glm::length(delta);
        if(distance>32 || distance<.01 || glm::dot(glm::dvec3(player.getForward()),delta/distance)<1-.025/distance)return false;
        entity.nether.angerTicks=600;
    }
    return true;
}

void EntityManager::angerPiglins(const glm::dvec3& position,bool zombified) {
    for(auto& entity:m_entities)if(entity.health>0 &&
        (zombified?entity.type==EntityType::ZombifiedPiglin:entity.type==EntityType::Piglin) && glm::distance(entity.position,position)<=16)
        entity.nether.angerTicks=600;
}

bool EntityManager::teleportEnderman(Entity& entity) {
    const auto rules=dimensionRules(m_world.dimension());
    for(uint32_t i=0;i<16;++i) {
        const uint32_t h=aiHash(entity.behaviorSeed^(++entity.nether.barterSequence));
        const int x=int(std::floor(entity.position.x))+int(h%33)-16;
        const int z=int(std::floor(entity.position.z))+int((h>>8)%33)-16;
        for(int y=std::min(rules.maximumY-3,int(entity.position.y)+8);y>=std::max(rules.minimumY+1,int(entity.position.y)-8);--y) {
            const auto support=m_world.getBlock(x,y-1,z);
            if(!isSolid(support)||isLava(support)||support==BlockId::MAGMA_BLOCK)continue;
            const glm::dvec3 candidate(x+.5,y,z+.5);
            bool dry=true;
            for(int dy=0;dy<3;++dy) {
                const auto block=m_world.getLoadedBlock(x,y+dy,z);
                if(!block||isFluid(*block)||*block==BlockId::FIRE||*block==BlockId::SOUL_FIRE)dry=false;
            }
            if(dry&&!collides(entity,candidate)) {
                entity.position=candidate;entity.velocity={0,0,0};cancelNavigation(entity);return true;
            }
        }
    }
    return false;
}

void EntityManager::tickNativeState(Entity& entity,float dt) {
    entity.nativeTickAccumulator+=dt*20;
    while(entity.nativeTickAccumulator>=1) {
        entity.nativeTickAccumulator-=1;
        auto& state=entity.nether;
        if(state.angerTicks && --state.angerTicks==0 && (entity.type==EntityType::ZombifiedPiglin||entity.type==EntityType::Enderman)){entity.ai={};entity.attackPending=false;}
        if(state.attackTicks)--state.attackTicks;
        if(state.boostTicks)--state.boostTicks;
        entity.effects.tick([&](float h){applyEntityPotion(entity,PotionDefinition{{StatusEffectType::Healing,0,1},false},h/4);},
                            [&](float h){damageEntity(entity,h,glm::vec3(0),false);});
        if((entity.type==EntityType::Piglin || entity.type==EntityType::PiglinBrute || entity.type==EntityType::Hoglin) && m_world.dimension()==DimensionId::Overworld) {
            if(++state.conversionTicks>=300) {
                entity.type=entity.type==EntityType::Hoglin?EntityType::Zoglin:EntityType::ZombifiedPiglin;
                state.conversionTicks=0;entity.health=std::min(entity.health,nativeMobHealth(entity.type));entity.ai={};
            }
        } else state.conversionTicks=0;
        if(state.cureTicks && --state.cureTicks==0 && entity.type==EntityType::ZombieVillager) {
            entity.type=EntityType::Villager;entity.health=20;entity.effects.clear();entity.ai={};
        }
        if(state.barterTicks && --state.barterTicks==0 && entity.type==EntityType::Piglin) {
            // One payment has one durable timer and sequence; no request-order RNG.
            const auto roll=aiHash(entity.behaviorSeed^(++state.barterSequence));
            constexpr ItemId drops[]={ItemId::ENDER_PEARL,ItemId::QUARTZ,ItemId::OBSIDIAN,ItemId::CRYING_OBSIDIAN,
                ItemId::STRING,ItemId::LEATHER,ItemId::FIRE_RESISTANCE_POTION,ItemId::SOUL_SAND};
            Entity drop;drop.type=EntityType::Item;drop.health=1;drop.position=entity.position+glm::dvec3(0,.8,0);
            drop.velocity=entity.facing*2.f+glm::vec3(0,2,0);drop.actionCooldown=1;
            drop.item={drops[roll%8],static_cast<uint8_t>(roll%8==6?1:2+(roll>>8)%3),0};
            m_nativeSpawns.push_back(drop);
        }
    }
    if(entity.type==EntityType::Enderman && touchesWater(entity) && entity.nether.attackTicks==0) {
        entity.nether.attackTicks=10;
        if(damageEntity(entity,1,glm::vec3(0),false)<=0)teleportEnderman(entity);
    }
    if(entity.type==EntityType::Strider) {
        const auto pos=glm::ivec3(glm::floor(entity.position));
        if(isLava(m_world.getBlock(pos.x,pos.y,pos.z)) || isLava(m_world.getBlock(pos.x,pos.y-1,pos.z))) {
            const double top=std::floor(entity.position.y)+1;
            if(isLava(m_world.getBlock(pos.x,pos.y,pos.z)) && !isLava(m_world.getBlock(pos.x,pos.y+1,pos.z)))entity.position.y=top;
            entity.velocity.y=std::max(entity.velocity.y,0.f);entity.ai.grounded=true;
        }
    }
    if(entity.type==EntityType::MagmaCube && entity.ai.grounded && entity.nether.attackTicks==0) {
        entity.velocity.y=5.f+entity.nether.magmaSize;entity.ai.grounded=false;entity.nether.attackTicks=30;
    }
}

bool EntityManager::updateNativeMob(Entity& entity,Player& player,float dt,bool targetable) {
    if(entity.type==EntityType::Strider && entity.riderId!=UINT64_MAX) {
        auto found=std::find_if(m_framePlayers.begin(),m_framePlayers.end(),[&](const auto& v){return v.id==entity.riderId;});
        if(found==m_framePlayers.end()||found->player->survivalStats().dead()) {
            entity.riderId=UINT64_MAX;return false;
        }
        auto& rider=*found->player;
        if(rider.sneakInput() && tryDismount(rider,entity.riderId))return false;
        if(rider.inventory().slot(size_t(rider.selectedSlot())).id==ItemId::WARPED_FUNGUS_ON_A_STICK) {
            const auto pos=glm::ivec3(glm::floor(entity.position));
            const bool warm=isLava(m_world.getBlock(pos.x,pos.y-1,pos.z))||isLava(m_world.getBlock(pos.x,pos.y,pos.z));
            glm::vec3 forward(rider.getForward().x,0,rider.getForward().z);
            if(glm::length(forward)>.001f)forward=glm::normalize(forward);
            const auto before=entity.position;const auto previousVelocity=entity.velocity;
            moveWithTerrain(entity,forward*(warm?4.2f:1.1f)*(entity.nether.boostTicks?1.8f:1.f),dt);
            if(riderCollides(entity.position+glm::dvec3(0,1.7,0))) {
                entity.position=before;entity.velocity=previousVelocity;entity.locomotionVelocity=glm::vec3(0);
            }
        }
        rider.followMount(entity.position+glm::dvec3(0,1.7,0));
        m_modelRegistry.advance(entity.type,entity.id,dt);
        m_modelRegistry.setLocomotion(entity.type,entity.id,glm::length(entity.locomotionVelocity));
        return true;
    }
    if(entity.type!=EntityType::Ghast && entity.type!=EntityType::Blaze)return false;
    const auto eye=entity.position+glm::dvec3(0,entity.type==EntityType::Ghast?2:1.5,0);
    const auto delta=player.getEyePosition()-eye;const auto distance=glm::length(delta);
    if(m_aiTime>=entity.ai.nextDecision) {
        ++m_aiStats.decisions;
        entity.ai.targetVisible=targetable&&distance<48&&aiClearSight(eye,player.getEyePosition());
        entity.ai.hasTarget=targetable&&(entity.ai.targetVisible||m_aiTime<entity.ai.memoryUntil);
        if(entity.ai.targetVisible){entity.ai.lastSeen=player.getEyePosition();entity.ai.memoryUntil=m_aiTime+3;}
        entity.ai.nextDecision=m_aiTime+(distance<Config::AI_NEAR_DISTANCE?Config::AI_NEAR_INTERVAL:Config::AI_FAR_INTERVAL);
    }
    glm::vec3 motion(0);
    if(targetable&&distance>.01 && (entity.ai.targetVisible || m_aiTime<entity.ai.memoryUntil)) {
        const auto direction=glm::vec3(glm::normalize(entity.ai.lastSeen-eye));
        entity.facing=direction;
        motion=direction*(distance>(entity.type==EntityType::Ghast?18:8)?nativeMobSpeed(entity.type):-.5f);
        if(entity.ai.targetVisible && entity.nether.attackTicks==0) {
            const auto type=entity.type==EntityType::Ghast?EntityType::GhastFireball:EntityType::BlazeFireball;
            Entity projectile;projectile.type=type;projectile.health=1;projectile.position=eye+glm::dvec3(direction)*1.5;
            projectile.velocity=direction*(type==EntityType::GhastFireball?12.f:18.f);projectile.shooterId=entity.id;
            m_nativeSpawns.push_back(projectile);
            if(type==EntityType::BlazeFireball) {
                entity.nether.attackTicks=++entity.nether.burstRemaining<3?6:80;
                if(entity.nether.burstRemaining>=3)entity.nether.burstRemaining=0;
            } else entity.nether.attackTicks=60;
            entity.attackPending=m_modelRegistry.playAction(entity.type,entity.id,"attack");
        }
    } else {
        const float phase=float(m_aiTime)+entity.behaviorSeed*.01f;
        motion={std::sin(phase*.2f)*.6f,std::cos(phase*.3f)*.3f,std::cos(phase*.2f)*.6f};
    }
    const int steps=sweptCollisionSteps(glm::length(motion)*dt);
    for(int i=0;i<steps;++i) {
        const auto next=entity.position+glm::dvec3(motion)*(double(dt)/steps);
        if(!collides(entity,next))entity.position=next;
    }
    entity.locomotionVelocity=motion;
    m_modelRegistry.advance(entity.type,entity.id,dt);
    m_modelRegistry.setLocomotion(entity.type,entity.id,glm::length(motion));
    return true;
}
