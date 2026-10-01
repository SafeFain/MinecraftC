#include "core/Window.h"
#include "renderer/BlockAtlasData.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"
#include "world/ChunkMesh.h"

#include <iostream>
#include <fstream>
#include <memory>

namespace {
void requireSmoke(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void runSmoke(const std::filesystem::path& assets, VisualQuality quality,
              const std::filesystem::path& evidence) {
    Window window(160, 96, "MinecraftC GI regression", Window::SurfaceMode::Vulkan, false, false);
    VulkanRenderer renderer;
    renderer.initialize(window, assets);
    renderer.setVisualQuality(quality);
    EnhancedVisualSettings settings;
    settings.enabled = true;
    settings.bloom = settings.lightShafts = settings.reflections = false;
    settings.atmosphere = settings.materialMotion = false;
    settings.gi.enabled = true;
    settings.gi.distance = quality == VisualQuality::Ultra ? 256 : 128;
    settings.gi.temporalStability = 0;
    renderer.setEnhancedVisualSettings(settings);
    std::vector<std::unique_ptr<Chunk>> chunks;
    for (int z = -2; z <= 2; ++z) for (int x = -2; x <= 2; ++x) {
        auto chunk = std::make_unique<Chunk>(x, z);
        for (int cz = 0; cz < 16; ++cz) for (int cx = 0; cx < 16; ++cx) {
            chunk->setBlock(cx, 64, cz, BlockId::STONE);
            chunk->setSkyLight(cx, 64, cz, 15);
            if (z == 1) for (int y = 65; y < 76; ++y) {
                chunk->setBlock(cx, y, cz, BlockId::PLANKS);
                chunk->setBlockLight(cx, y, cz, 14);
            }
        }
        chunks.push_back(std::move(chunk));
    }
    ChunkMesh mesh;
    const float stone = float(getAtlasTextureIndex(BlockTexture::Stone));
    const float planks = float(getAtlasTextureIndex(BlockTexture::Planks));
    mesh.vertices = {
        {-32,65,-32,1,1,0,1,0,0,stone,0}, {48,65,-32,1,1,0,1,8,0,stone,0},
        {48,65,48,1,1,0,1,8,8,stone,0}, {-32,65,48,1,1,0,1,0,8,stone,0},
        {-32,65,16,1,0,1,1,0,0,planks,2}, {48,65,16,1,0,1,1,8,0,planks,2},
        {48,76,16,1,0,1,1,8,1,planks,2}, {-32,76,16,1,0,1,1,0,1,planks,2}};
    mesh.indices = {0,2,1,0,3,2,4,6,5,4,7,6};
    mesh.opaqueIndexCount = mesh.indices.size();
    renderer.uploadChunkMesh(mesh);
    glm::vec3 camera(8,70,8);
    RenderEnvironment environment = DayNightCycle{}.evaluate();
    uint64_t sceneId = 17;
    const auto frame = [&] {
        const auto view = glm::lookAt(camera, camera + glm::vec3(0,-0.25f,1), glm::vec3(0,1,0));
        const auto vp = glm::perspective(glm::radians(70.0f), window.aspectRatio(), 0.1f, 512.0f)*view;
        renderer.beginFrame();
        renderer.setEnvironment(environment, camera);
        renderer.setViewProjection(vp);
        renderer.renderSky(environment, glm::inverse(vp), camera, false);
        renderer.beginVoxelGiFrame(glm::dvec3(camera), sceneId);
        for (const auto& chunk : chunks) renderer.submitVoxelGiChunk(*chunk);
        renderer.endVoxelGiFrame();
        renderer.renderChunk(mesh, glm::mat4(1), vp);
        PostProcessState post;
        post.environment = environment;
        post.inverseViewProjection = glm::inverse(vp);
        post.cameraPosition = camera;
        post.sceneId = sceneId;
        renderer.finishScene(post);
        renderer.endFrame();
        requireSmoke(renderer.voxelGiStatus().active, "GI did not activate");
    };
    const auto settle = [&] {
        for (int i = 0; i < 40; ++i) frame();
        requireSmoke(renderer.voxelGiStatus().pendingSlices == 0, "GI queue did not converge");
    };
    settle();
    const auto initial = VulkanGiSmokeProbe::readEffects(renderer);
    const std::string tier = quality == VisualQuality::Ultra ? "ultra" : "very-high";
    std::ofstream metrics;
    if (!evidence.empty()) {
        std::ofstream image(evidence/(tier+"-gi.ppm"),std::ios::binary);
        image << "P6\n80 48\n255\n";
        for(size_t i=0;i<initial.size();i+=4)for(int c=0;c<3;++c){
            const char value=char(voxelGiByte(std::sqrt(std::max(initial[i+c],0.0f))));
            image.write(&value,1);
        }
        requireSmoke(bool(image),"could not save GI screenshot");
        metrics.open(evidence/(tier+"-rays.csv"));
        metrics << "case,level,r,g,b,coverage\n";
    }
    float peak = 0;
    float minimumAo = 1;
    for (size_t i = 0; i < initial.size(); ++i) {
        if (i % 4 == 3) minimumAo = std::min(minimumAo, initial[i]);
        else peak = std::max(peak, initial[i]);
    }
    requireSmoke(peak > 0.00001f && minimumAo < 0.99f,
                 "real GPU GI/AO output was missing");
    renderer.setVisualQuality(quality);
    renderer.setEnhancedVisualSettings(settings);
    settings.bloomStrength = 25;
    renderer.setEnhancedVisualSettings(settings);
    frame();
    requireSmoke(renderer.voxelGiStatus().sourceChunks == chunks.size() &&
                 renderer.voxelGiStatus().pendingSlices == 0 &&
                 renderer.voxelGiStatus().uploadedBytes == 0 &&
                 renderer.voxelGiStatus().injectedVoxels == 0,
                 "unchanged quality/unrelated effect rebuilt GI");
    for (bool bloom : {true, false}) {
        settings.bloom = bloom;
        renderer.setEnhancedVisualSettings(settings);
        frame();
        requireSmoke(renderer.voxelGiStatus().sourceChunks == chunks.size() &&
                     renderer.voxelGiStatus().pendingSlices == 0 &&
                     renderer.voxelGiStatus().uploadedBytes == 0 &&
                     renderer.voxelGiStatus().injectedVoxels == 0,
                     "Bloom swapchain rebuild discarded the GI volume cache");
    }
    for (int axis = 0; axis < 3; ++axis) {
        camera[axis] += 1;
        frame();
        const auto partial = renderer.voxelGiStatus();
        requireSmoke(partial.uploadedBytes == 64u*64u*32u &&
                     partial.injectedVoxels == 64u*64u,
                     "single-axis motion did not use one actual GPU plane");
        const auto local = VulkanGiSmokeProbe::readEffects(renderer);
        VulkanGiSmokeProbe::forceFullUpdate(renderer);
        frame();
        const auto full = VulkanGiSmokeProbe::readEffects(renderer);
        requireSmoke(local.size() == full.size(), "GPU comparison size mismatch");
        for (size_t i = 0; i < local.size(); ++i)
            requireSmoke(std::abs(local[i]-full[i]) <= 0.00001f,
                         "partial injection disagrees with full-volume GPU reference");
    }
    camera += glm::vec3(1);
    frame();
    const auto diagonalStatus = renderer.voxelGiStatus();
    requireSmoke(diagonalStatus.uploadedBytes > 0 &&
                 diagonalStatus.injectedVoxels * 32 == diagonalStatus.uploadedBytes &&
                 diagonalStatus.uploadedBytes < 3u*64u*64u*64u*32u,
                 "diagonal move did not retain disjoint partial GPU regions");
    const auto diagonal = VulkanGiSmokeProbe::readEffects(renderer);
    VulkanGiSmokeProbe::forceFullUpdate(renderer);
    frame();
    const auto diagonalFull = VulkanGiSmokeProbe::readEffects(renderer);
    for (size_t i = 0; i < diagonal.size(); ++i)
        requireSmoke(std::abs(diagonal[i]-diagonalFull[i]) <= 0.00001f,
                     "multi-region injection differs from full-volume GPU reference");
    settings.gi.temporalStability = 100;
    renderer.setEnhancedVisualSettings(settings);
    frame();
    requireSmoke(renderer.voxelGiStatus().uploadedBytes == 0 &&
                 renderer.voxelGiStatus().injectedVoxels == 0,
                 "stability adjustment rebuilt GI");
    settings.gi.strength = 50;
    renderer.setEnhancedVisualSettings(settings);
    frame();
    requireSmoke(VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0 &&
                 renderer.voxelGiStatus().uploadedBytes == 0,
                 "strength adjustment kept old-scale history or rebuilt volume");
    chunks[12]->setBlock(8,65,8,BlockId::TORCH);
    frame();
    requireSmoke(VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0,
                 "block edit reused stale lighting history");
    while (renderer.voxelGiStatus().pendingSlices > 0) {
        frame();
        requireSmoke(VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0,
                     "bounded edit propagation accumulated intermediate old lighting");
    }
    settle();
    environment.daylight = 0.2f;
    environment.ambientColor *= 0.5f;
    frame();
    const size_t levels = quality == VisualQuality::Ultra ? 4 : 3;
    requireSmoke(renderer.voxelGiStatus().injectedVoxels == levels*64u*64u*64u &&
                 renderer.voxelGiStatus().uploadedBytes == 0 &&
                 VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y == 0,
                 "environment change did not relight fully and reject stale history");
    ++sceneId;
    settle();
    if (quality == VisualQuality::VeryHigh) {
        settings.gi.distance = 256;
        renderer.setEnhancedVisualSettings(settings);
        settle();
    }
    settings.gi.temporalStability = 0;
    settings.gi.distance = quality == VisualQuality::Ultra ? 256 : 128;
    settings.gi.strength = 100;
    renderer.setEnhancedVisualSettings(settings);
    environment.ambientIntensity = 0;
    environment.directIntensity = 0;
    // Isolate emitted radiance with unchanged receiver mesh/camera. The probe
    // dispatches the same traceCone used by the screen fragment shader.
    const auto put = [&](glm::ivec3 p,BlockId id,uint8_t sky=0) {
        const int cx=voxelGiFloorDiv(p.x,16),cz=voxelGiFloorDiv(p.z,16);
        auto& chunk=*chunks[size_t(cx+2+(cz+2)*5)];
        chunk.setBlock(voxelGiPositiveMod(p.x,16),p.y,voxelGiPositiveMod(p.z,16),id);
        chunk.setSkyLight(voxelGiPositiveMod(p.x,16),p.y,voxelGiPositiveMod(p.z,16),sky);
        chunk.setBlockLight(voxelGiPositiveMod(p.x,16),p.y,voxelGiPositiveMod(p.z,16),0);
    };
    const auto clear = [&] {
        for(auto& chunk:chunks)for(int y=64;y<84;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x)
            chunk->setBlock(x,y,z,BlockId::AIR);
    };
    const int lastLevel = quality == VisualQuality::Ultra ? 3 : 2;
    const auto ray = [&](const char* name,glm::vec3 origin,glm::vec3 direction,int level) {
        VulkanGiSmokeProbe::Ray input;
        input.origin=glm::vec4(origin,1); input.direction=glm::vec4(direction,0);
        const auto result=VulkanGiSmokeProbe::traceRays(renderer,{input},level)[0].result;
        if(metrics.is_open())metrics << name << ',' << level << ',' << result.r << ',' << result.g << ',' << result.b << ',' << result.a << '\n';
        return result;
    };
    const auto energy = [](glm::vec4 value){return value.r+value.g+value.b;};
    const auto compareFull = [&](glm::vec3 origin,glm::vec3 direction) {
        const auto partial=ray("partial",origin,direction,lastLevel);
        VulkanGiSmokeProbe::forceFullUpdate(renderer); frame();
        const auto full=ray("full",origin,direction,lastLevel);
        requireSmoke(glm::all(glm::lessThanEqual(glm::abs(partial-full),glm::vec4(0.00001f))),
                     "edited auxiliary regions differ from full GPU reference");
    };
    for(int axis=0;axis<3;++axis){
        clear();
        for(int v=0;v<8;++v)for(int u=0;u<8;++u){
            glm::ivec3 p; p[axis]=4; p[(axis+1)%3]=u; p[(axis+2)%3]=v;
            p.y+=64; put(p,BlockId::STONE);
        }
        glm::ivec3 source(4,4,4); source[axis]=12; source.y+=64;
        put(source,BlockId::STAR_CRYSTAL);
        glm::vec3 origin(4.5f,68.5f,4.5f),direction(0);
        origin[axis]=axis==1?64.5f:0.5f; direction[axis]=1;
        settle();
        for(int level=0;level<=lastLevel;++level)
            requireSmoke(energy(ray("closed",origin,direction,level))<0.000001f,
                         "axis thin wall leaked emitted radiance");
        for(int v=4;v<6;++v)for(int u=4;u<6;++u){
            glm::ivec3 p; p[axis]=4; p[(axis+1)%3]=u; p[(axis+2)%3]=v;
            p.y+=64; put(p,BlockId::AIR);
        }
        settle();
        for(int level=0;level<=lastLevel;++level)
            requireSmoke(energy(ray("aperture",origin,direction,level))>0.00001f,
                         "resolvable coarse aperture remained closed");
        compareFull(origin,direction);
    }
    clear();
    for(int x=0;x<=8;++x)for(int y=64;y<72;++y)put({x,y,8-x},BlockId::STONE);
    put({12,68,12},BlockId::STAR_CRYSTAL);
    settle();
    const glm::vec3 diagonalOrigin(0.5f,68.5f,0.5f),diagonalDirection(1,0,1);
    for(int level=0;level<=lastLevel;++level)
        requireSmoke(energy(ray("oblique-closed",diagonalOrigin,diagonalDirection,level))<0.000001f,
                     "oblique thin wall leaked colored emission");
    put({4,68,4},BlockId::AIR); put({4,69,4},BlockId::AIR);
    settle();
    for(int level=0;level<=lastLevel;++level)
        requireSmoke(energy(ray("oblique-open",diagonalOrigin,diagonalDirection,level))>0.00001f,
                     "oblique subcell aperture did not transmit");
    compareFull(diagonalOrigin,diagonalDirection);
    clear();
    const auto materials=buildVoxelGiMaterials(buildBlockAtlasData(assets));
    glm::vec3 previousColor(0);
    for(const auto id:{BlockId::TORCH,BlockId::STAR_CRYSTAL}){
        put({4,68,4},id); settle();
        const auto color=glm::vec3(ray("emission-color",{4.5f,72.5f,4.5f},{0,-1,0},0));
        requireSmoke(glm::length(color)>0.00001f,"colored source did not emit");
        const auto expected=voxelGiUnpackRgb(voxelGiRgb(materials[size_t(id)].emission));
        requireSmoke(glm::length(glm::normalize(color)-glm::normalize(expected))<0.002f,
                     "source chromaticity was replaced by reflected albedo or fixed warmth");
        if(id==BlockId::STAR_CRYSTAL)
            requireSmoke(glm::length(glm::normalize(color)-previousColor)>0.05f,
                         "different emitters produced the same GI color");
        previousColor=glm::normalize(color);
    }
    clear();
    put({4,68,4},BlockId::STONE,15);
    for(const auto d:{glm::ivec3(0,-1,0),glm::ivec3(1,0,0),glm::ivec3(-1,0,0),
                      glm::ivec3(0,0,1),glm::ivec3(0,0,-1)})put(glm::ivec3(4,68,4)+d,BlockId::STONE);
    environment.directIntensity=1; environment.directColor=glm::vec3(1);
    environment.lightDirection={0,1,0}; settle();
    const auto facing=ray("sun-facing",{4.5f,69.5f,4.5f},{0,-1,0},0);
    environment.lightDirection={0,-1,0}; frame();
    requireSmoke(renderer.voxelGiStatus().uploadedBytes==0 &&
                 VulkanGiSmokeProbe::lastUniforms(renderer).temporal.y==0,
                 "direction light change uploaded geometry or retained history");
    const auto away=ray("sun-away",{4.5f,69.5f,4.5f},{0,-1,0},0);
    requireSmoke(energy(facing)>0.00001f && energy(away)<0.000001f,
                 "face exposure did not control sun direction injection");
    clear();
    put({15,68,4},BlockId::STONE,15);
    for(const auto d:{glm::ivec3(0,1,0),glm::ivec3(0,-1,0),glm::ivec3(-1,0,0),
                      glm::ivec3(0,0,1),glm::ivec3(0,0,-1)})put(glm::ivec3(15,68,4)+d,BlockId::STONE);
    environment.lightDirection={1,0,0}; settle();
    requireSmoke(energy(ray("border-air",{15.9f,68.5f,4.5f},{-1,0,0},0))>0.00001f,
                 "loaded border air did not expose receiver");
    put({16,68,4},BlockId::STONE); settle();
    requireSmoke(energy(ray("border-edit",{15.9f,68.5f,4.5f},{-1,0,0},0))<0.000001f,
                 "neighbor edit retained stale GPU face exposure");
    compareFull({15.9f,68.5f,4.5f},{-1,0,0});
    clear();
    for(int x=0;x<16;++x)for(int y=64;y<80;++y)put({x,y,4},BlockId::STONE,15);
    environment.ambientIntensity=1; environment.ambientColor=glm::vec3(1);
    environment.directIntensity=0; settle();
    for(float boundary:{2.0f,4.0f}){
        std::vector<VulkanGiSmokeProbe::Ray> inputs(2);
        for(int i=0;i<2;++i){
            inputs[i].origin=glm::vec4(4.5f,68.5f,4.01f,0);
            inputs[i].direction=glm::vec4(0,0,1,boundary+(i==0?-0.001f:0.001f));
        }
        const auto results=VulkanGiSmokeProbe::traceRays(renderer,inputs);
        requireSmoke(energy(results[0].result)>0.00001f &&
            glm::length(glm::vec3(results[0].result-results[1].result))<0.01f,
            "radiance footprint has a discontinuity at a clipmap scale transition");
        if(metrics.is_open())for(const auto& r:results)
            metrics << "footprint-" << boundary << ",0," << r.result.r << ',' << r.result.g << ',' << r.result.b << ',' << r.result.a << '\n';
    }
    if(metrics.is_open())requireSmoke(bool(metrics),"could not save ray metrics");
    std::cout << "GI ray scenes: three axes/oblique, apertures, colors, sun, partial/full passed\n";
    settings.gi.strength = 0;
    renderer.setEnhancedVisualSettings(settings);
    // The disabled frame has no GI passes or source-cache submissions.
    renderer.beginFrame();
    renderer.finishScene(PostProcessState{});
    renderer.endFrame();
    requireSmoke(!renderer.voxelGiStatus().active, "zero strength left GI active");
    renderer.waitIdle();
    renderer.releaseChunkMesh(mesh);
    std::cout << "GI smoke tier=" << (quality == VisualQuality::Ultra ? "Ultra" : "VeryHigh")
              << " peak=" << peak << " AO=" << minimumAo
              << " single-plane=131072 bytes/4096 voxels; GPU local/full equal\n";
}
}

int main(int argc,char** argv) {
    if (argc != 2 && argc != 3) return 2;
    try {
        const auto assets = std::filesystem::absolute(argv[1]);
        const auto evidence=argc==3?std::filesystem::absolute(argv[2]):std::filesystem::path{};
        if(!evidence.empty())std::filesystem::create_directories(evidence);
        runSmoke(assets,VisualQuality::VeryHigh,evidence);
        runSmoke(assets,VisualQuality::Ultra,evidence);
        std::cout << "Voxel GI Vulkan smoke passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "Voxel GI Vulkan smoke failed: " << error.what() << '\n';
        return 1;
    }
}
