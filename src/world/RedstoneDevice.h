#pragma once

#include "world/RedstoneScheduler.h"

namespace Redstone {
struct Inputs {
    uint8_t rear=0,side=0;
    bool locked=false;
    bool observedChange=false;
};

// Component timing is independent of World. The owning adapter samples inputs,
// validates event incarnation, and publishes returned output/state changes only
// after the combinational network has settled.
class Device {
public:
    explicit Device(State state):m_state(state) {
        if(!state.valid())throw std::invalid_argument("Invalid redstone device");
    }
    const State& state() const {return m_state;}
    const TorchBurnout& burnout() const {return m_burnout;}
    void restoreBurnout(TorchBurnout value) {
        if(!value.valid())throw std::invalid_argument("Invalid torch burnout state");
        m_burnout=value;
    }
    void activeTick() {m_burnout.advance();}
    bool input(Inputs inputs,Position p,uint64_t identity,RedstoneScheduler& queue) {
        if(inputs.rear>15 || inputs.side>15)throw std::invalid_argument("Invalid redstone input");
        m_inputs=inputs;
        const auto before=encode(m_state);
        switch(m_state.kind) {
            case Kind::Torch:
                if(m_burnout.cooldown==0 && m_state.power!=torchTarget())
                    queue.schedule(p,identity,EventKind::Output,torchTarget(),2);
                break;
            case Kind::Repeater:
                m_state.locked=inputs.locked;
                if(!m_state.locked && m_state.power!=(inputs.rear?15:0))
                    queue.schedule(p,identity,EventKind::Output,inputs.rear?15:0,2*m_state.delay);
                break;
            case Kind::Comparator:
                if(m_state.power!=comparator(inputs.rear,inputs.side,m_state.alternate))
                    queue.schedule(p,identity,EventKind::Output,0,2);
                break;
            case Kind::Observer:
                if(inputs.observedChange && !m_observerPending && !m_state.power) {
                    m_observerPending=true;
                    queue.schedule(p,identity,EventKind::Output,15,2);
                }
                break;
            case Kind::Lamp:
                if(inputs.rear) {queue.cancel(p,identity);m_state.power=15;}
                else if(m_state.power)queue.schedule(p,identity,EventKind::LampOff,0,4);
                break;
            default: break;
        }
        return before!=encode(m_state);
    }
    bool apply(const Event& event,RedstoneScheduler& queue) {
        const auto before=encode(m_state);
        switch(m_state.kind) {
            case Kind::Torch: {
                if(m_burnout.cooldown)break;
                const auto target=torchTarget();
                if(m_state.power && !target && m_burnout.recordOff())
                    queue.schedule(event.position,event.identity,EventKind::Retry,0,160);
                m_state.power=target;
                break;
            }
            case Kind::Repeater:
                if(m_state.locked)break;
                // A scheduled rising edge survives a short on-pulse. A falling
                // edge must recheck the input so a short off-pulse is filtered.
                if(event.value || !m_inputs.rear)m_state.power=event.value;
                // Preserve the scheduled rising edge even if its input already
                // fell. The following falling edge gets a full configured delay.
                input(m_inputs,event.position,event.identity,queue);
                break;
            case Kind::Comparator:
                m_state.power=comparator(m_inputs.rear,m_inputs.side,m_state.alternate);
                break;
            case Kind::Observer:
                if(event.kind==EventKind::ObserverOff) {m_state.power=0;m_observerPending=false;}
                else {
                    m_state.power=15;
                    queue.schedule(event.position,event.identity,EventKind::ObserverOff,0,2);
                }
                break;
            case Kind::Lamp:
                if(!m_inputs.rear)m_state.power=0;
                break;
            default: break;
        }
        return before!=encode(m_state);
    }
    bool observerPending() const {return m_observerPending;}
    void restoreObserverPending(bool value) {m_observerPending=value;}
private:
    uint8_t torchTarget() const {return static_cast<uint8_t>(m_inputs.rear || m_burnout.cooldown?0:15);}
    State m_state;
    Inputs m_inputs;
    TorchBurnout m_burnout;
    bool m_observerPending=false;
};
} // namespace Redstone
