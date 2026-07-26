#include "game/WorldCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("minecraftc-catalog-test-" + std::to_string(
            static_cast<unsigned long long>(std::rand())));
    std::filesystem::remove_all(root);
    try {
        WorldCatalog catalog(root);
        const std::string first = catalog.create(
            "My Survival World", 42, GameMode::Survival, Difficulty::Normal, true);
        const std::string second = catalog.create(
            "My Survival World", 99, GameMode::Creative, Difficulty::Peaceful);
        require(first == "my-survival-world", "display name creates a safe id");
        require(second == "my-survival-world-2", "duplicate names get unique ids");
        const auto worlds = catalog.list();
        require(worlds.size() == 2, "catalog lists multiple saves");
        require(catalog.open(first).loadMetadata().seed == 42,
                "catalog opens the selected world");
        require(catalog.open(first).loadMetadata().cheatsEnabled,
                "catalog persists the create-world cheat option");
        bool rejected = false;
        try {
            (void)catalog.open("../escape");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "catalog rejects path traversal ids");
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
    std::cout << "World catalog tests passed\n";
    return 0;
}
