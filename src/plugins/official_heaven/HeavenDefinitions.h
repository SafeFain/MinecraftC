#pragma once
#include <filesystem>
#include "minecraftc/plugin.h"
namespace OfficialHeaven {
void loadDefinitions(const std::filesystem::path& package);
int32_t registerAssets(const MC_Host*,MC_PluginContext*,const std::filesystem::path&);
void loadAssetsFixture(const std::filesystem::path&);
}
