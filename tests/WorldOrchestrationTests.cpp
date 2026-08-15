// World orchestration tests: exercise the World facade's state transitions
// (chunk streaming, async generation handoff, persistence queues, mesh
// revision handoff, fluid ticks, seed reset) through the public API. These
// tests lock in the current orchestration behavior and serve as the
// regression net for the World.cpp component split.

#include "world/World.h"
#include "Config.h"
#include "game/SaveStore.h"
#include "renderer/GameRenderer.h"
#include "threading/ThreadPool.h"
#include "world/FluidLogic.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Minimal IGameRenderer: records chunk mesh upload/release calls and no-ops
// everything else. Only the paths World touches are observable.
class StubRenderer final : public IGameRenderer {
public:
    int uploadCount = 0;
    int releaseCount = 0;
    size_t lastUploadBytes = 0;

    // ── IRenderDevice ──────────────────────────────────────────────
    RenderDeviceCapabilities capabilities() const override { return {}; }
    RenderMeshHandle createMesh(const MeshData&) override { return {}; }
    void destroyMesh(RenderMeshHandle) override {}
    RenderTextureHandle createTexture(const TextureData&,
                                      const TextureSamplerDesc&) override {
        return {};
    }
    void destroyTexture(RenderTextureHandle) override {}
    RenderMaterialHandle createMaterial(const MaterialDesc&) override {
        return {};
    }
    void destroyMaterial(RenderMaterialHandle) override {}
    void beginFrame(const FrameData&) override {}
    void draw(const DrawCommand&) override {}
    void endFrame() override {}
    void resize(int, int) override {}
    void waitIdle() override {}

    // ── IGameRenderer ──────────────────────────────────────────────
    void initialize(Window&, const GraphicsCapabilities&,
                    const std::filesystem::path&) override {}
    void reinitialize(const GraphicsCapabilities&,
                      const std::filesystem::path&) override {}
    void beginFrame() override {}
    void setVisualQuality(VisualQuality) override {}
    void finishScene(const PostProcessState&) override {}
    void setEnvironment(const RenderEnvironment&, const glm::vec3&) override {}
    void renderSky(const RenderEnvironment&, const glm::mat4&,
                   const glm::vec3&, bool) override {}
    void renderChunk(const ChunkMesh&, const glm::mat4&, const glm::mat4&,
                     bool) override {}
    void renderChunkShadows(ShadowQuality, const glm::mat4&, const glm::mat4&,
                            const glm::dvec3&,
                            const std::vector<ShadowChunkSubmission>&) override {
    }
    void uploadChunkMesh(ChunkMesh& mesh) override {
        ++uploadCount;
        lastUploadBytes = mesh.uploadBytes();
    }
    void releaseChunkMesh(ChunkMesh&) override { ++releaseCount; }
    void beginTranslucent() override {}
    void endTranslucent() override {}
    void bindBlockShader() const override {}
    void unbindBlockShader() const override {}
    void renderWireframe(const glm::vec3&, const glm::mat4&) override {}
    void renderEntity(const glm::vec3&, const glm::vec3&, const glm::vec3&,
                      int, const glm::mat4&) override {}
    void renderCompatibilityEntityCube(const glm::vec3&, const glm::vec3&,
                                       const glm::vec3&, int, float,
                                       const glm::mat4&,
                                       SmoothLightSample) override {}
    model::ModelRenderer& modelRenderer() override {
        throw std::logic_error(
            "modelRenderer is not used by orchestration tests");
    }
    void flushModels(const glm::mat4&) override {}
    void beginViewModel(const glm::mat4&) override {}
    void renderEntityPart(const glm::vec3&, const glm::vec3&,
                          const glm::vec3&, float, const glm::vec3&, int,
                          const glm::mat4&, SmoothLightSample) override {}
    void renderParticles(const std::vector<ParticleRenderData>&,
                         const glm::mat4&, const glm::vec3&, const glm::vec3&,
                         float) override {}
    void renderClouds(const glm::dvec3&, const glm::mat4&, uint64_t, float,
                      int) override {}
    void setViewProjection(const glm::mat4&) override {}
    void setFrustum(const Frustum& frustum) override { m_frustum = frustum; }
    const Frustum& getFrustum() const override { return m_frustum; }
    RenderTextureHandle getBlockAtlasTexture() const override { return {}; }
    uint32_t blockAtlasTilesPerSide() const override { return 1; }
    bool usesFramebufferSrgb() const override { return false; }

private:
    Frustum m_frustum;
};

// Worker tasks hold raw World pointers; drain them before the World goes out
// of scope (mirrors the loading-gate drain in the application).
void drainWorkers(World& world, ThreadPool& pool) {
    for (int i = 0; i < 4000; ++i) {
        world.processCompletedGenerations();
        if (pool.idle()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(false, "worker tasks drain before world teardown");
}

// Stream toward the current render target until the loading gate opens.
// The gate is trivially "ready" for an empty target (freshly reset world),
// so always run at least one update to build the desired-chunk set.
void loadTarget(World& world) {
    int frames = 0;
    do {
        world.update({0.5, 64.0, 0.5});
        world.enqueueGeneration();
        world.processCompletedGenerations();
        ++frames;
    } while (!world.streamingTargetReady() && frames < 400);
    require(world.streamingTargetReady(), "streaming reaches the render target");
}

// Generate every chunk in the target and apply completed generations.
// enqueueGeneration is bounded by CHUNK_GEN_TASKS_IN_FLIGHT, so keep
// feeding the pipeline until the whole target is generated and idle.
void generateTarget(World& world, ThreadPool& pool) {
    int frames = 0;
    for (; frames < 4000; ++frames) {
        world.enqueueGeneration();
        world.processCompletedGenerations();
        const auto progress = world.generationProgress();
        if (progress.total > 0 && progress.completed == progress.total &&
            pool.idle()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(frames < 4000, "all target chunks finish generating");
    const auto progress = world.generationProgress();
    require(progress.total > 0 && progress.completed == progress.total,
            "all target chunks finish generating");
}

bool isChunkActive(const World& world, int cx, int cz) {
    for (const Chunk* chunk : world.getActiveChunks()) {
        if (chunk->cx == cx && chunk->cz == cz) return true;
    }
    return false;
}

// 1. Streaming: chunks load around the player, edits persist, and a far
// teleport unloads old chunks while saving their pending state.
void testChunkStreaming() {
    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-world-orch-streaming";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SaveStore store(root);
    World world;
    ThreadPool pool(2);
    world.setThreadPool(&pool);
    world.setSaveStore(&store);
    world.resetForNewSeed(1001);

    loadTarget(world);
    generateTarget(world, pool);
    require(isChunkActive(world, 0, 0), "spawn chunk is active");
    const int surface = world.getSurfaceY(0, 0);
    require(Config::isValidWorldY(surface),
            "surface query returns a valid height");

    // A player edit lands in the autosave queue and round-trips to disk.
    world.setBlock(0, surface + 1, 0, BlockId::STONE);
    require(world.hasModifiedChunks(), "block edit registers modified chunks");
    world.beginModifiedChunkAutosave();
    world.flushModifiedChunks();
    require(!world.hasModifiedChunks(), "flush empties the modified queues");
    bool found = false;
    for (const auto& entry : store.loadChunkOverrides(0, 0)) {
        if (entry.block == BlockId::STONE) {
            found = true;
            break;
        }
    }
    require(found, "override round-trips through SaveStore");

    // A second edit is left unsaved; unloading must persist it.
    world.setBlock(1, surface + 1, 1, BlockId::COBBLESTONE);
    const uint64_t revisionBefore = world.streamingRevision();
    int frames = 0;
    while (isChunkActive(world, 0, 0) && frames < 3000) {
        world.update({100000.0, 64.0, 100000.0});
        world.enqueueGeneration();
        world.processCompletedGenerations();
        ++frames;
    }
    require(frames < 3000, "old chunks unload after a far teleport");
    require(world.streamingRevision() != revisionBefore,
            "streaming revision bumps when the target changes");
    bool cobble = false;
    for (const auto& entry : store.loadChunkOverrides(0, 0)) {
        if (entry.block == BlockId::COBBLESTONE) {
            cobble = true;
            break;
        }
    }
    require(cobble, "unload persists unsaved overrides");
    drainWorkers(world, pool);
    std::filesystem::remove_all(root);
}

// 2. Generation handoff: completed generations apply exactly once and the
// chunk set stays fully generated afterwards.
void testGenerationHandoff() {
    World world;
    ThreadPool pool(2);
    world.setThreadPool(&pool);
    world.resetForNewSeed(42);

    loadTarget(world);
    generateTarget(world, pool);

    // Re-applying completed generations is idempotent: no chunk is left
    // half-generated and no second pass changes the progress.
    const auto progressAfter = world.generationProgress();
    world.processCompletedGenerations();
    const auto progressAgain = world.generationProgress();
    require(progressAgain.completed == progressAfter.completed &&
                progressAgain.completed == progressAgain.total,
            "completed generation application is idempotent");

    // Loaded chunks stay resident while the player remains in the target.
    require(isChunkActive(world, 0, 0) && isChunkActive(world, 1, 1),
            "chunks remain resident inside the render target");
    drainWorkers(world, pool);
}

// 3. Seed reset: a new seed clears the world state, bumps the streaming
// revision, and generates different terrain.
void testSeedReset() {
    World world;
    ThreadPool pool(2);
    world.setThreadPool(&pool);

    world.resetForNewSeed(777);
    loadTarget(world);
    generateTarget(world, pool);
    const uint64_t revisionA = world.streamingRevision();
    std::vector<int> heightsA;
    for (int x = -5; x <= 5; ++x) {
        for (int z = -5; z <= 5; ++z) {
            heightsA.push_back(world.getSurfaceY(x, z));
        }
    }

    world.resetForNewSeed(778);
    require(world.getActiveChunks().empty(), "reset clears active chunks");
    require(world.streamingRevision() != revisionA,
            "reset bumps the streaming revision");
    loadTarget(world);
    generateTarget(world, pool);
    std::vector<int> heightsB;
    for (int x = -5; x <= 5; ++x) {
        for (int z = -5; z <= 5; ++z) {
            heightsB.push_back(world.getSurfaceY(x, z));
        }
    }
    require(heightsA != heightsB, "a different seed changes the terrain");
    drainWorkers(world, pool);
}

// 4. Autosave queue: bounded flushes leave pending saves visible, unbounded
// flushes persist everything.
void testAutosaveQueue() {
    const auto root = std::filesystem::temp_directory_path() /
                      "minecraftc-world-orch-autosave";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SaveStore store(root);
    World world;
    ThreadPool pool(2);
    world.setThreadPool(&pool);
    world.setSaveStore(&store);
    world.resetForNewSeed(2024);

    // Load a small target; the second edit targets a neighboring chunk that
    // does not exist yet and is created on demand.
    int frames = 0;
    do {
        world.update({0.5, 64.0, 0.5}, 3);
        world.enqueueGeneration();
        world.processCompletedGenerations();
        ++frames;
    } while (!world.streamingTargetReady() && frames < 400);
    require(world.streamingTargetReady(), "small target streams in");
    generateTarget(world, pool);
    const int surface = world.getSurfaceY(0, 0);
    world.setBlock(0, surface + 1, 0, BlockId::STONE);
    world.setBlock(16, surface + 1, 0, BlockId::COBBLESTONE);

    world.beginModifiedChunkAutosave();
    require(world.hasPendingModifiedChunkSaves(),
            "autosave begin moves edits into the pending queue");
    require(!world.flushModifiedChunks(1),
            "bounded flush leaves pending saves");
    require(world.hasPendingModifiedChunkSaves(),
            "pending saves remain visible after a bounded flush");
    require(world.flushModifiedChunks(), "unbounded flush completes");
    require(!world.hasModifiedChunks(), "all modified state is persisted");
    drainWorkers(world, pool);
    std::filesystem::remove_all(root);
}

// 5. Mesh pipeline: completed meshes upload once, stale revisions are
// skipped, and GPU meshes release/restore through the renderer.
void testMeshPipeline() {
    World world;
    ThreadPool pool(2);
    world.setThreadPool(&pool);
    world.resetForNewSeed(555);

    // Load exactly one chunk so mesh counts are deterministic.
    world.update({0.5, 64.0, 0.5}, 1);
    world.enqueueGeneration();
    world.waitForInitialGeneration(8000);
    world.processCompletedGenerations();
    drainWorkers(world, pool);
    require(world.getActiveChunks().size() == 1,
            "bounded update loads exactly one chunk");

    StubRenderer stub;
    const int surface = world.getSurfaceY(0, 0);

    // First pass: the dirty chunk builds and uploads once.
    world.enqueueMeshBuilds(4);
    drainWorkers(world, pool);
    world.processCompletedMeshes(&stub, 4);
    require(stub.uploadCount == 1, "completed mesh uploads exactly once");
    require(stub.lastUploadBytes > 0, "uploaded mesh carries geometry");

    // Second pass: an edit after the worker finished makes the pending mesh
    // stale; the upload is skipped and the chunk is re-dirtied.
    world.setBlock(0, surface + 1, 0, BlockId::STONE);
    world.enqueueMeshBuilds(4);
    drainWorkers(world, pool);
    world.setBlock(1, surface + 1, 1, BlockId::COBBLESTONE);
    world.processCompletedMeshes(&stub, 4);
    require(stub.uploadCount == 1, "stale-revision mesh is skipped");

    // Third pass: the rebuilt mesh uploads again.
    world.enqueueMeshBuilds(4);
    drainWorkers(world, pool);
    world.processCompletedMeshes(&stub, 4);
    require(stub.uploadCount == 2, "rebuilt mesh uploads after revision change");

    // GPU release and restore round trip through the renderer.
    world.invalidateGpuMeshes();
    require(stub.releaseCount >= 1, "invalidating meshes releases GPU buffers");
    world.restoreGpuMeshes();
    require(stub.uploadCount >= 3, "restore re-uploads active meshes");
    drainWorkers(world, pool);
}

// 6. Fluid ticks: water falls through open air and lava ignites adjacent
// TNT through the scheduled tick queue.
void testFluidTicks() {
    World world;
    ThreadPool pool(2);
    world.setThreadPool(&pool);
    world.resetForNewSeed(999);

    world.update({0.5, 64.0, 0.5}, 1);
    world.enqueueGeneration();
    world.waitForInitialGeneration(8000);
    world.processCompletedGenerations();
    drainWorkers(world, pool);

    // Open sky column: a water source falls one cell per fluid tick window
    // (each fall re-schedules with a +5 tick delay).
    require(world.getBlock(0, 80, 0) == BlockId::AIR,
            "sky column above the terrain is open air");
    world.setBlock(0, 80, 0, BlockId::WATER);
    world.tickFluids(1000);
    require(isWater(world.getBlock(0, 80, 0)), "water source stays in place");
    require(isWater(world.getBlock(0, 79, 0)),
            "water spreads into the cell below");
    world.tickFluids(1005);
    require(isWater(world.getBlock(0, 78, 0)), "water keeps falling");

    // Lava adjacent to TNT ignites it through the same tick queue. It sits
    // beyond the water's horizontal reach so the two experiments cannot
    // interact.
    world.setBlock(10, 80, 0, BlockId::TNT);
    world.setBlock(9, 80, 0, BlockId::LAVA);
    world.tickFluids(1035);
    const auto ignitions = world.takeTntIgnitions();
    require(!ignitions.empty(), "lava ignites adjacent TNT");
    require(world.getBlock(10, 80, 0) == BlockId::AIR,
            "ignited TNT is consumed");
    drainWorkers(world, pool);
}

}  // namespace

int main() {
    testChunkStreaming();
    testGenerationHandoff();
    testSeedReset();
    testAutosaveQueue();
    testMeshPipeline();
    testFluidTicks();
    std::cout << "World orchestration tests passed\n";
    return 0;
}
