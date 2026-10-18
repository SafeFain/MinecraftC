#pragma once
#include "minecraftc/extensions.h"
#include <array>
namespace OfficialHeaven {
inline constexpr int MAX_SKY_MOTES_PER_UPDATE=8,MAX_POLLEN_PER_UPDATE=4,MAX_SPARKLE_PER_UPDATE=6;
std::array<MC_ParticleDefinitionV2,3> particleDefinitions();
MC_AmbientDefinition ambientDefinition();
}
