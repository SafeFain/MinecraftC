#include "renderer/ChunkRenderScene.h"

#include "Config.h"
#include "debug/Log.h"
#include "world/Block.h"
#include "world/Chunk.h"
#include "world/RegionGenerator.h"
#include "world/WorldGenerator.h"

#include <array>
#include <memory>
#include <queue>
#include <stdexcept>

namespace {
constexpr uint64_t SCENE_SEED = 1234567890ULL;
constexpr int REGION_SIDE = 3;

Chunk* chunkAt(const std::array<std::unique_ptr<Chunk>, 9>& chunks, int wx, int wz) {
    const int cx = wx >= 0 ? wx / Config::CHUNK_SIZE_X
                           : (wx - Config::CHUNK_SIZE_X + 1) / Config::CHUNK_SIZE_X;
    const int cz = wz >= 0 ? wz / Config::CHUNK_SIZE_Z
                           : (wz - Config::CHUNK_SIZE_Z + 1) / Config::CHUNK_SIZE_Z;
    if (cx < 0 || cx >= REGION_SIDE || cz < 0 || cz >= REGION_SIDE) return nullptr;
    return chunks[static_cast<size_t>(cz) * REGION_SIDE + cx].get();
}

glm::ivec3 localPosition(const Chunk& chunk, int wx, int wy, int wz) {
    return {wx - chunk.worldX(), wy, wz - chunk.worldZ()};
}

void initializeLighting(const std::array<std::unique_ptr<Chunk>, 9>& chunks) {
    std::queue<BlockLightNode> skyQueue;
    std::queue<BlockLightNode> blockQueue;
    for (const auto& owned : chunks) {
        Chunk& chunk = *owned;
        chunk.clearLight();
        for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
            for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                uint8_t vertical = 15;
                for (int y = Config::WORLD_MAX_Y - 1; y >= Config::WORLD_MIN_Y; --y) {
                    const BlockId block = chunk.getBlock(x, y, z);
                    const uint8_t damping = getLightDampening(block);
                    if (damping >= 15) vertical = 0;
                    else if (damping > 0) vertical = vertical > damping ? vertical - damping : 0;
                    if (vertical) {
                        chunk.setSkyLight(x, y, z, vertical);
                        skyQueue.push({chunk.worldX() + x, y, chunk.worldZ() + z, vertical});
                    }
                    const uint8_t emission = getLightEmission(block);
                    if (emission) {
                        chunk.setBlockLight(x, y, z, emission);
                        blockQueue.push({chunk.worldX() + x, y, chunk.worldZ() + z, emission});
                    }
                }
            }
        }
    }
    auto spread = [&](std::queue<BlockLightNode>& queue, bool sky) {
        while (!queue.empty()) {
            const BlockLightNode node = queue.front(); queue.pop();
            if (node.light <= 1) continue;
            for (const glm::ivec3& offset : FACE_OFFSETS) {
                const glm::ivec3 p{node.x + offset.x, node.y + offset.y,
                                   node.z + offset.z};
                if (!Config::isValidWorldY(p.y)) continue;
                Chunk* target = chunkAt(chunks, p.x, p.z);
                if (!target) continue;
                const glm::ivec3 local = localPosition(*target, p.x, p.y, p.z);
                const uint8_t damping = getLightDampening(
                    target->getBlock(local.x, local.y, local.z));
                if (damping >= 15) continue;
                const uint8_t next = node.light > std::max<uint8_t>(1, damping)
                    ? node.light - std::max<uint8_t>(1, damping) : 0;
                const uint8_t old = sky ? target->getSkyLight(local.x, local.y, local.z)
                                        : target->getBlockLight(local.x, local.y, local.z);
                if (next <= old) continue;
                if (sky) target->setSkyLight(local.x, local.y, local.z, next);
                else target->setBlockLight(local.x, local.y, local.z, next);
                queue.push({p.x, p.y, p.z, next});
            }
        }
    };
    spread(skyQueue, true);
    spread(blockQueue, false);
}

ChunkMesh generateCenterChunk(int& surfaceY) {
    WorldGenerator world(SCENE_SEED);
    RegionGenerator generator(world.getHeightPipeline(), world.getCaveGenerator(),
                              world.getTreeGenerator(), world.getOreGenerator(),
                              SCENE_SEED);
    std::array<std::unique_ptr<Chunk>, 9> chunks;
    std::vector<Chunk*> raw;
    for (int cz = 0; cz < REGION_SIDE; ++cz) for (int cx = 0; cx < REGION_SIDE; ++cx) {
        const size_t index = static_cast<size_t>(cz) * REGION_SIDE + cx;
        chunks[index] = std::make_unique<Chunk>(cx, cz);
        raw.push_back(chunks[index].get());
    }
    std::vector<RegionGenerationData::PendingBlock> pending;
    generator.generateRegion(0, 0, REGION_SIDE, Config::REGION_PADDING, raw, pending);
    initializeLighting(chunks);
    Chunk& center = *chunks[4];
    ChunkMesh mesh;
    auto block = [&](int wx, int wy, int wz) {
        if (!Config::isValidWorldY(wy)) return BlockId::AIR;
        Chunk* chunk = chunkAt(chunks, wx, wz);
        if (!chunk) return BlockId::AIR;
        const glm::ivec3 local = localPosition(*chunk, wx, wy, wz);
        return chunk->getBlock(local.x, local.y, local.z);
    };
    auto light = [&](int wx, int wy, int wz) -> LightSample {
        if (!Config::isValidWorldY(wy)) return {};
        Chunk* chunk = chunkAt(chunks, wx, wz);
        if (!chunk) return {};
        const glm::ivec3 local = localPosition(*chunk, wx, wy, wz);
        return unpackLight(chunk->getPackedLight(local.x, local.y, local.z));
    };
    mesh.build(center.worldX(), center.worldZ(), center.rawBlocks(),
               center.getColumnMaxYData(), block, light);
    if (mesh.empty() || mesh.opaqueIndexCount == 0)
        throw std::runtime_error("Generated center ChunkMesh is empty");
    surfaceY = center.getColumnMaxY(8, 8);
    LOG_INFO("Chunk demo mesh: " << mesh.vertices.size() << " vertices, "
             << mesh.opaqueIndexCount << " opaque/cutout indices, "
             << mesh.translucentIndexCount << " translucent indices retained");
    return mesh;
}
}

ChunkRenderScene::ChunkRenderScene(IGameRenderer& renderer,
                                   const std::filesystem::path& assetRoot,
                                   int benchmarkGridRadius)
    : m_renderer(renderer) {
    (void)assetRoot;
    int surfaceY = 0;
    m_mesh = generateCenterChunk(surfaceY);
    m_groundHeight = static_cast<float>(surfaceY + 1);
    renderer.uploadChunkMesh(m_mesh);
    m_camera.setPosition({39.0f, static_cast<float>(surfaceY + 17), 43.0f});
    m_camera.updateVectors(-135.0f, -28.0f);
    if (benchmarkGridRadius > 0) {
        for (int z = -benchmarkGridRadius; z <= benchmarkGridRadius; ++z)
            for (int x = -benchmarkGridRadius; x <= benchmarkGridRadius; ++x)
                m_instances.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(
                    static_cast<float>(x * Config::CHUNK_SIZE_X), 0.0f,
                    static_cast<float>(z * Config::CHUNK_SIZE_Z))));
        std::stable_sort(m_instances.begin(), m_instances.end(),
            [](const glm::mat4& a, const glm::mat4& b) {
                const glm::vec3 pa(a[3]);
                const glm::vec3 pb(b[3]);
                return glm::dot(pa, pa) < glm::dot(pb, pb);
            });
    }
}

ChunkRenderScene::~ChunkRenderScene() {
    m_renderer.waitIdle();
    m_renderer.releaseChunkMesh(m_mesh);
}

void ChunkRenderScene::render(float aspectRatio, const ExtraPass& extraPass) {
    FrameData frame;
    frame.clearColor = {0.48f, 0.70f, 0.91f, 1.0f};
    frame.view = m_camera.getViewMatrix();
    frame.projection = m_camera.getProjectionMatrix(aspectRatio);
    const glm::mat4 viewProjection = frame.projection * frame.view;
    RenderEnvironment environment;
    environment.fogColor = glm::vec3(frame.clearColor);
    m_renderer.beginFrame();
    m_renderer.setEnvironment(environment, m_camera.m_position);
    m_renderer.setViewProjection(viewProjection);
    m_renderer.bindBlockShader();
    if (m_instances.empty()) {
        m_renderer.renderChunk(m_mesh, glm::mat4(1.0f),
                               viewProjection, false);
        // Exercise the backend's multi-draw path with a second instance sharing
        // the same immutable production mesh and atlas resources.
        const glm::mat4 model = glm::translate(
            glm::mat4(1.0f), glm::vec3(-18.0f, -3.0f, 0.0f));
        m_renderer.renderChunk(m_mesh, model,
                               viewProjection, false);
    } else {
        for (const glm::mat4& model : m_instances) {
            m_renderer.renderChunk(m_mesh, model,
                                   viewProjection, false);
        }
    }
    if (m_mesh.translucentIndexCount) {
        m_renderer.beginTranslucent();
        if (m_instances.empty()) {
            m_renderer.renderChunk(m_mesh, glm::mat4(1.0f),
                                   viewProjection, true);
        } else {
            for (auto instance = m_instances.rbegin(); instance != m_instances.rend();
                 ++instance) {
                m_renderer.renderChunk(m_mesh, *instance,
                                       viewProjection, true);
            }
        }
        m_renderer.endTranslucent();
    }
    m_renderer.unbindBlockShader();
    if (extraPass) extraPass(viewProjection);
    // Resolve the offscreen scene target and compose it to the default
    // framebuffer before drawing the scene.
    m_renderer.finishScene(PostProcessState{});
    m_renderer.endFrame();
}
