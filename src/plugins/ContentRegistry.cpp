#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"

namespace Plugins {
namespace {
ContentState& registry() {
    static ContentState state;
    return state;
}
}

const ContentState& content() { return registry(); }
ContentState& mutableContentState() { return registry(); }
}
