#include "world/World.h"
#include "game/DimensionRules.h"
#include "world/RedstoneBlocks.h"
#include "world/RedstoneWiring.h"
#include "world/BlockEntityLogic.h"
#include "world/RedstonePersistence.h"
#include "world/NetherPortal.h"
#include <limits>
#include <climits>

using Redstone::Position;
using Redstone::Face;
using Redstone::Kind;

namespace {
bool observableChange(BlockId previous,BlockId current) {
    const auto old=Redstone::blockState(previous),next=Redstone::blockState(current);
    if(old && next && old->kind==Kind::Repeater && next->kind==Kind::Repeater) {
        auto a=*old,b=*next;a.locked=b.locked=false;
        return Redstone::encode(a)!=Redstone::encode(b);
    }
    return previous!=current;
}
class FlagScope {
public:
    explicit FlagScope(bool& flag):m_flag(flag),m_previous(flag) {m_flag=true;}
    ~FlagScope() {m_flag=m_previous;}
    FlagScope(const FlagScope&)=delete;
    FlagScope& operator=(const FlagScope&)=delete;
private:
    bool& m_flag;
    bool m_previous;
};
}

void World::registerRedstone(const glm::ivec3& p,BlockId id,bool notifyObservers,const std::vector<uint8_t>* saved) {
    const auto key=Redstone::position(p);
    if(!m_redstone.publishing) {
        const auto existing=m_redstone.nodes.find(key);
        if(existing!=m_redstone.nodes.end()) {
            m_redstone.scheduler.cancel(key,existing->second.identity);
            m_redstone.nodes.erase(existing);
        }
        if(const auto state=Redstone::blockState(id)) {
            if(m_redstone.nextIdentity==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Redstone identity overflow");
            auto inserted=m_redstone.nodes.emplace(key,RedstoneWorldState::Node{++m_redstone.nextIdentity,Redstone::Device(*state),{},false});
            if(saved && !saved->empty()) {
                auto& node=inserted.first->second;const auto snapshot=Redstone::decodeSnapshot(*saved);
                node.device.restoreBurnout(snapshot.burnout);node.device.restoreObserverPending(snapshot.observerPending);
                for(const auto& event:snapshot.events)node.suspended.push_back({event.remaining,{0,key,node.identity,event.kind,event.value,0}});
            }
            auto& node=inserted.first->second;
            if(state->kind==Kind::Observer && state->power && std::none_of(node.suspended.begin(),node.suspended.end(),[](const auto& event){return event.event.kind==Redstone::EventKind::ObserverOff;})) {
                // A moved powered observer has a fresh incarnation; its old
                // scheduled tick cannot follow its coordinate. Never latch it.
                node.device.restoreObserverPending(true);
                node.suspended.push_back({2,{0,key,node.identity,Redstone::EventKind::ObserverOff,0,0}});
            }
        }
    }
    DoorState door;
    if(decodeDoor(id,door)||id==BlockId::TNT)m_redstone.consumers.insert(key);
    else m_redstone.consumers.erase(key);
    const auto chunk=std::make_pair(worldToChunkX(p.x),worldToChunkZ(p.z));
    if(m_redstone.nodes.count(key) || m_redstone.consumers.count(key))m_redstone.components[chunk].insert(key);
    else {
        const auto found=m_redstone.components.find(chunk);
        if(found!=m_redstone.components.end()){found->second.erase(key);if(found->second.empty())m_redstone.components.erase(found);}
    }
    if(notifyObservers && !m_redstone.publishing)notifyRedstoneObservers(key);
}
void World::notifyRedstoneObservers(Position key) {
    for(uint8_t face=0;face<6;++face) {
        const auto neighbor=key+Redstone::offset(static_cast<Face>(face));
        const auto found=m_redstone.nodes.find(neighbor);
        if(found==m_redstone.nodes.end())continue;
        if(!getLoadedBlock(neighbor.x,neighbor.y,neighbor.z))continue;
        auto& node=found->second;const auto state=node.device.state();
        if(!node.active && std::none_of(getSimulationChunks().begin(),getSimulationChunks().end(),[&](const Chunk* chunk){return chunk->cx==worldToChunkX(neighbor.x) && chunk->cz==worldToChunkZ(neighbor.z);}))continue;
        if(state.kind==Kind::Observer && neighbor+Redstone::offset(Redstone::opposite(state.facing))==key) {
            node.device.input({0,0,false,true},neighbor,node.identity,m_redstone.scheduler);
            if(node.blocked) {
                const auto pending=m_redstone.scheduler.eventsFor(neighbor,node.identity);
                m_redstone.scheduler.cancel(neighbor,node.identity);
                for(const auto& event:pending)if(std::none_of(node.suspended.begin(),node.suspended.end(),[&](const auto& saved){return saved.event.kind==event.event.kind;}))node.suspended.push_back(event);
            }
            Redstone::CircuitSnapshot snapshot;snapshot.observerPending=node.device.observerPending();
            const auto events=node.active && !node.blocked?m_redstone.scheduler.eventsFor(neighbor,node.identity):node.suspended;
            for(const auto& event:events)snapshot.events.push_back({event.event.kind,static_cast<uint16_t>(event.remaining),event.event.value});
            m_persistence.setCircuitState(Redstone::vector(neighbor),Redstone::encodeSnapshot(snapshot));
        }
    }
}
void World::redstoneChanged(const glm::ivec3& p,BlockId previous,BlockId current) {
    if(!m_redstone.publishing) {
        // External edits are observed immediately, independently of any staged
        // internal transition at this position.
        m_redstone.observations.erase(Redstone::position(p));
    }
    notifyContainerChanged(p);
    registerRedstone(p,current,observableChange(previous,current));
    if(!m_redstone.publishing)m_persistence.setCircuitState(p,{});
    if(m_redstone.nodes.empty() && m_redstone.consumers.empty())return;
    const auto state=Redstone::blockState(current);
    if(!m_redstone.publishing || !state || state->kind!=Kind::Dust) {
        m_redstone.dirty=true;m_redstone.propagation.reset();m_redstone.graph.reset();
    }
    // A newly attached source must also discover naturally generated door/TNT
    // recipients without a whole-world voxel scan.
    for(uint8_t a=0;a<6;++a)for(int b=-1;b<6;++b) {
        auto q=p+faceOffset(static_cast<FaceDir>(a));
        if(b>=0)q+=faceOffset(static_cast<FaceDir>(b));
        const auto block=getLoadedBlock(q.x,q.y,q.z);if(!block)continue;
        DoorState door;if(decodeDoor(*block,door)||*block==BlockId::TNT) {
            const auto recipient=Redstone::position(q);m_redstone.consumers.insert(recipient);
            m_redstone.components[{worldToChunkX(q.x),worldToChunkZ(q.z)}].insert(recipient);
        }
    }
}

uint8_t World::redstoneOutput(const glm::ivec3& p,Face face,bool wire,bool& known) const {
    if(!dimensionRules(dimension()).contains(p.y))return 0;
    const auto loaded=getLoadedBlock(p.x,p.y,p.z);
    if(!loaded){known=false;return 0;}
    ButtonState button;
    if(decodeButton(*loaded,button))return button.pressed?15:0;
    auto state=Redstone::blockState(*loaded);
    const auto live=m_redstone.nodes.find(Redstone::position(p));
    if(m_redstone.evaluating && live!=m_redstone.nodes.end())state=live->second.device.state();
    if(!state)return 0;
    if(state->kind==Kind::Dust) {
        if(wire || face==Face::Up || (state->alternate && face!=Face::Down))return 0;
        if(m_redstone.frozenDust.count(Redstone::position(p)))known=false;
        if(face!=Face::Down) {
            const auto shape=Redstone::connections(Redstone::position(p),state->alternate,[&](Position q){
                const auto id=getLoadedBlock(q.x,q.y,q.z);
                if(!id && dimensionRules(dimension()).contains(q.y))known=false;
                return id.value_or(BlockId::AIR);
            });
            if(!(shape.mask&(1u<<(unsigned(face)-2))))return 0;
        }
        return state->power;
    }
    return Redstone::output(*state,face,Redstone::opposite(state->facing)).weak;
}
uint8_t World::redstoneConducted(const glm::ivec3& p,bool strongOnly,bool& known) const {
    const auto conductor=getLoadedBlock(p.x,p.y,p.z);
    if(!conductor){if(dimensionRules(dimension()).contains(p.y))known=false;return 0;}
    if(!Redstone::conductive(*conductor))return 0;
    uint8_t power=0;
    for(uint8_t raw=0;raw<6;++raw) {
        const auto direction=static_cast<Face>(raw);
        const auto source=p+Redstone::vector(Redstone::offset(direction));
        const auto input=getLoadedBlock(source.x,source.y,source.z);
        if(!input){if(dimensionRules(dimension()).contains(source.y))known=false;continue;}
        ButtonState button;
        if(decodeButton(*input,button)) {
            if(button.pressed && source-faceOffset(button.attachment)==p)power=15;
            continue;
        }
        auto state=Redstone::blockState(*input);
        const auto live=m_redstone.nodes.find(Redstone::position(source));
        if(m_redstone.evaluating && live!=m_redstone.nodes.end())state=live->second.device.state();
        if(state) {
            // Redstone blocks activate components directly, but never charge
            // an adjacent conductor. Conductors do not charge other conductors.
            if(state->kind==Kind::Block)continue;
            const auto face=Redstone::opposite(direction);
            if(strongOnly || state->kind==Kind::Lever)power=std::max(power,Redstone::output(*state,face,Redstone::opposite(state->facing)).strong);
            else power=std::max(power,redstoneOutput(source,face,false,known));
        }
    }
    return power;
}
uint8_t World::redstoneReceived(const glm::ivec3& p,bool wire,bool& known) const {
    uint8_t power=0;
    const auto receiver=Redstone::blockState(getBlock(p.x,p.y,p.z));
    for(uint8_t raw=0;raw<6;++raw) {
        const auto direction=static_cast<Face>(raw);
        if(receiver && receiver->kind==Kind::Piston && direction==receiver->facing)continue;
        const auto q=p+Redstone::vector(Redstone::offset(direction));
        power=std::max(power,redstoneOutput(q,Redstone::opposite(direction),wire,known));
        const auto cell=getLoadedBlock(q.x,q.y,q.z);
        if(cell && Redstone::conductive(*cell))power=std::max(power,redstoneConducted(q,wire,known));
    }
    return power;
}

void World::touchRedstoneOre(const glm::ivec3& p) {
    const auto id=getLoadedBlock(p.x,p.y,p.z);
    if(id && Redstone::native(*id) && Redstone::ore(Redstone::nativeSlot(*id)) && !Redstone::litOre(*id))
        setBlock(p.x,p.y,p.z,static_cast<BlockId>(unsigned(*id)+2));
}
bool World::placeRedstone(const glm::ivec3& p,BlockId id) {
    const auto state=Redstone::blockState(id);if(!state)return false;
    if(!dimensionRules(dimension()).contains(p.y) || getLoadedBlock(p.x,p.y,p.z)!=BlockId::AIR)return false;
    if(Redstone::thin(state->kind)) {
        const auto direction=(state->kind==Kind::Lever || state->kind==Kind::Torch)?state->facing:Face::Up;
        const auto support=p-Redstone::vector(Redstone::offset(direction));
        if(!supportsFace(support,static_cast<FaceDir>(direction)))return false;
    }
    setBlock(p.x,p.y,p.z,id);return true;
}
bool World::interactRedstone(const glm::ivec3& p) {
    touchRedstoneOre(p);
    const auto queued=m_redstone.externalEdits.find(Redstone::position(p));
    auto state=Redstone::blockState(queued==m_redstone.externalEdits.end()?getBlock(p.x,p.y,p.z):queued->second);if(!state)return false;
    switch(state->kind) {
        case Kind::Lever:case Kind::Comparator:case Kind::Dust:state->alternate=!state->alternate;break;
        case Kind::Repeater:state->delay=static_cast<uint8_t>(state->delay%4+1);break;
        default:return false;
    }
    setBlock(p.x,p.y,p.z,Redstone::block(*state));
    m_interactionSounds.push_back({p,false,state->alternate,true});return true;
}
void World::publishRedstone(Position p,const Redstone::State& state) {
    const auto id=Redstone::block(state);
    const auto previous=getLoadedBlock(p.x,p.y,p.z);
    if(!previous)return;
    if(state.kind!=Kind::Dust && state.kind!=Kind::Lamp) {
        m_redstone.dirty=true;m_redstone.propagation.reset();m_redstone.graph.reset();
    }
    if(*previous==id){m_redstone.staged.erase(p);return;}
    m_redstone.observations.emplace(p,*previous);
    m_redstone.staged[p]=id;
}

bool World::tickRedstone(RedstoneWorkBudget budget) {
    if(m_replicaMode)return true;
    FlagScope evaluating(m_redstone.evaluating);
    size_t work=budget.propagation;
    const auto& active=m_redstone.activeChunks;
    const auto scan=[&](auto& entries,std::optional<Position>& cursor,const auto& visit) {
        auto it=cursor?entries.upper_bound(*cursor):entries.begin();
        while(it!=entries.end() && work) {
            --work;const auto key=it->first;cursor=key;visit(*it);it=entries.upper_bound(key);
        }
        if(it!=entries.end())return false;
        cursor.reset();return true;
    };
    if(m_redstone.phase==RedstoneWorldState::Phase::Lifecycle) {
        if(!scan(m_redstone.nodes,m_redstone.cursor,[&](auto& entry) {
            auto& node=entry.second;const auto p=entry.first;
            const bool loaded=active.count({worldToChunkX(p.x),worldToChunkZ(p.z)})!=0;
            if(node.active==loaded)return;
            if(!loaded) {
                if(!node.blocked){node.suspended=m_redstone.scheduler.eventsFor(p,node.identity);m_redstone.scheduler.cancel(p,node.identity);}
            } else if(!node.blocked){m_redstone.scheduler.resume(node.suspended);node.suspended.clear();}
            node.active=loaded;m_redstone.dirty=true;m_redstone.propagation.reset();m_redstone.graph.reset();
        }))return false;
        m_redstone.phase=RedstoneWorldState::Phase::Inputs;
    }
    auto settle=[&]()->bool {
        if(!m_redstone.dirty)return true;
        if(!m_redstone.graph)m_redstone.graph=std::make_unique<RedstoneWorldState::GraphBuild>();
        auto& build=*m_redstone.graph;auto& wires=build.wires;auto& uncertain=build.uncertain;
        using Stage=RedstoneWorldState::GraphBuild::Stage;
        if(build.stage==Stage::Nodes) {
            if(!scan(m_redstone.nodes,build.cursor,[&](const auto& entry) {
                const auto& node=entry.second;const auto p=entry.first;
                if(!node.active || node.device.state().kind!=Kind::Dust)return;
                bool known=true;
                auto& wire=wires[p];wire.source=redstoneReceived(Redstone::vector(p),true,known);
                if(node.device.state().alternate) {if(!known){uncertain.insert(p);build.pending.insert(p);}return;}
                for(uint8_t side=2;side<6;++side) {
                    const auto q=p+Redstone::offset(static_cast<Face>(side));
                    for(int height=-1;height<=1;++height) {
                        const auto target=q+Position{0,height,0};const auto found=m_redstone.nodes.find(target);
                        if(found==m_redstone.nodes.end() || found->second.device.state().kind!=Kind::Dust || found->second.device.state().alternate)continue;
                        if(!found->second.active){known=false;continue;}
                        if(height) {
                            const auto above=getLoadedBlock(p.x,p.y+1,p.z),beside=getLoadedBlock(q.x,q.y,q.z);
                            if(!above || !beside){known=false;continue;}
                            if(height>0 && (isFullCollisionBlock(*above)||!blockSupportsFace(*beside,FaceDir::TOP)))continue;
                            if(height<0 && blockCollisionBoxes(*beside).count)continue;
                        }
                        wire.neighbors.push_back(target);build.adjacency[p].push_back(target);build.adjacency[target].push_back(p);
                    }
                }
                if(!known){uncertain.insert(p);build.pending.insert(p);}

            }))return false;
            build.stage=Stage::Boundary;
        }
        if(build.stage==Stage::Boundary) {
            while(!build.pending.empty() && work) {
                --work;const auto p=*build.pending.begin();build.pending.erase(build.pending.begin());
                for(const auto neighbor:build.adjacency[p])if(uncertain.insert(neighbor).second)build.pending.insert(neighbor);
            }
            if(!build.pending.empty())return false;
            m_redstone.frozenDust=std::move(uncertain);build.stage=Stage::Prune;
        }
        if(build.stage==Stage::Prune) {
            auto it=build.cursor?m_redstone.frozenDust.upper_bound(*build.cursor):m_redstone.frozenDust.begin();
            while(it!=m_redstone.frozenDust.end() && work){--work;build.cursor=*it;wires.erase(*it++);}
            if(it!=m_redstone.frozenDust.end())return false;
            build.cursor.reset();m_redstone.propagation=std::make_unique<Redstone::DustPropagation>(std::move(wires));build.stage=Stage::Propagate;
        }
        if(build.stage==Stage::Propagate) {
            if(!m_redstone.propagation->stepBudget(work))return false;
            build.stage=Stage::Publish;
        }
        if(!scan(m_redstone.propagation->result(),build.cursor,[&](const auto& entry) {
            auto& node=m_redstone.nodes.at(entry.first);auto state=node.device.state();
            if(state.power==entry.second)return;
            state.power=entry.second;node.device=Redstone::Device(state);publishRedstone(entry.first,state);
        }))return false;
        m_redstone.propagation.reset();m_redstone.graph.reset();m_redstone.dirty=false;
        return true;
    };
    const auto sample=[&](bool age) {
        return scan(m_redstone.nodes,m_redstone.cursor,[&](auto& entry) {
            auto& node=entry.second;if(!node.active)return;
            const auto state=node.device.state();const auto p=Redstone::vector(entry.first);
            bool known=true;Redstone::Inputs inputs;
            if(state.kind==Kind::Torch) {
                const auto support=p-Redstone::vector(Redstone::offset(state.facing));
                inputs.rear=redstoneOutput(support,state.facing,false,known);
                inputs.rear=std::max(inputs.rear,redstoneConducted(support,false,known));
            }
            else if(state.kind==Kind::Repeater || state.kind==Kind::Comparator) {
                const auto rear=p-Redstone::vector(Redstone::offset(state.facing));
                inputs.rear=redstoneOutput(rear,state.facing,false,known);
                const auto behind=getLoadedBlock(rear.x,rear.y,rear.z);
                if(behind && Redstone::conductive(*behind))inputs.rear=std::max(inputs.rear,redstoneConducted(rear,false,known));
                for(uint8_t side=2;side<6;++side) {
                    const auto face=static_cast<Face>(side);if(face==state.facing || face==Redstone::opposite(state.facing))continue;
                    const auto q=p+Redstone::vector(Redstone::offset(face));
                    const auto strength=redstoneOutput(q,Redstone::opposite(face),false,known);
                    const auto source=getLoadedBlock(q.x,q.y,q.z);const auto diode=source?Redstone::blockState(*source):std::nullopt;
                    if(diode && (diode->kind==Kind::Dust || diode->kind==Kind::Repeater || diode->kind==Kind::Comparator || diode->kind==Kind::Block))
                        inputs.side=std::max(inputs.side,strength);
                    if(diode && (diode->kind==Kind::Repeater || diode->kind==Kind::Comparator) && strength)inputs.locked=true;
                }
                if(state.kind==Kind::Comparator) {
                    auto containerPosition=rear;
                    auto container=static_cast<const World*>(this)->getBlockEntity(rear);
                    if(!container && behind && Redstone::conductive(*behind) && inputs.rear<15) {
                        const auto farther=rear-Redstone::vector(Redstone::offset(state.facing));
                        if(!getLoadedBlock(farther.x,farther.y,farther.z) && dimensionRules(dimension()).contains(farther.y))known=false;
                        else {containerPosition=farther;container=static_cast<const World*>(this)->getBlockEntity(farther);}
                    }
                    if(container && (container->type==BlockEntityType::Chest || container->type==BlockEntityType::Furnace || container->type==BlockEntityType::BrewingStand)) {
                        const auto key=Redstone::position(containerPosition);
                        auto cached=m_redstone.containerSignals.find(key);
                        if(cached==m_redstone.containerSignals.end()) {
                            std::vector<Redstone::Slot> slots;
                            for(int i=0;i<containerStackCount(container->type);++i) {const auto* stack=containerStack(*container,i);slots.push_back({stack->count,stack->empty()?uint8_t(64):getItemProps(stack->id).maxStack});}
                            cached=m_redstone.containerSignals.emplace(key,Redstone::fullness(slots)).first;
                        }
                        inputs.rear=cached->second;
                    }
                }
            } else if(state.kind==Kind::Lamp || state.kind==Kind::Piston)inputs.rear=redstoneReceived(p,false,known);
            else if(state.kind==Kind::Observer) {
                const auto front=p-Redstone::vector(Redstone::offset(state.facing));
                known=!dimensionRules(dimension()).contains(front.y)||getLoadedBlock(front.x,front.y,front.z).has_value();
            }
            else {if(age && !node.blocked)node.device.activeTick();return;}
            if(!known && !node.blocked) {
                node.suspended=m_redstone.scheduler.eventsFor(entry.first,node.identity);m_redstone.scheduler.cancel(entry.first,node.identity);node.blocked=true;
            } else if(known && node.blocked) {
                m_redstone.scheduler.resume(node.suspended);node.suspended.clear();node.blocked=false;
            }
            if(known && node.device.input(inputs,entry.first,node.identity,m_redstone.scheduler) && state.kind!=Kind::Lamp)
                publishRedstone(entry.first,node.device.state());
            if(age && !node.blocked)node.device.activeTick();
        });
    };
    if(m_redstone.phase==RedstoneWorldState::Phase::Inputs) {
        if(!settle())return false;
        m_redstone.phase=RedstoneWorldState::Phase::InputSample;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::InputSample) {
        if(!sample(true))return false;
        m_redstone.phase=RedstoneWorldState::Phase::Events;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Events) {
        if(!m_redstone.scheduler.advance(budget.events,[&](Position p,uint64_t identity) {
            const auto node=m_redstone.nodes.find(p);
            return node!=m_redstone.nodes.end() && node->second.identity==identity;
        },[&](const Redstone::Event& event) {
            auto& node=m_redstone.nodes.at(event.position);
            if(node.device.apply(event,m_redstone.scheduler) && node.device.state().kind!=Kind::Lamp)
                publishRedstone(event.position,node.device.state());
        }))return false;
        m_redstone.phase=RedstoneWorldState::Phase::Outputs;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Outputs) {
        if(!settle())return false;
        m_redstone.phase=RedstoneWorldState::Phase::OutputSample;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::OutputSample) {
        if(!sample(false))return false;
        m_redstone.phase=RedstoneWorldState::Phase::Lamps;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Lamps) {
        if(!scan(m_redstone.nodes,m_redstone.cursor,[&](const auto& entry) {
            const auto& node=entry.second;const auto state=node.device.state();
            if(!node.active || node.blocked)return;
            if(state.kind==Kind::Lamp)publishRedstone(entry.first,state);
            if(state.kind==Kind::Piston && !pistonReserved(Redstone::vector(entry.first))) {
                bool known=true;const bool powered=redstoneReceived(Redstone::vector(entry.first),false,known)>0;
                if(known && powered!=(state.power!=0))m_redstone.pistonRequests.push_back({entry.first,powered});
            }
        }))return false;
        m_redstone.phase=RedstoneWorldState::Phase::Consumers;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Consumers) {
        auto it=m_redstone.cursor?m_redstone.consumers.upper_bound(*m_redstone.cursor):m_redstone.consumers.begin();
        while(it!=m_redstone.consumers.end() && work) {
            --work;m_redstone.cursor=*it;
            if(active.count({worldToChunkX(it->x),worldToChunkZ(it->z)}))m_redstone.readyConsumers.push_back(*it);
            ++it;
        }
        if(it!=m_redstone.consumers.end())return false;
        m_redstone.cursor.reset();m_redstone.phase=RedstoneWorldState::Phase::Observations;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Observations) {
        if(!scan(m_redstone.observations,m_redstone.cursor,[&](const auto& entry) {
            const auto staged=m_redstone.staged.find(entry.first);
            const auto current=staged!=m_redstone.staged.end()?std::optional<BlockId>(staged->second):getLoadedBlock(entry.first.x,entry.first.y,entry.first.z);
            if(current && observableChange(entry.second,*current))notifyRedstoneObservers(entry.first);
        }))return false;
        m_redstone.observations.clear();m_redstone.phase=RedstoneWorldState::Phase::Snapshots;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Snapshots) {
        if(!scan(m_redstone.nodes,m_redstone.cursor,[&](const auto& entry) {
            const auto& node=entry.second;const auto kind=node.device.state().kind;
            if(kind==Kind::Dust || kind==Kind::Lever || kind==Kind::Block)return;
            Redstone::CircuitSnapshot snapshot;snapshot.burnout=node.device.burnout();snapshot.observerPending=node.device.observerPending();
            const auto events=node.active && !node.blocked?m_redstone.scheduler.eventsFor(entry.first,node.identity):node.suspended;
            for(const auto& event:events)snapshot.events.push_back({event.event.kind,static_cast<uint16_t>(event.remaining),event.event.value});
            m_persistence.setCircuitState(Redstone::vector(entry.first),Redstone::encodeSnapshot(snapshot));
        }))return false;
        m_redstone.phase=RedstoneWorldState::Phase::Motions;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Motions) {
        while(m_redstone.motionCursor<m_redstone.motions.size() && work){--work;++m_redstone.motionCursor;}
        if(m_redstone.motionCursor<m_redstone.motions.size())return false;
        m_redstone.motionCursor=0;m_redstone.phase=RedstoneWorldState::Phase::Commit;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Commit) {
        FlagScope publishing(m_redstone.publishing),transaction(m_blockTransaction),paired(m_pairMutation),commit(m_pistonCommit);
        if(!scan(m_redstone.staged,m_redstone.cursor,[&](const auto& entry){setBlock(entry.first.x,entry.first.y,entry.first.z,entry.second);}))return false;
        m_redstone.staged.clear();m_redstone.phase=RedstoneWorldState::Phase::Effects;
    }
    if(m_redstone.phase==RedstoneWorldState::Phase::Effects) {
        FlagScope publishing(m_redstone.publishing),paired(m_pairMutation);
        while(m_redstone.effectCursor<m_pendingBlockChanges.size() && work) {
            --work;const auto change=m_pendingBlockChanges[m_redstone.effectCursor++];
            finishBlockChange(change.position,change.previous,change.current,change.record,change.revision);
        }
        if(m_redstone.effectCursor<m_pendingBlockChanges.size())return false;
        m_redstone.effectCursor=0;m_pendingBlockChanges.clear();m_redstone.dirty=false;
        m_redstone.phase=RedstoneWorldState::Phase::Drive;
    }
    FlagScope committing(m_redstone.committingConsumers);
    if(m_redstone.phase==RedstoneWorldState::Phase::Drive) {
        while(m_redstone.driveCursor<m_redstone.readyConsumers.size() && work) {
            --work;const auto p=m_redstone.readyConsumers[m_redstone.driveCursor++];
            const auto loaded=getLoadedBlock(p.x,p.y,p.z);if(!loaded)continue;
            DoorState door;if(decodeDoor(*loaded,door)){refreshDoorPower(Redstone::vector(p));continue;}
            bool known=true;
            if(*loaded==BlockId::TNT && redstoneReceived(Redstone::vector(p),false,known) && known) {
                setBlock(p.x,p.y,p.z,BlockId::AIR);m_redstone.ignitions.push_back(Redstone::vector(p));
            }
        }
        if(m_redstone.driveCursor<m_redstone.readyConsumers.size())return false;
        m_redstone.driveCursor=0;m_redstone.readyConsumers.clear();m_redstone.phase=RedstoneWorldState::Phase::Pistons;
    }
    std::vector<glm::ivec3> ready;
    size_t examined=0;
    for(auto it=m_pendingDoorPower.begin();it!=m_pendingDoorPower.end() && examined++<Config::BUTTON_TRANSITIONS_PER_TICK;) {
        const auto [x,y,z]=*it;
        if(generatedAt(x,z)) {ready.emplace_back(x,y,z);it=m_pendingDoorPower.erase(it);}
        else ++it;
    }
    for(const auto& p:ready)refreshDoorPower(p);
    if(!tickPistons(work))return false;
    m_redstone.committedBefore.clear();m_redstone.motionView.clear();
    m_redstone.phase=RedstoneWorldState::Phase::Ready;
    return true;
}


bool World::pistonReserved(const glm::ivec3& p) const {
    return m_redstone.reservations.count(Redstone::position(p))!=0;
}
bool World::chunkPinned(int cx,int cz) const {
    if(m_redstone.phase!=RedstoneWorldState::Phase::Ready)return true;
    if(m_replicaMode)for(const auto& motion:m_redstone.motions)for(const auto& edit:motion.edits())
        if(worldToChunkX(edit.first.x)==cx && worldToChunkZ(edit.first.z)==cz)return true;
    for(auto it=m_redstone.reservations.lower_bound({cx*16,Config::WORLD_MIN_Y,INT_MIN});it!=m_redstone.reservations.end() && it->first.x<(cx+1)*16;++it)
        if(worldToChunkZ(it->first.z)==cz)return true;
    return false;
}
void World::applyBlockTransaction(const std::map<Position,BlockId>& edits) {
    if(m_blockTransaction)throw std::logic_error("Nested block transaction");
    for(const auto& edit:edits)if(!getLoadedBlock(edit.first.x,edit.first.y,edit.first.z))
        throw std::logic_error("Unavailable block transaction target");
    FlagScope commit(m_pistonCommit);
    {
        FlagScope transaction(m_blockTransaction),paired(m_pairMutation);
        for(const auto& edit:edits)setBlock(edit.first.x,edit.first.y,edit.first.z,edit.second);
    }
    // Raw writes precede all callbacks, support, fluid, light and signal work.
    // Worker mesh snapshots cannot be submitted until this main-thread call ends.
    auto pending=std::move(m_pendingBlockChanges);m_pendingBlockChanges.clear();
    {
        FlagScope paired(m_pairMutation);
        for(const auto& change:pending)finishBlockChange(change.position,change.previous,change.current,change.record,change.revision);
    }
    for(const auto& change:pending)validateInteractiveNeighbors(change.position);
    std::set<std::pair<int,int>> chunks;
    for(const auto& edit:edits) {
        const int cx=worldToChunkX(edit.first.x),cz=worldToChunkZ(edit.first.z);
        chunks.emplace(cx,cz);
        const int x=edit.first.x-cx*16,z=edit.first.z-cz*16;
        if(!m_redstone.publishing && (x==0 || x==15) && (z==0 || z==15))
            m_redstone.atomicMeshes.emplace(cx+(x==0?-1:1),cz+(z==0?-1:1));
    }
    if(!m_redstone.publishing)m_redstone.transactions.push_back(std::move(chunks));
}
namespace {
Redstone::PushReaction pistonReaction(BlockId id) {
    using Reaction=Redstone::PushReaction;
    if(isFluid(id) || id==BlockId::FIRE || id==BlockId::SNOW_LAYER)return Reaction::Destroy;
    if(id==BlockId::CRYING_OBSIDIAN || respawnAnchorCharge(id)>=0)return Reaction::Block;
    if(const auto state=Redstone::blockState(id);state && Redstone::thin(state->kind))return Reaction::Destroy;
    if(id==BlockId::BEDROCK || id==BlockId::OBSIDIAN || NetherPortal::isPortal(id) ||
       id==BlockId::CRAFTING_TABLE || workstationTypeForBlock(id) || blockEntityTypeForBlock(id)) {
        // Circuit blocks have timing state too; only an unextended piston and
        // redstone block may move. Thin attached circuits break at the endpoint.
        const auto state=Redstone::blockState(id);
        if(!state)return Reaction::Block;
        if(Redstone::thin(state->kind))return Reaction::Destroy;
        if(state->kind==Kind::Piston)return state->power?Reaction::Block:Reaction::Move;
        return state->kind==Kind::Block || state->kind==Kind::Lamp || state->kind==Kind::Observer?Reaction::Move:Reaction::Block;
    }
    if(Redstone::native(id) && Redstone::pistonHead(Redstone::nativeSlot(id)))return Reaction::Block;
    ButtonState button;DoorState door;
    if(decodeButton(id,button)||decodeDoor(id,door)||isBed(id)||(getBlockProps(id).shape==RenderShape::Cross || getBlockProps(id).shape==RenderShape::CeilingCross || getBlockProps(id).shape==RenderShape::Spike))return Reaction::Destroy;
    return Reaction::Move;
}
}
bool World::startPiston(Position p,bool extending) {
    if(pistonReserved(Redstone::vector(p)) || m_redstone.motions.size()>=4096)return false;
    const auto id=getLoadedBlock(p.x,p.y,p.z);const auto state=id?Redstone::blockState(*id):std::nullopt;
    if(!state || state->kind!=Kind::Piston)return false;
    if(m_redstone.nextMotion==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Piston transaction identity exhausted");
    Redstone::PistonMotion motion;motion.id=++m_redstone.nextMotion;motion.base=p;motion.piston=*state;motion.extending=extending;
    const auto query=[&](Position q)->std::optional<Redstone::PushCell> {
        if(!dimensionRules(dimension()).contains(q.y)||pistonReserved(Redstone::vector(q)))return {};
        const auto block=getLoadedBlock(q.x,q.y,q.z);if(!block)return {};
        const auto* entity=static_cast<const World*>(this)->getBlockEntity(Redstone::vector(q));
        const auto reaction=entity && entity->type!=BlockEntityType::Redstone?Redstone::PushReaction::Block:pistonReaction(*block);
        return Redstone::PushCell{static_cast<uint16_t>(*block),reaction};
    };
    if(extending) {
        const auto plan=Redstone::planPush(motion.head(),state->facing,query);if(!plan)return false;
        motion.moves=plan->moves;motion.destroyed=plan->destroy;
    } else {
        if(getLoadedBlock(motion.head().x,motion.head().y,motion.head().z)!=motion.headBlock())return false;
        const auto target=motion.head()+Redstone::offset(state->facing);
        if(state->alternate && dimensionRules(dimension()).contains(target.y)) {
            const auto cell=query(target);
            if(!cell)return false;
            if(cell->block && cell->reaction==Redstone::PushReaction::Move)
                motion.moves.push_back({target,motion.head(),cell->block});
        }
    }
    if(!Redstone::validMotion(motion))return false;
    ItemStack destroyed;
    if(motion.destroyed) {
        const auto destroyedPosition=Redstone::vector(*motion.destroyed);const auto destroyedId=getBlock(destroyedPosition.x,destroyedPosition.y,destroyedPosition.z);
        if(!isFluid(destroyedId) && destroyedId!=BlockId::FIRE)destroyed={itemForBlock(destroyedId),1,0};
        DoorState door;std::optional<glm::ivec3> partner;
        if(decodeDoor(destroyedId,door))partner=destroyedPosition+glm::ivec3(0,door.upper?-1:1,0);
        else if(isBed(destroyedId))partner=destroyedPosition+bedPartnerOffset(destroyedId);
        else if(destroyedId==BlockId::SUNFLOWER_BOTTOM || destroyedId==BlockId::SUNFLOWER_TOP)partner=destroyedPosition+glm::ivec3(0,destroyedId==BlockId::SUNFLOWER_TOP?-1:1,0);
        if(partner) {
            const auto other=getLoadedBlock(partner->x,partner->y,partner->z);if(!other)return false;
            if(*other!=BlockId::AIR)motion.additionalDestroy.push_back(Redstone::position(*partner));
        }
    }
    if(!Redstone::validMotion(motion))return false;
    m_redstone.motions.push_back(motion);
    for(const auto& edit:motion.edits())m_redstone.reservations.emplace(edit.first,motion.id);
    // Write ahead of the first voxel change. A partially flushed cross-chunk
    // save is repaired by the same journal overlay in every chunk load path.
    if(m_saveStore)m_saveStore->savePistonMotion(motion);
    applyBlockTransaction(motion.edits());
    if(!destroyed.empty() && gameRules().boolean(GameRuleId::BlockDrops))m_supportDrops.push_back({Redstone::vector(*motion.destroyed),destroyed});
    m_interactionSounds.push_back({Redstone::vector(p),false,extending,false,static_cast<uint8_t>(extending?5:6)});
    return true;
}
bool World::tickPistons(size_t& work) {
    while(m_redstone.motionRemaining && work) {
        --work;--m_redstone.motionRemaining;
        auto& index=m_redstone.motionCursor;
        auto& motion=m_redstone.motions[index];bool loaded=true;
        for(const auto& edit:motion.edits())if(!getLoadedBlock(edit.first.x,edit.first.y,edit.first.z))loaded=false;
        if(!loaded){++index;continue;}
        if(motion.age<2) {
            const auto direction=glm::dvec3(Redstone::vector(Redstone::offset(motion.extending?motion.piston.facing:Redstone::opposite(motion.piston.facing))));
            const double progress=motion.age*.5;
            const auto sweep=[&](Position from,BlockId block) {
                const auto boxes=blockCollisionBoxes(block);
                for(unsigned i=0;i<boxes.count;++i) {
                    const auto minimum=glm::dvec3(Redstone::vector(from))+direction*progress+glm::dvec3(boxes.boxes[i].min);
                    const auto maximum=glm::dvec3(Redstone::vector(from))+direction*progress+glm::dvec3(boxes.boxes[i].max);
                    m_redstone.sweeps.push_back({minimum,maximum,direction*.5});
                }
            };
            for(const auto& move:motion.moves)sweep(move.from,static_cast<BlockId>(move.block));
            sweep(motion.extending?motion.base:motion.head(),motion.headBlock());
            ++motion.age;
            if(m_saveStore)m_saveStore->savePistonMotion(motion);
        }
        if(motion.age==2) {
            const auto edits=motion.edits();applyBlockTransaction(edits);
            if(m_saveStore) {
                std::set<std::pair<int,int>> chunks;
                for(const auto& edit:edits)chunks.emplace(worldToChunkX(edit.first.x),worldToChunkZ(edit.first.z));
                m_chunks.withUnique([&](ChunkStore&){for(const auto& key:chunks){m_persistence.saveOverrides(key.first,key.second);m_persistence.saveBlockEntities(key.first,key.second);}});
            }
            const auto completedId=motion.id;
            for(const auto& edit:edits) {
                m_redstone.reservations.erase(edit.first);
                m_fluids.scheduleAround(Redstone::vector(edit.first));
            }
            m_redstone.motions.erase(m_redstone.motions.begin()+static_cast<std::ptrdiff_t>(index));
            if(m_saveStore)m_saveStore->erasePistonMotion(completedId);
        } else ++index;
    }
    if(m_redstone.motionRemaining)return false;
    m_redstone.motionCursor=0;
    while(m_redstone.requestCursor<m_redstone.pistonRequests.size() && work) {
        --work;const auto request=m_redstone.pistonRequests[m_redstone.requestCursor++];startPiston(request.first,request.second);
    }
    if(m_redstone.requestCursor<m_redstone.pistonRequests.size())return false;
    m_redstone.requestCursor=0;m_redstone.pistonRequests.clear();return true;
}

std::vector<BlockCollisionBox> World::collisionBoxesAt(const glm::ivec3& p) const {
    const auto fixed=blockCollisionBoxes(getBlock(p.x,p.y,p.z));
    std::vector<BlockCollisionBox> result(fixed.boxes.begin(),fixed.boxes.begin()+fixed.count);
    for(const auto& motion:pistonMotions()) {
        const auto direction=glm::dvec3(Redstone::vector(Redstone::offset(motion.extending?motion.piston.facing:Redstone::opposite(motion.piston.facing))));
        const auto add=[&](Position from,BlockId block) {
            const auto offset=glm::dvec3(Redstone::vector(from)-p)+direction*(motion.age*.5);
            const auto boxes=blockCollisionBoxes(block);
            for(unsigned i=0;i<boxes.count;++i) {
                const auto minimum=offset+glm::dvec3(boxes.boxes[i].min),maximum=offset+glm::dvec3(boxes.boxes[i].max);
                if(glm::any(glm::lessThanEqual(maximum,glm::dvec3(0))) || glm::any(glm::greaterThanEqual(minimum,glm::dvec3(1))))continue;
                result.push_back({glm::vec3(glm::max(minimum,glm::dvec3(0))),glm::vec3(glm::min(maximum,glm::dvec3(1)))});
            }
        };
        for(const auto& move:motion.moves)add(move.from,static_cast<BlockId>(move.block));
        add(motion.extending?motion.base:motion.head(),motion.headBlock());
    }
    return result;
}

void World::retireRedstoneChunkUnlocked(int cx,int cz,bool keepWarm) {
    m_redstone.atomicMeshes.erase({cx,cz});
    for(auto it=m_redstone.containerSignals.lower_bound({cx*16,Config::WORLD_MIN_Y,INT_MIN});it!=m_redstone.containerSignals.end() && it->first.x<(cx+1)*16;) {
        if(worldToChunkZ(it->first.z)==cz)it=m_redstone.containerSignals.erase(it);else ++it;
    }
    const auto found=m_redstone.components.find({cx,cz});if(found==m_redstone.components.end())return;
    for(const auto p:found->second) {
        const auto it=m_redstone.nodes.find(p);
        if(it!=m_redstone.nodes.end()) {
            auto& node=it->second;
            if(!node.blocked && node.active) {
                node.suspended=m_redstone.scheduler.eventsFor(p,node.identity);
                m_redstone.scheduler.cancel(p,node.identity);
            }
            node.active=false;
            Redstone::CircuitSnapshot snapshot;snapshot.burnout=node.device.burnout();snapshot.observerPending=node.device.observerPending();
            for(const auto& event:node.suspended)snapshot.events.push_back({event.event.kind,static_cast<uint16_t>(event.remaining),event.event.value});
            m_persistence.setCircuitStateUnlocked(Redstone::vector(p),Redstone::encodeSnapshot(snapshot));
            if(!keepWarm)m_redstone.nodes.erase(it);
        }
        if(!keepWarm){m_redstone.consumers.erase(p);m_redstone.frozenDust.erase(p);m_redstone.observations.erase(p);}
    }
    if(!keepWarm)m_redstone.components.erase(found);
}
