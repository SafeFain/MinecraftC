#include "renderer/RenderEnvironment.h"
#include "renderer/CameraEffects.h"
#include "model/ModelRenderLogic.h"
#include "renderer/ShaderDialect.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const std::string desktopShader = "#version 330 core\nvoid main(){}\n";
    require(shaderSourceForApi(desktopShader, GraphicsApi::OpenGL33) == desktopShader,
            "desktop shader source was unexpectedly rewritten");
    const std::string esShader = shaderSourceForApi(
        desktopShader, GraphicsApi::OpenGLES30);
    require(esShader.rfind("#version 300 es\nprecision highp float;\n"
                           "precision highp int;\n", 0) == 0 &&
            esShader.find("#version 330 core") == std::string::npos,
            "GLES shader preamble was not generated correctly");
    require(model::modelPass(model::AlphaMode::Opaque) ==
                model::ModelPass::Opaque &&
            model::modelPass(model::AlphaMode::Mask) ==
                model::ModelPass::Opaque,
            "opaque and masked model materials were split incorrectly");
    require(model::modelPass(model::AlphaMode::Blend) ==
                model::ModelPass::Blend,
            "blended model material used the opaque pass");
    std::vector<model::BlendSortEntry> blended{{2.0f}, {9.0f}, {4.0f}};
    model::sortBlended(blended);
    require(blended[0].distanceSquared == 9.0f &&
            blended[2].distanceSquared == 2.0f,
            "blended model draws were not sorted far-to-near");
    CameraEffects cameraEffects;
    cameraEffects.reset({0.0, 64.0, 0.0});
    cameraEffects.update({0.0, 64.0, 0.0}, true, false, 1.0f / 60.0f);
    require(cameraEffects.movementBlend() == 0.0f,
            "stationary player produced view bobbing");
    for (int i = 1; i <= 30; ++i)
        cameraEffects.update({i * 0.08, 64.0, 0.0}, true, false, 1.0f / 60.0f);
    require(cameraEffects.movementBlend() > 0.5f &&
            glm::length(cameraEffects.translation()) > 0.001f,
            "ground movement did not produce view bobbing");
    cameraEffects.onDamage(6.0f);
    require(cameraEffects.trauma() > 0.6f,
            "damage did not produce camera trauma");
    cameraEffects.update({2.4, 64.0, 0.0}, true, false, 0.05f);
    require(glm::length(cameraEffects.rotationDegrees()) > 0.1f,
            "damage trauma did not produce rotational shake");
    for (int i = 0; i < 20; ++i)
        cameraEffects.update({2.4, 64.0, 0.0}, true, false, 0.05f);
    require(cameraEffects.trauma() == 0.0f,
            "damage trauma did not decay to zero");

    DayNightCycle cycle;
    require(DayNightCycle::isDayPhase(0.0f), "sunrise was not day");
    require(DayNightCycle::isDayPhase(0.25f), "noon was not day");
    require(DayNightCycle::isDayPhase(0.5f - 0.000001f),
            "the phase immediately before sunset was not day");
    require(!DayNightCycle::isDayPhase(0.5f),
            "sunset did not enter night immediately");
    require(!DayNightCycle::isDayPhase(0.999999f),
            "the phase immediately before sunrise was not night");
    cycle.setDay();
    require(cycle.phase() == 0.0f && cycle.isDay(),
            "set day did not select sunrise");
    cycle.setNight();
    require(cycle.phase() == 0.5f && cycle.isNight(),
            "set night did not select sunset");
    cycle.update(0.1f, 0, true);
    require(cycle.phase() == 0.5f,
            "a manually selected time was lost in static-cycle mode");
    cycle.resetMorning();
    const float morning = cycle.phase();
    require(!cycle.isNight(), "morning was classified as night");

    cycle.update(1.0f, 20, false);
    require(cycle.phase() == morning, "paused cycle advanced");

    cycle.update(0.1f, 20, true);
    require(cycle.phase() > morning, "active cycle did not advance");

    DayNightCycle nightCycle;
    for (int i = 0; i < 900; ++i) nightCycle.update(0.1f, 1, true);
    require(nightCycle.isNight(), "night phase was not recognized");

    cycle.update(0.1f, 0, false);
    require(std::abs(cycle.phase() - DayNightCycle::STATIC_DAY_PHASE) < 0.0001f,
            "static day did not select noon");

    const RenderEnvironment noon = cycle.evaluate();
    require(noon.daylight > 0.95f, "static noon is not daylight");
    require(noon.ambientIntensity >= Config::NIGHT_AMBIENT_MIN &&
            noon.ambientIntensity <= 1.0f,
            "noon ambient is outside expected range");
    const RenderEnvironment rain = applyWeather(noon, 1.0f, 0.0f, 0.0f);
    const RenderEnvironment thunder = applyWeather(noon, 1.0f, 1.0f, 0.0f);
    const RenderEnvironment flash = applyWeather(noon, 1.0f, 1.0f, 1.0f);
    require(rain.directIntensity < noon.directIntensity &&
            thunder.directIntensity < rain.directIntensity,
            "weather did not progressively darken direct light");
    require(rain.starIntensity == 0.0f,
            "overcast weather did not hide stars");
    require(flash.ambientIntensity > thunder.ambientIntensity,
            "lightning flash did not brighten the environment");
    const glm::vec3 noonCloud = cloudColorForEnvironment(noon);
    const glm::vec3 rainCloud = cloudColorForEnvironment(rain);
    const glm::vec3 thunderCloud = cloudColorForEnvironment(thunder);
    require(glm::length(rainCloud) < glm::length(noonCloud),
            "rain clouds should be darker than clear-day clouds");
    require(glm::length(thunderCloud) < glm::length(rainCloud),
            "thunder clouds should be darker than rain clouds");

    // Switching back to an automatic cycle resumes from noon. Advance half a
    // cycle in bounded frame-sized steps to reach midnight.
    for (int i = 0; i < 6000; ++i) cycle.update(0.1f, 20, true);
    const RenderEnvironment midnight = cycle.evaluate();
    require(midnight.daylight < 0.05f, "half cycle did not reach night");
    require(midnight.starIntensity > 0.9f, "night sky has no stars");
    require(midnight.ambientIntensity >= Config::NIGHT_AMBIENT_MIN,
            "night ambient fell below playable minimum");
    require(glm::length(cloudColorForEnvironment(midnight)) <
                glm::length(noonCloud),
            "night clouds should be darker than daytime clouds");

    // One complete 10-minute cycle must wrap back to its starting phase.
    cycle.update(0.1f, 0, false);
    const float start = cycle.phase();
    for (int i = 0; i < 6000; ++i) cycle.update(0.1f, 10, true);
    require(std::abs(cycle.phase() - start) < 0.001f,
            "day cycle did not wrap deterministically");

    std::cout << "render environment logic passed\n";
}
