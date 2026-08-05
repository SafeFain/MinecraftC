#pragma once

#include "platform/Clipboard.h"

namespace platform::sdl {

class SdlClipboard final : public Clipboard {
public:
    bool writeText(std::string_view text) override;
    bool readText(std::string& text) override;
};

} // namespace platform::sdl

