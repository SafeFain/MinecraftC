#pragma once
#include <filesystem>
#include <memory>

namespace Platform {
class DynamicLibrary {
public:
    explicit DynamicLibrary(const std::filesystem::path& path);
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    void* symbol(const char* name) const;
    static bool supported();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
