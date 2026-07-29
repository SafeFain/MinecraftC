#pragma once

#include "model/ModelAsset.h"

#include <algorithm>
#include <vector>

namespace model {

enum class ModelPass { Opaque, Blend };

inline ModelPass modelPass(AlphaMode mode) {
    return mode == AlphaMode::Blend ? ModelPass::Blend : ModelPass::Opaque;
}

struct BlendSortEntry { float distanceSquared = 0.0f; };

template<typename Draw>
void sortBlended(std::vector<Draw>& draws) {
    std::stable_sort(draws.begin(), draws.end(),
        [](const Draw& a, const Draw& b) {
            return a.distanceSquared > b.distanceSquared;
        });
}

} // namespace model
