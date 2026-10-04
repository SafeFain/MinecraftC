#include "core/Window.h"
#include "model/ModelRenderer.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace {
constexpr int width = 128;
constexpr int height = 128;

std::shared_ptr<model::ModelAsset> makeModel(model::AlphaMode mode) {
    auto asset = std::make_shared<model::ModelAsset>();
    model::Material material;
    material.baseColor = {0.12f, 0.32f, 0.06f, 1.0f};
    material.alphaMode = mode;
    material.doubleSided = true;
    asset->materials.push_back(material);
    model::Primitive primitive;
    primitive.material = 0;
    for (const glm::vec3 position : {glm::vec3(-1,-1,0), glm::vec3(1,-1,0),
                                   glm::vec3(1,1,0), glm::vec3(-1,1,0)}) {
        model::Vertex vertex;
        vertex.position = position;
        vertex.normal = {0,0,1};
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0,1,2,0,2,3};
    asset->primitives.push_back(primitive);
    return asset;
}

glm::vec3 capture(VulkanRenderer& renderer, model::ModelHandle model,
                  const RenderEnvironment& environment, float altitude,
                  float cameraX = 0.0f, float distance = 4.0f) {
    const glm::vec3 camera(cameraX, altitude, distance);
    const auto vp = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1024.0f) *
        glm::lookAt(camera, glm::vec3(cameraX, altitude, 0), glm::vec3(0,1,0));
    const auto frame = [&] {
        renderer.beginFrame();
        renderer.setEnvironment(environment, camera);
        renderer.setViewProjection(vp);
        model::ModelDraw draw;
        draw.model = model;
        // Scale the distant control to keep its screen footprint unchanged.
        draw.transform = glm::translate(glm::mat4(1), glm::vec3(cameraX, altitude, 0)) *
            glm::scale(glm::mat4(1), glm::vec3(distance / 4.0f));
        renderer.modelRenderer().queue(draw);
        renderer.flushModels(vp);
        renderer.endFrame();
    };
    for (int i = 0; i < 3; ++i) frame();
    VulkanGiSmokeProbe::requestCapture(renderer);
    frame();
    const auto rgba = VulkanGiSmokeProbe::readCapture(renderer);
    if (rgba.size() != width * height * 4)
        throw std::runtime_error("unexpected capture dimensions");
    glm::vec3 color(0);
    for (int y = height/2-4; y < height/2+4; ++y)
        for (int x = width/2-4; x < width/2+4; ++x) {
            const size_t index = (y * width + x) * 4;
            color += glm::vec3(rgba[index], rgba[index+1], rgba[index+2]);
        }
    return color / 64.0f;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        Window window(width, height, "MinecraftC model altitude regression",
                      Window::SurfaceMode::Vulkan, false, false);
        VulkanRenderer renderer;
        renderer.initialize(window, std::filesystem::absolute(argv[1]));
        renderer.setVisualQuality(VisualQuality::Low);
        renderer.setEnhancedVisuals(false);
        for (const auto mode : {model::AlphaMode::Opaque, model::AlphaMode::Mask,
                                model::AlphaMode::Blend}) {
            const auto model = renderer.modelRenderer().upload(makeModel(mode));
            for (bool heaven : {false, true}) {
                RenderEnvironment environment;
                if (heaven) environment = applyHeavenEnvironment(environment);
                const auto reference = capture(renderer, model, environment, 0);
                if (reference.g < reference.r + 20 || reference.g < reference.b + 20)
                    throw std::runtime_error("reference model did not retain its green color");
                for (float altitude : {-64.0f, 64.0f, 125.0f, 160.0f, 256.0f, 319.0f}) {
                    // Also translate horizontally: no render-origin component
                    // may masquerade as camera-to-surface distance.
                    const auto actual = capture(renderer, model, environment, altitude, 240);
                    if (glm::any(glm::greaterThan(glm::abs(actual-reference), glm::vec3(2))))
                        throw std::runtime_error("model color changed with altitude/camera translation");
                }
                const float fogEnd = (Config::RENDER_DISTANCE + 0.5f) * Config::CHUNK_SIZE_X;
                const auto distant = capture(renderer, model, environment, 256, 240, fogEnd+20);
                if (glm::length(distant-reference) < 30)
                    throw std::runtime_error("actual distance no longer applies model fog");
            }
        }
        std::cout << "PASS Vulkan model fog: both dimensions, opaque/mask/blend, "
                     "Y=-64..319, camera translation and distant fog\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
