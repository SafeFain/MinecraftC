#include "OverworldStructureFixtures.h"
#include "core/Window.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"
#include "world/ChunkMesh.h"
#include "world/WorldGenerator.h"

#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
constexpr int width = 960, height = 640;
int floorChunk(int n) { return n/16-(n%16<0); }

void writeCapture(VulkanRenderer& renderer, const std::filesystem::path& path) {
    const auto rgba = VulkanGiSmokeProbe::readCapture(renderer);
    if (rgba.size() != width*height*4) throw std::runtime_error("unexpected structure capture size");
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t i = 0; i < rgba.size(); i += 4)
        out.write(reinterpret_cast<const char*>(rgba.data()+i),3);
    if (!out) throw std::runtime_error("structure capture write failed");
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: vulkan_structure_smoke <assets> <capture-directory>\n";
        return 2;
    }
    try {
        std::filesystem::create_directories(argv[2]);
        Window window(width,height,"MinecraftC v18 structures",Window::SurfaceMode::Vulkan,false,false);
        VulkanRenderer renderer;
        renderer.initialize(window,std::filesystem::absolute(argv[1]));
        renderer.setVisualQuality(VisualQuality::Medium);
        renderer.setEnhancedVisuals(false);
        std::cout << VulkanGiSmokeProbe::deviceDescription(renderer) << '\n';
        DayNightCycle day;
        day.setPhase(0.25f);
        const auto environment = day.evaluate();
        for (const auto& fixture : NEW_STRUCTURE_FIXTURES) {
            WorldGenerator world(1234567890ULL);
            const int ox = floorChunk(fixture.x)-1, oz = floorChunk(fixture.z)-1;
            std::vector<std::unique_ptr<Chunk>> chunks;
            std::vector<Chunk*> view;
            for (int z = 0; z < 3; ++z) for (int x = 0; x < 3; ++x) {
                chunks.push_back(std::make_unique<Chunk>(ox+x,oz+z));
                view.push_back(chunks.back().get());
            }
            std::vector<RegionGenerationData::PendingBlock> pending;
            world.generateRegion(ox,oz,3,Config::REGION_PADDING,view,pending);
            const auto blockAt = [&](int x,int y,int z) {
                const int cx = floorChunk(x)-ox, cz = floorChunk(z)-oz;
                if (cx<0 || cx>=3 || cz<0 || cz>=3 || !Config::isValidWorldY(y)) return BlockId::AIR;
                const auto& c = chunks[cz*3+cx];
                return c->getBlock(x-c->worldX(),y,z-c->worldZ());
            };
            for (int pass = 0; pass < 2; ++pass) for (const auto& b : pending) {
                if (b.overwrite != (pass==1)) continue;
                const int cx = floorChunk(b.worldX)-ox, cz = floorChunk(b.worldZ)-oz;
                if (cx<0 || cx>=3 || cz<0 || cz>=3) continue;
                if (!b.overwrite && isSolid(blockAt(b.worldX,b.worldY,b.worldZ))) continue;
                auto& c = chunks[cz*3+cx];
                c->setBlock(b.worldX-c->worldX(),b.worldY,b.worldZ-c->worldZ(),b.id);
            }
            std::array<ChunkMesh,9> meshes;
            for (size_t i = 0; i < chunks.size(); ++i) {
                int maxima[16][16];
                chunks[i]->copyColumnMaxY(maxima);
                auto& mesh = meshes[i];
                mesh.build(chunks[i]->worldX(),chunks[i]->worldZ(),chunks[i]->rawBlocks(),maxima,
                           blockAt,[](int,int,int) { return LightSample{15,0}; });
                if (mesh.opaqueIndexCount==0 || mesh.indexCount!=mesh.indices.size() ||
                    mesh.translucentIndexOffset!=mesh.opaqueIndexCount ||
                    mesh.shadowCasterIndexOffset!=mesh.opaqueIndexCount+mesh.translucentIndexCount)
                    throw std::runtime_error("structure index-layer handoff mismatch");
                renderer.uploadChunkMesh(mesh);
            }
            const float r = static_cast<float>(fixture.radius);
            const float top = static_cast<float>(fixture.height);
            // Use the blueprint's entry orientation, retaining its true world
            // terrain while translating only the render origin for precision.
            std::vector<StructurePlacement> placements;
            world.getStructureGenerator().generateStructuresRegion(fixture.x,fixture.z,1,1,placements);
            const auto it = std::find_if(placements.begin(),placements.end(),[&](const auto& p) { return p.type==fixture.type; });
            if (it==placements.end()) throw std::runtime_error("missing visual structure fixture");
            glm::vec3 offset(r*2.2f,top+12,-r*3.2f);
            if (fixture.type==StructureType::MountainWatchtower || fixture.type==StructureType::AbandonedFarmstead)
                offset.z = -offset.z;
            if (((it->variant>>60)&1u) != 0) offset.x = -offset.x;
            const int rotations = static_cast<int>((it->variant>>61)&3u);
            for (int i = 0; i < rotations; ++i) { const float px=offset.x; offset.x=-offset.z; offset.z=px; }
            const glm::vec3 camera = glm::vec3(0,fixture.y,0)+offset;
            const glm::vec3 target(0,fixture.y+top*0.3f,0);
            const auto vp = glm::perspective(glm::radians(58.0f),window.aspectRatio(),0.1f,512.0f) *
                            glm::lookAt(camera,target,glm::vec3(0,1,0));
            const auto frame = [&] {
                renderer.beginFrame();
                renderer.setEnvironment(environment,camera);
                renderer.setViewProjection(vp);
                renderer.renderSky(environment,glm::inverse(vp),camera,false);
                for (size_t i = 0; i < meshes.size(); ++i) {
                    const glm::mat4 transform = glm::translate(glm::mat4(1),
                        glm::vec3(chunks[i]->worldX()-fixture.x,0,chunks[i]->worldZ()-fixture.z));
                    renderer.renderChunk(meshes[i],transform,vp);
                }
                PostProcessState post;
                post.environment=environment;
                post.cameraPosition=camera;
                post.inverseViewProjection=glm::inverse(vp);
                renderer.finishScene(post);
                renderer.endFrame();
            };
            for (int i = 0; i < 8; ++i) frame();
            VulkanGiSmokeProbe::requestCapture(renderer);
            frame();
            writeCapture(renderer,std::filesystem::path(argv[2])/(std::string(structureCommandName(fixture.type))+".ppm"));
            renderer.waitIdle();
            for (auto& mesh : meshes) renderer.releaseChunkMesh(mesh);
            std::cout << "PASS " << structureCommandName(fixture.type) << " generated region, mesh layers, Vulkan upload/draw/capture\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Structure smoke failed: " << e.what() << '\n';
        return 1;
    }
}
