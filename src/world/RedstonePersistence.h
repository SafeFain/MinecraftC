#pragma once
#include "world/RedstoneDevice.h"

namespace Redstone {
struct CircuitSnapshot {
    TorchBurnout burnout;
    bool observerPending=false;
    struct Delayed {EventKind kind;uint16_t remaining;uint8_t value;};
    std::vector<Delayed> events;
};
inline std::vector<uint8_t> encodeSnapshot(const CircuitSnapshot& state) {
    if(!state.burnout.valid() || state.events.size()>4)throw std::invalid_argument("Invalid circuit snapshot");
    std::vector<uint8_t> bytes{1,state.burnout.count};
    for(size_t i=0;i<state.burnout.count;++i)bytes.push_back(state.burnout.ages[i]);
    bytes.push_back(static_cast<uint8_t>(state.burnout.cooldown));
    bytes.push_back(static_cast<uint8_t>(state.burnout.cooldown>>8));
    bytes.push_back(state.observerPending?1:0);bytes.push_back(static_cast<uint8_t>(state.events.size()));
    std::set<EventKind> kinds;
    for(const auto& event:state.events) {
        if(event.kind>EventKind::Retry || event.remaining>160 || event.value>15 || !kinds.insert(event.kind).second)
            throw std::invalid_argument("Invalid circuit delayed event");
        bytes.push_back(static_cast<uint8_t>(event.kind));bytes.push_back(static_cast<uint8_t>(event.remaining));
        bytes.push_back(static_cast<uint8_t>(event.remaining>>8));bytes.push_back(event.value);
    }
    return bytes;
}
inline CircuitSnapshot decodeSnapshot(const std::vector<uint8_t>& bytes) {
    CircuitSnapshot state;if(bytes.empty())return state;
    if(bytes.size()>64)throw std::invalid_argument("Circuit snapshot exceeds bounds");
    size_t cursor=0;
    auto read=[&](){if(cursor>=bytes.size())throw std::invalid_argument("Truncated circuit snapshot");return bytes[cursor++];};
    if(read()!=1)throw std::invalid_argument("Unsupported circuit snapshot version");
    state.burnout.count=read();if(state.burnout.count>8)throw std::invalid_argument("Invalid burnout history");
    for(size_t i=0;i<state.burnout.count;++i)state.burnout.ages[i]=read();
    const auto low=read();state.burnout.cooldown=static_cast<uint16_t>(low|(uint16_t(read())<<8));
    const auto pending=read();if(pending>1)throw std::invalid_argument("Invalid circuit flag");
    state.observerPending=pending!=0;const auto count=read();
    if(count>4)throw std::invalid_argument("Too many circuit events");
    for(size_t i=0;i<count;++i) {
        const auto kind=static_cast<EventKind>(read());const auto a=read();
        const auto remaining=static_cast<uint16_t>(a|(uint16_t(read())<<8));const auto value=read();
        state.events.push_back({kind,remaining,value});
    }
    if(cursor!=bytes.size())throw std::invalid_argument("Trailing circuit snapshot data");
    encodeSnapshot(state);return state;
}
} // namespace Redstone
