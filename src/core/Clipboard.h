#pragma once

#include <string>
#include <string_view>

namespace platform {

class Clipboard {
public:
    virtual ~Clipboard() = default;
    virtual bool writeText(std::string_view text) = 0;
    virtual bool readText(std::string& text) = 0;
};

} // namespace platform

