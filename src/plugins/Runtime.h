#pragma once
#include "minecraftc/plugin.h"
#include <functional>
#include <string>

namespace Plugins {
struct ActorContext {uint64_t player=0;uint32_t dimension=0,role=MC_LOCAL;};
inline std::function<ActorContext()>& contextProvider(){static std::function<ActorContext()> value;return value;}
inline const ActorContext*& scopedContext(){static thread_local const ActorContext* value=nullptr;return value;}
inline ActorContext context(){return scopedContext()?*scopedContext():contextProvider()?contextProvider()():ActorContext{};}
class ActorScope {
    ActorContext value;
    const ActorContext* previous;
public:
    explicit ActorScope(ActorContext actor):value(actor),previous(scopedContext()){scopedContext()=&value;}
    ~ActorScope(){scopedContext()=previous;}
    ActorScope(const ActorScope&)=delete;
    ActorScope& operator=(const ActorScope&)=delete;
};
using Dispatcher=std::function<void(MC_Event&)>;
inline Dispatcher& dispatcher() { static Dispatcher value; return value; }
inline bool dispatch(MC_Event& event) {
    event.size=sizeof(event);
    if(dispatcher())dispatcher()(event);
    return !event.cancelled;
}
inline MC_Event event(MC_EventKind kind) {MC_Event value{};value.size=sizeof(value);value.kind=kind;const auto actor=context();value.player_id=actor.player;value.dimension=actor.dimension;value.role=actor.role;return value;}
inline std::function<bool(const std::string&)>& commandDispatcher() {static std::function<bool(const std::string&)> value;return value;}
}
