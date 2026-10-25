#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Config.h"
#include "renderer/RenderHandles.h"

struct MeshVertex {
    float px, py, pz;
    float ao, skyLight, blockLight, alpha;
    float u, v;
    float tile;
    float face;
};

static_assert(sizeof(MeshVertex) == 44,
    "MeshVertex must stay 44 bytes: the Vulkan pipeline and shaders encode this layout");

struct ChunkMesh {
    // Mesh construction samples one voxel beyond each horizontal edge for
    // face visibility, smooth lighting, and ambient occlusion.
    inline static constexpr std::array<std::array<int, 2>, 8>
        NEIGHBOR_DEPENDENCY_OFFSETS{{
            {{-1, -1}}, {{0, -1}}, {{1, -1}},
            {{-1,  0}},             {{1,  0}},
            {{-1,  1}}, {{0,  1}}, {{1,  1}}
        }};

    std::vector<MeshVertex> vertices;
    std::vector<unsigned int> indices;

    RenderMeshHandle renderHandle;
    size_t indexCount = 0;
    size_t opaqueIndexCount = 0;
    size_t translucentIndexOffset = 0;
    size_t translucentIndexCount = 0;
    size_t shadowCasterIndexOffset = 0;
    size_t shadowCasterIndexCount = 0;
    bool gpuReady = false;

    void clear() {
        vertices.clear();
        indices.clear();
        indexCount = 0;
        opaqueIndexCount = 0;
        translucentIndexOffset = 0;
        translucentIndexCount = 0;
        shadowCasterIndexOffset = 0;
        shadowCasterIndexCount = 0;
        gpuReady = false;
    }

    void abandonGpuResources() {
        renderHandle = {};
        gpuReady = false;
    }

    bool empty() const {
        return vertices.empty() || indices.empty();
    }

    size_t uploadBytes() const {
        return vertices.size() * sizeof(MeshVertex) +
               indices.size() * sizeof(unsigned int);
    }

    // Replace only the CPU-side geometry with a completed worker mesh.
    // GPU ownership remains in Renderer while worker-produced CPU geometry is
    // transferred into the active chunk mesh.
    void adoptCpuGeometry(ChunkMesh& completed) {
        using std::swap;
        swap(vertices, completed.vertices);
        swap(indices, completed.indices);
        swap(indexCount, completed.indexCount);
        swap(opaqueIndexCount, completed.opaqueIndexCount);
        swap(translucentIndexOffset, completed.translucentIndexOffset);
        swap(translucentIndexCount, completed.translucentIndexCount);
        swap(shadowCasterIndexOffset, completed.shadowCasterIndexOffset);
        swap(shadowCasterIndexCount, completed.shadowCasterIndexCount);
    }

    // Greedy mesh construction lives in world/ChunkMeshBuilder.h so data-only
    // consumers do not parse block, light and fluid algorithms. Include that
    // header wherever build() is called.
    template<typename NeighborFunc, typename LightFunc>
    void build(int chunkWorldX, int chunkWorldZ,
               const uint16_t* blocks,
               const int columnMaxY[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z],
               NeighborFunc&& getNeighbor, LightFunc&& getLight);

    void upload();
    void destroy();
};
