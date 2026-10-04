#pragma once
#include "minecraftc/plugin.h"
#include <functional>
#include <string>

namespace Plugins {
using Dispatcher=std::function<void(MC_Event&)>;
inline Dispatcher& dispatcher() { static Dispatcher value; return value; }
inline bool dispatch(MC_Event& event) {
    event.size=sizeof(event);
    if(dispatcher())dispatcher()(event);
    return !event.cancelled;
}
inline MC_Event event(MC_EventKind kind) {MC_Event value{};value.size=sizeof(value);value.kind=kind;return value;}
inline std::function<bool(const std::string&)>& commandDispatcher() {static std::function<bool(const std::string&)> value;return value;}
}
