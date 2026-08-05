#include "core/AssetStore.h"

bool AssetStore::validPath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    if (path.size() > 1 && path[1] == ':') return false;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string_view part = path.substr(
            start, slash == std::string_view::npos ? path.size() - start : slash - start);
        if (part.empty() || part == "." || part == ".." ||
            part.find('\\') != std::string_view::npos)
            return false;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}
