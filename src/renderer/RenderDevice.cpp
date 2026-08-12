#include "renderer/RenderDevice.h"

#include <limits>
#include <stdexcept>

void validateMeshData(const MeshData& data) {
    const size_t vertexCount = data.layout == MeshVertexLayout::Chunk
        ? data.chunkVertices.size() : data.vertices.size();
    if (vertexCount == 0)
        throw std::invalid_argument("Mesh requires at least one vertex");
    if (data.indices.empty() || data.indices.size() % 3 != 0)
        throw std::invalid_argument("Triangle mesh indices must be non-empty triples");
    for (uint32_t index : data.indices) {
        if (index >= vertexCount)
            throw std::invalid_argument("Mesh index is outside the vertex array");
    }
    const auto validRange = [&](uint32_t offset, uint32_t count) {
        return static_cast<uint64_t>(offset) + count <= data.indices.size();
    };
    if (!validRange(data.opaqueIndexOffset, data.opaqueIndexCount) ||
        !validRange(data.translucentIndexOffset, data.translucentIndexCount) ||
        !validRange(data.shadowCasterIndexOffset, data.shadowCasterIndexCount))
        throw std::invalid_argument("Mesh index range is outside the index array");
}

void validateTextureData(const TextureData& data) {
    if (data.width == 0 || data.height == 0)
        throw std::invalid_argument("Texture dimensions must be non-zero");
    const uint64_t bytes = static_cast<uint64_t>(data.width) * data.height * 4u;
    if (bytes > std::numeric_limits<size_t>::max() || data.pixels.size() != bytes)
        throw std::invalid_argument("RGBA texture byte count does not match dimensions");
    uint32_t expectedWidth = data.width;
    uint32_t expectedHeight = data.height;
    for (const auto& level : data.mipLevels) {
        expectedWidth = std::max(1u, expectedWidth / 2u);
        expectedHeight = std::max(1u, expectedHeight / 2u);
        const uint64_t levelBytes = static_cast<uint64_t>(level.width) * level.height * 4u;
        if (level.width != expectedWidth || level.height != expectedHeight ||
            level.pixels.size() != levelBytes)
            throw std::invalid_argument("Texture mip dimensions or byte count are invalid");
    }
}

bool isMeshMaterialCompatible(MeshVertexLayout layout,
                              MaterialPipeline pipeline) {
    if (layout == MeshVertexLayout::PositionUv)
        return pipeline == MaterialPipeline::UnlitTextured;
    return pipeline == MaterialPipeline::ChunkOpaqueCutout ||
           pipeline == MaterialPipeline::ChunkTranslucent;
}

glm::mat4 clipSpaceCorrection(GraphicsApi api) {
    glm::mat4 correction{1.0f};
    if (api == GraphicsApi::Vulkan) {
        correction[1][1] = -1.0f;
        correction[2][2] = 0.5f;
        correction[3][2] = 0.5f;
    }
    return correction;
}
