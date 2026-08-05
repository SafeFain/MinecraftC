#pragma once

#include <cstdint>

struct RenderMeshHandle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
    friend bool operator==(RenderMeshHandle a, RenderMeshHandle b) {
        return a.value == b.value;
    }
};

struct RenderTextureHandle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
};
