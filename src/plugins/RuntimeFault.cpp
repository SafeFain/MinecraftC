#include "plugins/Runtime.h"

#include <utility>

namespace Plugins {
namespace {
// Worker generation errors are queued separately and drained by PluginManager.
std::string fault;
}

const std::string& runtimeFault() { return fault; }
void setRuntimeFault(std::string message) { fault = std::move(message); }
}
