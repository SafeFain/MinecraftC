#include "entity/EntityManager.h"
#include "entity/ProjectileLogic.h"
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
           e.type!=EntityType::PrimedTnt;
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
    const auto terrain=navigationTerrain();
    const double offsets[]={0,.785398163,-.785398163,1.570796327,-1.570796327};
    ai.hasDestination=false;
    for (int i=0;i<5;++i) {
        const double angle=offsets[(i+ai.sequence)%5];
        const glm::dvec3 direction(away.x*std::cos(angle)-away.z*std::sin(angle),0,
                                   away.x*std::sin(angle)+away.z*std::cos(angle));
        const auto destination=GroundNavigation::stand(terrain,renderSize(entity.type),
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
    const glm::dvec3 eye=entity.position+glm::dvec3(0,renderSize(entity.type).y*.8,0);
    if (entity.type==EntityType::Villager) {
        Entity* threat=nullptr;
        double nearest=Config::AI_VILLAGER_FEAR_DISTANCE;
        for (uint64_t id:nearbyEntities(entity.position,nearest)) {
            Entity* other=aiEntity(id);
            if (!other || !hostile(other->type)) continue;
            const double distance=glm::distance(entity.position,other->position);
            if (distance<nearest && aiClearSight(eye,other->position+
                                              glm::dvec3(0,renderSize(other->type).y*.5,0))) {
                threat=other; nearest=distance;
            }
        }
        if (threat) {
            ai.danger=threat->position;
            ai.panicUntil=m_aiTime+Config::AI_PANIC_SECONDS;
        }
    }
    if (!hostile(entity.type) && ai.panicUntil>m_aiTime) {
        if (previousBehavior!=EntityBehavior::Flee) {
            cancelNavigation(entity);
            ai.nextPath=m_aiTime;
            ai.hasDestination=false;
        }
        ai.behavior=EntityBehavior::Flee;
        ai.speed=entity.type==EntityType::Villager ? Config::AI_VILLAGER_PANIC_SPEED :
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
    if (hostile(entity.type)) {
        const double playerDistance=glm::distance(entity.position,player.getPosition());
        if (entity.type==EntityType::Spider && playerDistance>=Config::AI_SIGHT_DISTANCE)
            entity.spiderProvoked=false;
        const bool playerAllowed=playerTargetable &&
            (entity.type!=EntityType::Spider || !isDay || thunderstorm || entity.spiderProvoked);
        ai.targetVisible=false;
        if (ai.hasTarget) {
            Entity* other=ai.targetId ? aiEntity(ai.targetId) : nullptr;
            const bool valid=ai.targetId ? other && livingMob(*other) &&
                (ai.retaliating || !hostile(other->type)) : playerAllowed;
            if (!valid) ai.hasTarget=false;
            else {
                const glm::dvec3 position=other ? other->position : player.getPosition();
                const glm::dvec3 center=position+glm::dvec3(0,other ? renderSize(other->type).y*.5 : .9,0);
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
            if (entity.type==EntityType::Zombie || entity.type==EntityType::ZombieVillager) {
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
            ai.speed=entity.type==EntityType::Spider ? 3.0f : 2.0f;
            const double distance=glm::distance(entity.position,ai.lastSeen);
            if (entity.type==EntityType::Skeleton && ai.targetVisible &&
                distance<Config::AI_SKELETON_MIN_DISTANCE) {
                if (previousBehavior!=EntityBehavior::Chase || ai.path.empty() ||
                    GroundNavigation::reached(entity.position,ai.destination)) chooseEscape(entity,ai.lastSeen);
                else ai.hasDestination=true;
            } else if (entity.type==EntityType::Skeleton && ai.targetVisible &&
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
    if (entity.type==EntityType::Villager) {
        const auto& v=entity.villager;
        const uint32_t tick=static_cast<uint32_t>(worldTick%24000);
        const bool working=(tick>=2000 && tick<4000) || (tick>=9000 && tick<11000);
        std::optional<glm::ivec3> poi;
        if (!isDay && v.hasBed) { poi=v.claimedBed; ai.behavior=EntityBehavior::Sleep; }
        else if (working && v.hasWorkstation) { poi=v.claimedWorkstation; ai.behavior=EntityBehavior::Work; }
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
        if (!v.hasBed || !v.hasWorkstation) {
            requestPoiClaim(entity);
            if (ai.pathStatus==GroundNavigation::Status::Pending &&
                ai.purpose!=NavigationPurpose::Move) { ai.behavior=EntityBehavior::Idle; return; }
        }
        if (v.hasBed && v.hasWorkstation) {
            const glm::dvec3 center=(glm::dvec3(v.claimedBed)+glm::dvec3(v.claimedWorkstation))*.5;
            if (glm::distance(entity.position,center)>12) {
                if (auto destination=GroundNavigation::stand(navigationTerrain(),renderSize(entity.type),
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
            if (auto destination=GroundNavigation::stand(navigationTerrain(),renderSize(entity.type),
                    std::floor(entity.position.x+std::cos(angle)*radius)+.5,
                    std::floor(entity.position.z+std::sin(angle)*radius)+.5,entity.position.y))
                ai.destination={*destination,.35,.6};
            else ai.behavior=EntityBehavior::Idle;
        }
    }
    if (ai.behavior==EntityBehavior::Wander) {
        ai.hasDestination=true;
        ai.speed=entity.type==EntityType::Villager ? .45f : .65f;
    }
}

void EntityManager::updateMobAi(Entity& entity,Player& player,float dt,bool isDay,
                               bool playerTargetable,uint64_t worldTick,bool thunderstorm) {
    auto& ai=entity.ai;
    if (hostile(entity.type) && shouldHostileDespawn(
            static_cast<float>(glm::distance(entity.position,player.getPosition())),
            entity.ageSeconds,aiHash(entity.behaviorSeed+static_cast<uint32_t>(entity.ageSeconds)))) {
        entity.health=0; cancelNavigation(entity); return;
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
        const float interval=glm::distance(entity.position,player.getPosition())<=Config::AI_NEAR_DISTANCE ?
            Config::AI_NEAR_INTERVAL : Config::AI_FAR_INTERVAL;
        ai.nextDecision=m_aiTime+interval;
    }
    auto target=[&]() -> std::optional<glm::dvec3> {
        if (!ai.hasTarget) return std::nullopt;
        if (!ai.targetId) return playerTargetable ? std::optional<glm::dvec3>(player.getPosition()) : std::nullopt;
        Entity* other=aiEntity(ai.targetId);
        return other && livingMob(*other) ? std::optional<glm::dvec3>(other->position) : std::nullopt;
    };
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
        if (distance>=(ranged ? 14.0 : 1.5) || !aiClearSight(origin,center)) continue;
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
                source.amount=3; source.cause=DamageCause::Melee;
                source.shieldBlockable=true; source.hasOrigin=true; source.origin=origin;
                source.impulse=knockback*4.0f+glm::vec3(0,2,0);
                player.takeDamage(source);
            } else if (Entity* other=aiEntity(ai.targetId)) {
                damageEntity(*other,3,knockback*3.0f+glm::vec3(0,1.5f,0),false,entity.position,entity.id);
                if (other->health<=0 && other->type==EntityType::Villager &&
                    (entity.type==EntityType::Zombie || entity.type==EntityType::ZombieVillager) &&
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
            const bool ranged=entity.type==EntityType::Skeleton;
            const glm::dvec3 origin=entity.position+glm::dvec3(0,ranged ? 1.45 : 1.2,0);
            const glm::dvec3 center=!ai.targetId && ranged ? player.getEyePosition() : *position+glm::dvec3(0,.9,0);
            if (glm::distance(origin,center)<(ranged ? 14.0 : 1.5) && aiClearSight(origin,center)) {
                entity.actionCooldown=ranged ? 2.0f : 1.0f;
                entity.attackPending=m_modelRegistry.playAction(entity.type,entity.id,"attack");
            }
        }
    }
    if (entity.type==EntityType::Villager) {
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
