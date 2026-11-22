#include "core/AssetStore.h"
#include "core/RuntimeClock.h"
#include "core/Utf8Path.h"
#include "core/Window.h"
#include "world/WorldGenContext.h"

#include <cstdint>
#include <filesystem>
#include <string>

bool verifyHeaderConsumer(WorldGenContext context, const RuntimeClock& clock,
                          std::uint64_t expectedHash) {
    return context.seed() == 42 && clock.now() == 7 &&
           WorldGenContext::hashPosition(context.seed(), -4, 2, -8) == expectedHash;
}

bool verifyCoreHeaderConsumer(const AssetStore* assets, const Window* window,
                              WindowSafeArea safeArea, WindowTextInputArea textArea,
                              const std::filesystem::path& path,
                              const std::string& nativeUtf8, const std::string& genericUtf8) {
    const auto headerArea = projectTextInputArea(safeArea, 100, 100, 200, 200);
    return assets == nullptr && window == nullptr &&
           safeArea.x == 20 && safeArea.y == 60 && safeArea.width == 100 && safeArea.height == 40 &&
           textArea.x == 10 && textArea.y == 50 && textArea.width == 50 && textArea.height == 20 &&
           textArea.x == headerArea.x && textArea.y == headerArea.y &&
           textArea.width == headerArea.width && textArea.height == headerArea.height &&
           pathToUtf8(path) == nativeUtf8 && genericPathToUtf8(path) == genericUtf8;
}
