#pragma once

#include "core/GraphicsApi.h"

#include <string>

inline std::string shaderSourceForApi(std::string source, GraphicsApi api) {
    if (api == GraphicsApi::OpenGL33) return source;
    constexpr const char* header =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    const size_t newline = source.find('\n');
    if (source.rfind("#version", 0) == 0)
        source.erase(0, newline == std::string::npos ? source.size() : newline + 1);
    return std::string(header) + source;
}
