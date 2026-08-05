#include "platform/sdl/SdlClipboard.h"

#include "debug/Log.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

namespace platform::sdl {

bool SdlClipboard::writeText(std::string_view text) {
    const std::string copy(text);
    if (SDL_SetClipboardText(copy.c_str())) return true;
    LOG_WARN("Could not write SDL clipboard: " << SDL_GetError());
    return false;
}

bool SdlClipboard::readText(std::string& text) {
    char* value = SDL_GetClipboardText();
    if (!value) {
        LOG_WARN("Could not read SDL clipboard: " << SDL_GetError());
        return false;
    }
    text.assign(value);
    SDL_free(value);
    return true;
}

} // namespace platform::sdl

