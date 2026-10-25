#include "entity/EntityManager.h"
#include "entity/EntityPresentation.h"
#include "plugins/Runtime.h"
#include "plugins/Json.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include "world/World.h"
#include "player/Player.h"
#include <algorithm>
#include <cmath>
#include <limits>

bool EntityManager::pluginSnapshot(uint64_t id,MC_EntitySnapshot& out,std::string& state) const {
    const auto* entity=entityById(id);if(!entity||entity->health<=0)return false;
    out.size=sizeof(out);out.id=id;out.type=static_cast<uint16_t>(entity->type);
    out.dimension=static_cast<uint32_t>(m_world.dimension());
    for(int i=0;i<3;++i){out.position[i]=entity->position[i];out.velocity[i]=entity->velocity[i];}
    out.health=entity->health;out.age=entity->ageSeconds;out.state_version=entity->pluginStateVersion;
    state=entity->pluginState;out.state=nullptr;return true;
}
bool EntityManager::pluginRemove(uint64_t id) {
    auto it=std::find_if(m_entities.begin(),m_entities.end(),[&](const auto& e){return e.id==id&&e.health>0;});
    if(it==m_entities.end())return false;
    it->health=0;m_dirtyEntityChunks.insert(std::make_pair(static_cast<int>(std::floor(it->position.x/16)),static_cast<int>(std::floor(it->position.z/16))));return true;
}
bool EntityManager::pluginIntent(uint64_t id,const MC_EntityIntent& intent) {
    auto it=std::find_if(m_entities.begin(),m_entities.end(),[&](const auto& e){return e.id==id&&e.health>0;});
    if(it==m_entities.end()||!registeredMob(it->type)||intent.jump>1)return false;
    for(float value:intent.movement)if(!std::isfinite(value)||std::abs(value)>32)return false;
    if(intent.state) {
        std::string state=intent.state;if(state.size()>MC_ENTITY_STATE_LIMIT)return false;
        Plugins::Json::parse(state);it->pluginState=std::move(state);
    }
    m_dirtyEntityChunks.insert({static_cast<int>(std::floor(it->position.x/16)),static_cast<int>(std::floor(it->position.z/16))});
    it->pluginIntentUntil=m_aiTime+Config::AI_NEAR_INTERVAL;
    it->pluginMovement={intent.movement[0],0,intent.movement[2]};it->pluginAttack=intent.attack_player;
    if(intent.jump && std::abs(it->velocity.y)<.001f&&collides(*it,it->position-glm::dvec3(0,.06,0)))it->velocity.y=Config::AI_JUMP_SPEED;
    return true;
}
void EntityManager::pluginEvent(Entity& entity,uint32_t kind) {
    const auto* definition=Plugins::entityContent(static_cast<uint16_t>(entity.type));
    if(!definition||!definition->callback)return;
    MC_EntitySnapshot snapshot{};snapshot.size=sizeof(snapshot);snapshot.dimension=static_cast<uint32_t>(m_world.dimension());
    snapshot.id=entity.id;snapshot.type=static_cast<uint16_t>(entity.type);snapshot.health=entity.health;snapshot.age=entity.ageSeconds;
    for(int i=0;i<3;++i){snapshot.position[i]=entity.position[i];snapshot.velocity[i]=entity.velocity[i];}
    snapshot.state=entity.pluginState.c_str();snapshot.state_version=entity.pluginStateVersion;
    MC_EntityIntent intent{};intent.size=sizeof(intent);intent.attack_player=UINT64_MAX;
    const auto actor=Plugins::context();Plugins::ActorScope scope({actor.player,snapshot.dimension,actor.role});
    definition->callback(kind,snapshot,intent);
    if(kind==MC_ENTITY_DECIDE||kind==MC_ENTITY_SPAWN) {
        // Validate before publishing callback output; malformed output faults the session.
        bool valid=intent.size>=sizeof(intent)&&intent.jump<=1;
        for(float value:intent.movement)valid=valid&&std::isfinite(value)&&std::abs(value)<=32;
        try {
            if(intent.state){std::string state=intent.state;valid=valid&&state.size()<=MC_ENTITY_STATE_LIMIT;Plugins::Json::parse(state);if(valid)entity.pluginState=std::move(state);}
        }catch(...){valid=false;}
        if(!valid){Plugins::mutableContentState().runtimeFault="Invalid entity intent: "+definition->key;return;}
        entity.pluginMovement={intent.movement[0],0,intent.movement[2]};entity.pluginAttack=intent.attack_player;
        if(intent.jump&&std::abs(entity.velocity.y)<.001f&&collides(entity,entity.position-glm::dvec3(0,.06,0)))entity.velocity.y=Config::AI_JUMP_SPEED;
    }
}
bool EntityManager::pluginAi(Entity& entity,float dt) {
    const auto* definition=Plugins::entityContent(static_cast<uint16_t>(entity.type));
    if((!definition||!definition->callback)&&m_aiTime>=entity.pluginIntentUntil)return false;
    if(definition&&definition->callback&&m_aiTime>=entity.ai.nextDecision){pluginEvent(entity,MC_ENTITY_DECIDE);double distance=std::numeric_limits<double>::max();for(const auto& player:m_framePlayers)distance=std::min(distance,glm::distance(entity.position,player.player->getPosition()));
        entity.ai.nextDecision=m_aiTime+(distance<=Config::AI_NEAR_DISTANCE?Config::AI_NEAR_INTERVAL:Config::AI_FAR_INTERVAL);}
    if(!Plugins::mutableContentState().runtimeFault.empty())return true;
    moveWithTerrain(entity,entity.pluginMovement,dt);
    if(glm::length(entity.pluginMovement)>.001f)entity.facing=glm::normalize(entity.pluginMovement);
    presentation().setLocomotion(entity.type,entity.id,glm::length(entity.pluginMovement));
    (void)presentation().advance(entity.type,entity.id,dt);
    if(entity.pluginAttack!=UINT64_MAX&&entity.actionCooldown<=0)for(const auto& view:m_framePlayers) {
        if(view.id!=entity.pluginAttack||!view.damageable||!view.targetable)continue;
        const auto origin=entity.position+glm::dvec3(0,entitySize(entity).y*.75f,0);
        if(glm::distance(origin,view.player->getEyePosition())>3||!aiClearSight(origin,view.player->getEyePosition()))continue;
        DamageSourceInfo source;source.amount=definition?definition->damage:3;source.cause=DamageCause::Melee;source.shieldBlockable=true;source.hasOrigin=true;source.origin=entity.position;
        view.player->takeDamage(source);entity.actionCooldown=1;presentation().play(entity.type,entity.id,"attack");
    }
    return true;
}
