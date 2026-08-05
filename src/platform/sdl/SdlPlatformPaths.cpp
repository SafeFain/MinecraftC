#include "platform/sdl/SdlPlatformPaths.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <stdexcept>

namespace platform::sdl {
std::filesystem::path preferencePath() {
    char* preferred = SDL_GetPrefPath("SafeFain", "MinecraftC");
    if (!preferred)
        throw std::runtime_error("Cannot determine application data directory");
    const std::filesystem::path result = std::filesystem::u8path(preferred);
    SDL_free(preferred);
    return result;
}
} // namespace platform::sdl

