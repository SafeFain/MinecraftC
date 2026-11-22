#include <cstdint>
#include <filesystem>
#include <string>

import minecraftc.core.asset_store;
import minecraftc.core.runtime_clock;
import minecraftc.core.utf8_path;
import minecraftc.core.window;
import minecraftc.world.gen_context;

bool verifyHeaderConsumer(WorldGenContext context, const RuntimeClock& clock,
                          std::uint64_t expectedHash);
bool verifyCoreHeaderConsumer(const AssetStore* assets, const Window* window,
                              WindowSafeArea safeArea, WindowTextInputArea textArea,
                              const std::filesystem::path& path,
                              const std::string& nativeUtf8, const std::string& genericUtf8);

int main() {
    const WorldGenContext context{42};
    const RuntimeClock clock{[]() -> std::uint64_t { return 7; }};
    const auto hash = WorldGenContext::hashPosition(context.seed(), -4, 2, -8);

    const auto safeArea = projectWindowSafeArea(10, 50, 50, 20, 100, 100, 200, 200);
    const auto textArea = projectTextInputArea(safeArea, 100, 100, 200, 200);
    const std::filesystem::path path{u8"\u4e16\u754c/\u00e9.txt"};
    const auto genericUtf8 = genericPathToUtf8(path);
    const std::string expectedUtf8 = "\xe4\xb8\x96\xe7\x95\x8c/\xc3\xa9.txt";

    return verifyHeaderConsumer(context, clock, hash) &&
           genericUtf8 == expectedUtf8 &&
           verifyCoreHeaderConsumer(nullptr, nullptr, safeArea, textArea, path,
                                    pathToUtf8(path), genericUtf8) ? 0 : 1;
}
