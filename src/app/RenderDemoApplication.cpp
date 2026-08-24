#include "app/Application.h"

#include "Config.h"
#include "core/RuntimeClock.h"
#include "core/Window.h"
#include "debug/CrashHandler.h"
#include "debug/Log.h"
#include "entity/EntityModelRegistry.h"
#include "renderer/ChunkRenderScene.h"
#include "renderer/TexturedCubeScene.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

class BasicRenderApplication final : public ApplicationHost {
public:
    BasicRenderApplication(RuntimePaths paths, bool texturedDemo = false,
                           int benchmarkFrames = 0)
        : m_paths(std::move(paths)),
          m_window(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT,
                   "MinecraftC - Vulkan",
                   Window::SurfaceMode::Vulkan,
                   benchmarkFrames == 0, benchmarkFrames == 0),
          m_benchmarkFrames(benchmarkFrames) {
        auto renderer = std::make_unique<VulkanRenderer>();
        renderer->initialize(m_window, m_paths.assetRoot);
        m_renderer = std::move(renderer);
        if (texturedDemo)
            m_texturedScene = std::make_unique<TexturedCubeScene>(
                *m_renderer, m_paths.assetRoot);
        else
            m_scene = std::make_unique<ChunkRenderScene>(
                *m_renderer, m_paths.assetRoot, benchmarkFrames > 0 ? 8 : 0);
        if (benchmarkFrames > 0 && !texturedDemo) {
            m_benchmarkModels = std::make_unique<EntityModelRegistry>();
            m_benchmarkModels->loadAll(m_paths.assetRoot);
            m_benchmarkModels->uploadAll(m_renderer->modelRenderer());
        }
        m_window.setResizeCallback([this](int width, int height) {
            m_renderer->resize(width, height);
        });
    }

    bool iterate() override {
        const auto started = m_clock.now();
        if (!m_backgrounded && !m_window.isMinimized()) {
            if (m_texturedScene) {
                m_texturedScene->render(m_window.aspectRatio());
            } else if (m_benchmarkModels) {
                m_scene->render(m_window.aspectRatio(), [this](const glm::mat4& vp) {
                    renderBenchmarkModels(vp);
                });
            } else {
                m_scene->render(m_window.aspectRatio());
            }
            if (m_benchmarkFrames > 0) {
                const double elapsedMs = RuntimeClock::seconds(
                    RuntimeClock::elapsed(started, m_clock.now())) * 1000.0;
                ++m_renderedFrames;
                if (m_renderedFrames > benchmarkWarmupFrames()) {
                    m_frameSamples.push_back(elapsedMs);
                    const RendererPerformanceStats stats = m_renderer->performanceStats();
                    m_rendererTotals.cpuWaitMs += stats.cpuWaitMs;
                    m_rendererTotals.cpuPrepareMs += stats.cpuPrepareMs;
                    m_rendererTotals.cpuRecordMs += stats.cpuRecordMs;
                    m_rendererTotals.cpuSubmitMs += stats.cpuSubmitMs;
                    m_rendererTotals.uploadBytes += stats.uploadBytes;
                    m_rendererTotals.drawCalls += stats.drawCalls;
                    m_rendererTotals.pipelineBinds += stats.pipelineBinds;
                    m_rendererTotals.descriptorBinds += stats.descriptorBinds;
                    m_rendererTotals.vertexBufferBinds += stats.vertexBufferBinds;
                }
                if (static_cast<int>(m_frameSamples.size()) >= m_benchmarkFrames) {
                    reportBenchmark();
                    m_running = false;
                }
            }
        }
        m_window.finishEventFrame();
        return m_running && !m_window.shouldClose();
    }

    void event(ApplicationEvent event, const void* nativeEvent) override {
        if (nativeEvent) m_window.handleEvent(nativeEvent);
        if (event == ApplicationEvent::EnterBackground) {
            m_renderer->suspendPresentation();
            m_backgrounded = true;
        }
        if (event == ApplicationEvent::EnterForeground) {
            m_renderer->resumePresentation();
            m_backgrounded = false;
        }
        if (event == ApplicationEvent::Terminating) m_running = false;
    }

    void shutdown() override {
        if (m_cleaned) return;
        m_cleaned = true;
        m_scene.reset();
        m_texturedScene.reset();
        m_renderer->waitIdle();
        Debug::Log::shutdown();
    }

private:
    RuntimePaths m_paths;
    Window m_window;
    std::unique_ptr<IGameRenderer> m_renderer;
    std::unique_ptr<ChunkRenderScene> m_scene;
    std::unique_ptr<TexturedCubeScene> m_texturedScene;
    std::unique_ptr<EntityModelRegistry> m_benchmarkModels;
    bool m_running = true;
    bool m_backgrounded = false;
    bool m_cleaned = false;
    int m_benchmarkFrames = 0;
    int m_renderedFrames = 0;
    RuntimeClock m_clock;
    std::vector<double> m_frameSamples;
    RendererPerformanceStats m_rendererTotals{};

    int benchmarkWarmupFrames() const {
        return m_benchmarkFrames >= 1800 ? 600 : 60;
    }

    void renderBenchmarkModels(const glm::mat4& viewProjection) {
        static constexpr std::array<EntityType, 4> TYPES{
            EntityType::Cow, EntityType::Pig,
            EntityType::Sheep, EntityType::Chicken};
        m_benchmarkModels->beginFrame();
        for (uint64_t id = 1; id <= 128; ++id) {
            const EntityType type = TYPES[static_cast<size_t>(id) % TYPES.size()];
            m_benchmarkModels->setLocomotion(type, id, 1.0f);
            m_benchmarkModels->advance(type, id, 1.0f / 60.0f);
            const int row = static_cast<int>((id - 1) / 16);
            const int column = static_cast<int>((id - 1) % 16);
            m_benchmarkModels->queue(type, id,
                glm::dvec3(column * 3 - 22, m_scene->groundHeight(),
                           row * 3 - 10),
                glm::vec3(0.0f, 0.0f, -1.0f), static_cast<uint32_t>(id),
                glm::dvec3(0.0), glm::vec3(0.0f), m_renderer->modelRenderer(),
                glm::vec3(1.0f), {1.0f, 0.0f});
        }
        m_benchmarkModels->endFrame();
        m_renderer->flushModels(viewProjection);
    }

    void reportBenchmark() {
        std::sort(m_frameSamples.begin(), m_frameSamples.end());
        const auto percentile = [&](double fraction) {
            const size_t index = std::min(m_frameSamples.size() - 1,
                static_cast<size_t>(fraction * (m_frameSamples.size() - 1)));
            return m_frameSamples[index];
        };
        double total = 0.0;
        for (double sample : m_frameSamples) total += sample;
        const double divisor = static_cast<double>(m_frameSamples.size());
        std::cout << std::fixed << std::setprecision(3)
                  << "MINECRAFTC_BENCHMARK frames=" << m_frameSamples.size()
                  << " width=" << m_window.width()
                  << " height=" << m_window.height()
                  << " avg_ms=" << total / divisor
                  << " p50_ms=" << percentile(0.50)
                  << " p95_ms=" << percentile(0.95)
                  << " p99_ms=" << percentile(0.99)
                  << " wait_ms=" << m_rendererTotals.cpuWaitMs / divisor
                  << " prepare_ms=" << m_rendererTotals.cpuPrepareMs / divisor
                  << " record_ms=" << m_rendererTotals.cpuRecordMs / divisor
                  << " submit_ms=" << m_rendererTotals.cpuSubmitMs / divisor
                  << " draws=" << m_rendererTotals.drawCalls / m_frameSamples.size()
                  << " pipeline_binds="
                  << m_rendererTotals.pipelineBinds / m_frameSamples.size()
                  << " descriptor_binds="
                  << m_rendererTotals.descriptorBinds / m_frameSamples.size()
                  << " buffer_binds="
                  << m_rendererTotals.vertexBufferBinds / m_frameSamples.size()
                  << '\n';
    }
};


std::unique_ptr<ApplicationHost> createRenderDemoApplication(
    RuntimePaths paths, bool texturedDemo, int benchmarkFrames) {
    Debug::Log::init(Debug::LogLevel::Trace, false);
    Debug::installCrashHandlers();
    return std::make_unique<BasicRenderApplication>(
        std::move(paths), texturedDemo, benchmarkFrames);
}
