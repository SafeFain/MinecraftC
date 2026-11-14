#pragma once

#include "world/RedstoneRules.h"
#include <map>
#include <set>
#include <limits>

namespace Redstone {
enum class EventKind : uint8_t { Output, ObserverOff, LampOff, Retry };
struct Event {
    uint64_t due = 0;
    Position position;
    uint64_t identity = 0;
    EventKind kind = EventKind::Output;
    uint8_t value = 0;
    uint64_t sequence = 0;
};
struct EventOrder {
    bool operator()(const Event& a, const Event& b) const {
        return std::tie(a.due,a.kind,a.position,a.sequence)<std::tie(b.due,b.kind,b.position,b.sequence);
    }
};
struct EventKey {
    Position position;
    uint64_t identity;
    EventKind kind;
    friend bool operator<(const EventKey& a,const EventKey& b) {
        return std::tie(a.position,a.identity,a.kind)<std::tie(b.position,b.identity,b.kind);
    }
};

// A scheduled event owns its component incarnation, not merely its coordinate.
// Removing and replacing a component cannot revive the removed component's pulse.
class RedstoneScheduler {
public:
    uint64_t tick() const { return m_tick; }
    size_t size() const { return m_events.size(); }
    bool pendingTick() const { return m_inTick; }
    void schedule(Position p,uint64_t identity,EventKind kind,uint8_t value,uint64_t delay) {
        if(!identity || kind>EventKind::Retry || value>15 || delay>std::numeric_limits<uint64_t>::max()-m_tick)
            throw std::invalid_argument("Invalid redstone event");
        EventKey key{p,identity,kind};
        // First scheduled transition wins. In particular, a short input pulse
        // must not replace an already scheduled repeater rising edge.
        if(m_keys.count(key)) return;
        if(m_sequence==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Redstone sequence overflow");
        Event e{m_tick+delay,p,identity,kind,value,++m_sequence};
        m_events.insert(e); m_keys.emplace(key,e);
    }
    void cancel(Position p,uint64_t identity) {
        auto it=m_keys.lower_bound({p,identity,EventKind::Output});
        while(it!=m_keys.end() && it->first.position==p && it->first.identity==identity) {
            m_events.erase(it->second); it=m_keys.erase(it);
        }
    }
    // valid checks incarnation identity only. World must pause unavailable
    // chunks before advancing, rather than report their events as stale.
    template<class Valid,class Apply>
    bool advance(size_t budget,Valid&& valid,Apply&& apply) {
        if(!m_inTick) {
            if(m_tick==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Redstone tick overflow");
            ++m_tick; m_inTick=true;
        }
        while(!m_events.empty() && m_events.begin()->due<=m_tick && budget) {
            const auto e=*m_events.begin();
            m_events.erase(m_events.begin()); m_keys.erase({e.position,e.identity,e.kind});
            --budget;
            if(valid(e.position,e.identity)) apply(e);
        }
        if(!m_events.empty() && m_events.begin()->due<=m_tick) return false;
        m_inTick=false; return true;
    }
    struct Paused { uint64_t remaining; Event event; };
    std::vector<Paused> eventsFor(Position p,uint64_t identity) const {
        std::vector<Paused> result;
        auto it=m_keys.lower_bound({p,identity,EventKind::Output});
        while(it!=m_keys.end() && it->first.position==p && it->first.identity==identity) {
            const auto& event=it->second;
            result.push_back({event.due>m_tick?event.due-m_tick:0,event});++it;
        }
        return result;
    }
    template<class Contains>
    std::vector<Paused> pause(Contains&& contains) {
        std::vector<Paused> result;
        for(auto it=m_events.begin();it!=m_events.end();) {
            if(!contains(it->position)) {++it;continue;}
            const auto event=*it;
            result.push_back({event.due>m_tick?event.due-m_tick:0,event});
            m_keys.erase({event.position,event.identity,event.kind}); it=m_events.erase(it);
        }
        return result;
    }
    void resume(const std::vector<Paused>& events) {
        for(const auto& e:events) schedule(e.event.position,e.event.identity,e.event.kind,e.event.value,e.remaining);
    }
private:
    uint64_t m_tick=0,m_sequence=0;
    bool m_inTick=false;
    std::set<Event,EventOrder> m_events;
    std::map<EventKey,Event> m_keys;
};

// Recompute dirty connected wire regions from external sources, never from old
// wire power. Thus rings cannot retain a signal after the last source disappears.
// Caller stages results and publishes only after complete() becomes true.
class DustPropagation {
public:
    struct Wire { uint8_t source=0; std::vector<Position> neighbors; };
    explicit DustPropagation(std::map<Position,Wire> wires):m_wires(std::move(wires)) {}
    bool step(size_t budget) {return stepBudget(budget);}
    bool stepBudget(size_t& budget) {
        if(!m_initialized) {
            auto it=m_cursor?m_wires.upper_bound(*m_cursor):m_wires.begin();
            while(it!=m_wires.end() && budget) {
                --budget;const auto& entry=*it;m_cursor=entry.first;
                if(entry.second.source>15)throw std::invalid_argument("Invalid wire source");
                m_result[entry.first]=0;
                if(entry.second.source)m_frontier.emplace(entry.first,entry.second.source);
                ++it;
            }
            if(it!=m_wires.end())return false;
            m_initialized=true;m_cursor.reset();
        }
        while(budget && !m_frontier.empty()) {
            --budget;
            const auto entry=*m_frontier.begin(); m_frontier.erase(m_frontier.begin());
            auto& current=m_result.at(entry.first);
            if(current>=entry.second) continue;
            current=entry.second;
            if(current<=1) continue;
            for(const auto p:m_wires.at(entry.first).neighbors) {
                if(!m_wires.count(p) || m_result.at(p)>=current-1) continue;
                auto& candidate=m_frontier[p]; candidate=std::max<uint8_t>(candidate,current-1);
            }
        }
        return complete();
    }
    bool complete() const { return m_initialized && m_frontier.empty(); }
    const std::map<Position,uint8_t>& result() const {
        if(!complete()) throw std::logic_error("Wire propagation incomplete");
        return m_result;
    }
private:
    bool m_initialized=false;
    std::optional<Position> m_cursor;
    std::map<Position,Wire> m_wires;
    std::map<Position,uint8_t> m_result,m_frontier;
};
} // namespace Redstone
