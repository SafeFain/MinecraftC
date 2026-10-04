#include "core/DynamicLibrary.h"
#include <stdexcept>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#include <dlfcn.h>
#endif

namespace Platform {
struct DynamicLibrary::Impl {
#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
};
bool DynamicLibrary::supported() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    return false;
#else
    return true;
#endif
}
DynamicLibrary::DynamicLibrary(const std::filesystem::path& path) : m_impl(std::make_unique<Impl>()) {
#if defined(_WIN32)
    m_impl->handle=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!m_impl->handle) throw std::runtime_error("Cannot load native plugin: " + path.u8string());
#elif !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    m_impl->handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!m_impl->handle) {const char* error=dlerror();throw std::runtime_error(error?error:"Cannot load native plugin");}
#else
    (void)path;throw std::runtime_error("External native plugins are unsupported on this platform");
#endif
}
DynamicLibrary::~DynamicLibrary() {
#if defined(_WIN32)
    if(m_impl->handle) FreeLibrary(m_impl->handle);
#elif !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    if(m_impl->handle) dlclose(m_impl->handle);
#endif
}
void* DynamicLibrary::symbol(const char* name) const {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(m_impl->handle,name));
#elif !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    return dlsym(m_impl->handle,name);
#else
    (void)name;return nullptr;
#endif
}
}
