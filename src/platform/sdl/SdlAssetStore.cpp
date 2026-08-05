#include "core/AssetStore.h"
#include "core/RuntimeClock.h"

#include <SDL3/SDL.h>

#include <limits>
#include <stdexcept>

struct AssetStore::Impl {
    SDL_Storage* storage = nullptr;
};

const AssetStore* AssetStore::s_current = nullptr;

AssetStore::AssetStore(const std::filesystem::path& root, uint64_t readyTimeoutMs)
    : m_impl(std::make_unique<Impl>()) {
    if (!root.empty()) m_root = std::filesystem::weakly_canonical(root);
    const std::string native = root.u8string();
    m_impl->storage = SDL_OpenTitleStorage(native.empty() ? nullptr : native.c_str(), 0);
    if (!m_impl->storage)
        throw std::runtime_error("Could not open title storage: " +
                                 std::string(SDL_GetError()));
    RuntimeClock clock;
    const uint64_t started = clock.now();
    while (!SDL_StorageReady(m_impl->storage) && RuntimeClock::milliseconds(
            RuntimeClock::elapsed(started, clock.now())) < readyTimeoutMs) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    if (!SDL_StorageReady(m_impl->storage)) {
        SDL_CloseStorage(m_impl->storage);
        m_impl->storage = nullptr;
        throw std::runtime_error("Title storage did not become ready within timeout");
    }
    s_current = this;
}

AssetStore::~AssetStore() {
    if (s_current == this) s_current = nullptr;
    if (m_impl && m_impl->storage) SDL_CloseStorage(m_impl->storage);
}

std::vector<uint8_t> AssetStore::readBinary(std::string_view relative) const {
    if (!validPath(relative)) throw std::invalid_argument("Invalid asset path");
    const std::string path(relative);
    Uint64 length = 0;
    if (!SDL_GetStorageFileSize(m_impl->storage, path.c_str(), &length))
        throw std::runtime_error("Could not query asset '" + path + "': " +
                                 SDL_GetError());
    if (length > static_cast<Uint64>(std::numeric_limits<size_t>::max()))
        throw std::runtime_error("Asset is too large: " + path);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (length && !SDL_ReadStorageFile(
            m_impl->storage, path.c_str(), bytes.data(), length))
        throw std::runtime_error("Could not read asset '" + path + "': " +
                                 SDL_GetError());
    return bytes;
}

std::string AssetStore::readText(std::string_view relative) const {
    const auto bytes = readBinary(relative);
    return std::string(bytes.begin(), bytes.end());
}

std::vector<uint8_t> AssetStore::readPath(const std::filesystem::path& path) {
    if (s_current) {
        if (s_current->m_root.empty() && path.is_relative())
            return s_current->readBinary(path.generic_u8string());
        std::error_code error;
        const auto relative = std::filesystem::relative(path, s_current->m_root, error);
        if (!error) return s_current->readBinary(relative.generic_u8string());
    }
    SDL_IOStream* stream = SDL_IOFromFile(path.u8string().c_str(), "rb");
    if (!stream) throw std::runtime_error("Could not open asset: " + path.u8string());
    const Sint64 size = SDL_GetIOSize(stream);
    if (size < 0) {
        SDL_CloseIO(stream);
        throw std::runtime_error("Could not size asset: " + path.u8string());
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size && SDL_ReadIO(stream, bytes.data(), bytes.size()) != bytes.size()) {
        SDL_CloseIO(stream);
        throw std::runtime_error("Could not read asset: " + path.u8string());
    }
    SDL_CloseIO(stream);
    return bytes;
}

std::string AssetStore::readTextPath(const std::filesystem::path& path) {
    const auto bytes = readPath(path);
    return {bytes.begin(), bytes.end()};
}

