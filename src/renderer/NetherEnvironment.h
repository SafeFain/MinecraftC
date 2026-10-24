#pragma once

#include "renderer/RenderEnvironment.h"
#include "world/NetherGenerator.h"

inline RenderEnvironment netherEnvironment(const NetherGenerator& generator,
                                          const glm::dvec3& position) {
    constexpr std::array<std::array<float,3>,5> colors{{
        {{.29f,.055f,.035f}},{{.075f,.22f,.25f}},{{.31f,.035f,.055f}},
        {{.025f,.19f,.18f}},{{.22f,.20f,.23f}}
    }};
    glm::vec3 fog(0);
    // Bilinear samples blend the eight-block neighborhood at biome edges.
    const double gx=position.x/8.0,gz=position.z/8.0;
    const int bx=static_cast<int>(std::floor(gx)),bz=static_cast<int>(std::floor(gz));
    const float fx=static_cast<float>(gx-bx),fz=static_cast<float>(gz-bz);
    for(int z=0;z<2;++z)for(int x=0;x<2;++x) {
        const auto& c=colors[static_cast<size_t>(generator.biomeAt((bx+x)*8,(bz+z)*8))];
        fog+=glm::vec3(c[0],c[1],c[2])*(x?fx:1-fx)*(z?fz:1-fz);
    }
    RenderEnvironment env;
    env.skyFlags=SkyNoAtmosphere|SkyNoLowClouds|SkyNoVoxelClouds|SkyNoLightShafts|SkyCavernAmbient;
    env.skyStyle=RenderSkyStyle::Layered;
    env.sunSize=env.moonSize=env.starIntensity=env.directIntensity=0;
    env.daylight=0;env.ambientIntensity=.55f;env.ambientColor={.65f,.44f,.36f};
    env.zenithColor=env.horizonColor=env.fogColor=fog;
    env.particleIntensity=.65f;
    return env;
}
