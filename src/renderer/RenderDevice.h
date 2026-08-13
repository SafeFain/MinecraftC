#pragma once

#include "renderer/RenderHandles.h"
#include "core/GraphicsApi.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include "world/ChunkMesh.h"

struct BasicMeshVertex {
    glm::vec3 position{0.0f};
    glm::vec2 uv{0.0f};
};
struct UiMeshVertex {
    glm::vec2 position{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
};

enum class MeshVertexLayout { PositionUv, Chunk };

struct MeshData {
    MeshVertexLayout layout = MeshVertexLayout::PositionUv;
    std::vector<BasicMeshVertex> vertices;
    std::vector<MeshVertex> chunkVertices;
    std::vector<uint32_t> indices;
    uint32_t opaqueIndexOffset = 0;
    uint32_t opaqueIndexCount = 0;
    uint32_t translucentIndexOffset = 0;
    uint32_t translucentIndexCount = 0;
    uint32_t shadowCasterIndexOffset = 0;
    uint32_t shadowCasterIndexCount = 0;
};

enum class TextureFormat {
    Rgba8Srgb
};

enum class TextureFilter {
    Nearest,
    Linear,
    NearestMipmapLinear
};

enum class TextureAddressMode {
    Repeat,
    ClampToEdge
};

struct TextureData {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::vector<uint8_t> pixels;
    struct MipLevel {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
    };
    std::vector<MipLevel> mipLevels;
};

struct TextureSamplerDesc {
    TextureFilter minFilter = TextureFilter::Nearest;
    TextureFilter magFilter = TextureFilter::Nearest;
    TextureAddressMode addressU = TextureAddressMode::Repeat;
    TextureAddressMode addressV = TextureAddressMode::Repeat;
};

enum class MaterialPipeline {
    UnlitTextured,
    ChunkOpaqueCutout,
    ChunkTranslucent,
    UiTextured
};

struct MaterialDesc {
    MaterialPipeline pipeline = MaterialPipeline::UnlitTextured;
    RenderTextureHandle baseColorTexture{};
    bool depthTest = true;
    bool backfaceCull = true;
    uint32_t atlasTilesPerSide = 1;
    float alphaCutoff = 0.1f;
    bool smoothLighting = true;
};

struct FrameData {
    glm::vec4 clearColor{0.055f, 0.065f, 0.08f, 1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 ambientColor{0.72f, 0.76f, 0.82f};
    glm::vec3 directColor{1.0f, 0.96f, 0.86f};
    glm::vec3 lightDirection{-0.35f, -0.8f, -0.25f};
};

struct DrawCommand {
    RenderMeshHandle mesh{};
    RenderMaterialHandle material{};
    glm::mat4 model{1.0f};
    glm::vec4 tint{1.0f};
    glm::mat4 viewProjection{1.0f};
    bool useCustomViewProjection = false;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct RenderDeviceCapabilities {
    bool texturedMesh = false;
    bool gameplay = false;
    bool chunkOpaqueCutout = false;
};

struct RendererPerformanceStats {
    double cpuWaitMs = 0.0;
    double cpuPrepareMs = 0.0;
    double cpuRecordMs = 0.0;
    double cpuSubmitMs = 0.0;
    uint64_t uploadBytes = 0;
    uint32_t drawCalls = 0;
    uint32_t pipelineBinds = 0;
    uint32_t descriptorBinds = 0;
    uint32_t vertexBufferBinds = 0;
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    virtual RenderDeviceCapabilities capabilities() const = 0;
    virtual RenderMeshHandle createMesh(const MeshData& data) = 0;
    virtual void destroyMesh(RenderMeshHandle handle) = 0;
    virtual RenderTextureHandle createTexture(
        const TextureData& data, const TextureSamplerDesc& sampler) = 0;
    virtual void destroyTexture(RenderTextureHandle handle) = 0;
    virtual RenderMaterialHandle createMaterial(const MaterialDesc& desc) = 0;
    virtual void destroyMaterial(RenderMaterialHandle handle) = 0;

    virtual void beginFrame(const FrameData& frame) = 0;
    virtual void draw(const DrawCommand& command) = 0;
    virtual void endFrame() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void waitIdle() = 0;
    virtual RendererPerformanceStats performanceStats() const { return {}; }
};

void validateMeshData(const MeshData& data);
void validateTextureData(const TextureData& data);
bool isMeshMaterialCompatible(MeshVertexLayout layout,
                              MaterialPipeline pipeline);
glm::mat4 clipSpaceCorrection(GraphicsApi api);
