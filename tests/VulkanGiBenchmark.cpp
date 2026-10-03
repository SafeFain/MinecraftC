#include "core/Window.h"
#include "renderer/backend/vulkan/VulkanGiSmokeProbe.h"
#include "world/ChunkMesh.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace {
struct Options {
    int width=2560, height=1440, frames=600, warmup=120;
    std::filesystem::path assets, output;
    std::string scene="all", tier="all";
    bool reference=false;
};
struct Scene { const char* name; glm::vec3 camera, target; };
const std::array<Scene,3> scenes{{
    {"colored-room",{8,69,-6},{8,67,12}},
    {"cave-entry",{8,70,-16},{8,67,12}},
    {"thin-wall",{21,69,2},{21,67,14}}
}};
struct Tier { const char* name; VisualQuality quality; bool gi; };
const std::array<Tier,4> tiers{{
    {"high",VisualQuality::High,false},
    {"very-high-off",VisualQuality::VeryHigh,false},
    {"very-high",VisualQuality::VeryHigh,true},
    {"ultra",VisualQuality::Ultra,true}
}};

double percentile(std::vector<double> values, double fraction) {
    if(values.empty())return -1;
    std::sort(values.begin(),values.end());
    return values[size_t(std::ceil(fraction*values.size()))-1];
}

void run(const Options& o, const Scene& scene, const Tier& tier, std::ofstream& summary) {
    Window window(o.width,o.height,"MinecraftC GI benchmark",Window::SurfaceMode::Vulkan,false,false);
    VulkanRenderer renderer; renderer.initialize(window,o.assets);
    renderer.setVisualQuality(tier.quality);
    EnhancedVisualSettings settings;
    settings.enabled=true; settings.gi.enabled=tier.gi;
    settings.gi.distance=tier.quality==VisualQuality::Ultra?256:128;
    settings.gi.temporalStability=tier.quality==VisualQuality::Ultra?100:75;
    renderer.setEnhancedVisualSettings(settings);
    if(o.reference){
        auto config=voxelGiConfig(tier.quality,settings);
        config.temporalReuse=false; config.emptySpaceSkipping=false;
        VulkanGiSmokeProbe::configureDiagnostic(renderer,config);
    }
    std::vector<std::unique_ptr<Chunk>> chunks;
    for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x)chunks.push_back(std::make_unique<Chunk>(x,z));
    const auto source=[&](int x,int z)->Chunk* {
        const int cx=voxelGiFloorDiv(x,16),cz=voxelGiFloorDiv(z,16);
        return cx>=-2&&cx<=2&&cz>=-2&&cz<=2?chunks[size_t(cx+2+(cz+2)*5)].get():nullptr;
    };
    const auto put=[&](int x,int y,int z,BlockId id){
        source(x,z)->setBlock(voxelGiPositiveMod(x,16),y,voxelGiPositiveMod(z,16),id);
    };
    for(int z=-24;z<=32;++z)for(int x=-24;x<=32;++x)put(x,64,z,BlockId::WHITE_TERRACOTTA);
    for(int y=65;y<=74;++y)for(int z=0;z<=16;++z){
        put(1,y,z,BlockId::RED_WOOL); put(15,y,z,BlockId::BLUE_WOOL);
    }
    for(int x=1;x<=15;++x)for(int y=65;y<=74;++y)put(x,y,16,BlockId::STONE);
    for(int x=1;x<=15;++x)for(int z=0;z<=16;++z)put(x,74,z,BlockId::STONE);
    for(int x=18;x<=25;++x)for(int y=65;y<=72;++y)put(x,y,12,BlockId::STONE);
    put(4,66,10,BlockId::TORCH); put(12,66,10,BlockId::STAR_CRYSTAL);
    put(21,67,15,BlockId::STAR_CRYSTAL); // hidden by a one-block wall
    for(auto& c:chunks)for(int y=Config::WORLD_MIN_Y;y<=76;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x){
        const int wx=c->cx*16+x,wz=c->cz*16+z;
        const bool roof=wx>=1&&wx<=15&&wz>=0&&wz<=16&&y<=74;
        c->setSkyLight(x,y,z,roof?5:15);
        const int torch=std::abs(wx-4)+std::abs(y-66)+std::abs(wz-10);
        const int crystal=std::abs(wx-12)+std::abs(y-66)+std::abs(wz-10);
        c->setBlockLight(x,y,z,uint8_t(std::max(0,12-std::min(torch,crystal))));
    }
    const auto block=[&](int x,int y,int z){
        const auto* c=source(x,z);
        return c&&Config::isValidWorldY(y)?c->getBlock(voxelGiPositiveMod(x,16),y,voxelGiPositiveMod(z,16)):BlockId::AIR;
    };
    const auto light=[&](int x,int y,int z){
        const auto* c=source(x,z);
        if(!c||!Config::isValidWorldY(y))return LightSample{15,0};
        return LightSample{c->getSkyLight(voxelGiPositiveMod(x,16),y,voxelGiPositiveMod(z,16)),
                           c->getBlockLight(voxelGiPositiveMod(x,16),y,voxelGiPositiveMod(z,16))};
    };
    std::vector<ChunkMesh> meshes(chunks.size());
    for(size_t i=0;i<chunks.size();++i){
        std::vector<uint16_t> blocks; std::vector<uint8_t> lights;
        chunks[i]->copyRawState(blocks,lights);
        int maximum[16][16]{};
        meshes[i].build(chunks[i]->cx*16,chunks[i]->cz*16,blocks.data(),maximum,block,light);
        if(!meshes[i].empty())renderer.uploadChunkMesh(meshes[i]);
    }
    const auto environment=DayNightCycle{}.evaluate();
    const auto vp=glm::perspective(glm::radians(70.0f),window.aspectRatio(),0.1f,512.0f)*
        glm::lookAt(scene.camera,scene.target,glm::vec3(0,1,0));
    const auto frame=[&]{
        renderer.beginFrame(); renderer.setEnvironment(environment,scene.camera);
        renderer.setViewProjection(vp);
        renderer.renderSky(environment,glm::inverse(vp),scene.camera,false);
        renderer.beginVoxelGiFrame(glm::dvec3(scene.camera),17);
        for(const auto& c:chunks)renderer.submitVoxelGiChunk(*c);
        renderer.endVoxelGiFrame();
        for(size_t i=0;i<meshes.size();++i)if(!meshes[i].empty())
            renderer.renderChunk(meshes[i],glm::translate(glm::mat4(1),glm::vec3(chunks[i]->cx*16,0,chunks[i]->cz*16)),vp);
        PostProcessState post; post.environment=environment; post.inverseViewProjection=glm::inverse(vp);
        post.cameraPosition=scene.camera; post.sceneId=17; post.exposure=1;
        renderer.finishScene(post); renderer.endFrame();
    };
    for(int i=0;i<o.warmup;++i)frame();
    if(tier.gi&&!renderer.voxelGiStatus().active)
        throw std::runtime_error("requested GI is unsupported or unavailable on this device");
    if(tier.gi&&(renderer.voxelGiStatus().pendingSlices||renderer.voxelGiStatus().pendingCoarseChunks))
        throw std::runtime_error("warmup did not converge; increase --warmup");
    const std::string name=std::string(scene.name)+"-"+tier.name+(o.reference?"-reference":"");
    std::ofstream metadata(o.output/(name+"-metadata.txt"));
    metadata << VulkanGiSmokeProbe::deviceDescription(renderer) << "\n"
        << "resolution=" << window.width() << 'x' << window.height() << "\n"
        << "warmup=" << o.warmup << " frames=" << o.frames << " exposure=1\n"
        << "reference=" << o.reference << " (reuse and hierarchical skipping disabled; same visual gains)\n"
        << "timestamps are delayed by frame fences; gpu_sample_id identifies their submission\n";
    std::ofstream csv(o.output/(name+"-frames.csv"));
    csv << "frame,cpu_frame_ms,cpu_cache_ms,cpu_coarse_ms,cpu_pack_ms,cpu_wait_ms,gpu_sample_id,gpu_frame_ms,gpu_inject_ms,gpu_screen_ms,gpu_composite_ms,upload_bytes,pending_slices,pending_coarse\n";
    std::vector<double> cpu,gpu,screen,injection,composite;
    uint64_t lastSample=0;
    for(int i=0;i<o.frames;++i){
        const auto start=RuntimeClock{}.now(); frame();
        const double elapsed=RuntimeClock::seconds(RuntimeClock::elapsed(start,RuntimeClock{}.now()))*1000;
        const auto s=renderer.voxelGiStatus(); const auto p=renderer.performanceStats();
        cpu.push_back(elapsed);
        const bool measured=s.gpuTimingAvailable&&s.gpuSampleId!=lastSample;
        csv << i << ',' << elapsed << ',' << s.cpuCacheMs << ',' << s.cpuCoarseMs << ',' << s.cpuPackMs << ',' << p.cpuWaitMs << ',';
        if(measured){
            lastSample=s.gpuSampleId; gpu.push_back(s.gpuFrameMs); screen.push_back(s.gpuScreenMs);
            injection.push_back(s.gpuInjectionMs); composite.push_back(s.gpuCompositeMs);
            csv << s.gpuSampleId << ',' << s.gpuFrameMs << ',' << s.gpuInjectionMs << ',' << s.gpuScreenMs << ',' << s.gpuCompositeMs;
        }else csv << ",,,,";
        csv << ',' << s.uploadedBytes << ',' << s.pendingSlices << ',' << s.pendingCoarseChunks << '\n';
    }
    VulkanGiSmokeProbe::requestCapture(renderer); frame();
    const auto image=VulkanGiSmokeProbe::readCapture(renderer);
    std::ofstream ppm(o.output/(name+".ppm"),std::ios::binary);
    ppm << "P6\n" << window.width() << ' ' << window.height() << "\n255\n";
    for(size_t i=0;i<image.size();i+=4)ppm.write(reinterpret_cast<const char*>(image.data()+i),3);
    if(tier.gi){
        const auto effects=VulkanGiSmokeProbe::readEffects(renderer);
        glm::dvec3 mean(0); double peak=0; size_t lit=0;
        for(size_t i=0;i<effects.size();i+=4){
            const glm::dvec3 rgb(effects[i],effects[i+1],effects[i+2]); mean+=rgb;
            const double luminance=glm::dot(rgb,glm::dvec3(.2126,.7152,.0722));
            peak=std::max(peak,luminance); lit+=luminance>1e-5;
        }
        mean/=double(effects.size()/4);
        std::ofstream lighting(o.output/(name+"-lighting.csv"));
        lighting << "mean_r,mean_g,mean_b,peak_luminance,lit_pixels,total_pixels\n"
            << mean.r << ',' << mean.g << ',' << mean.b << ',' << peak << ',' << lit << ',' << effects.size()/4 << '\n';
        if(!lighting)throw std::runtime_error("could not save lighting metrics");
    }
    summary << name << ',' << window.width() << ',' << window.height() << ',' << o.frames << ',' << gpu.size();
    for(const auto* series:{&cpu,&gpu,&screen,&injection,&composite})
        for(double f:{0.5,0.95,0.99})summary << ',' << percentile(*series,f);
    summary << '\n'; summary.flush();
    std::cout << name << ": CPU median=" << percentile(cpu,.5) << " GPU median=" << percentile(gpu,.5)
        << " screen median=" << percentile(screen,.5) << " ms (" << gpu.size() << " samples)\n";
    if(!csv||!ppm||!summary||!metadata)throw std::runtime_error("could not save benchmark evidence");
    renderer.waitIdle(); for(auto& m:meshes)if(m.gpuReady)renderer.releaseChunkMesh(m);
}
}

int main(int argc,char** argv){
    try{
        Options o;
        if(argc<3)throw std::runtime_error("usage: vulkan_gi_benchmark assets output [--width N --height N --frames N --warmup N --scene all|colored-room|cave-entry|thin-wall --tier all|high|very-high-off|very-high|ultra --reference]");
        o.assets=std::filesystem::absolute(argv[1]);o.output=std::filesystem::absolute(argv[2]);
        for(int i=3;i<argc;++i){
            const std::string key=argv[i];
            if(key=="--reference"){o.reference=true;continue;}
            if(i+1>=argc)throw std::runtime_error("missing option value");
            const std::string value=argv[++i];
            if(key=="--width")o.width=std::stoi(value);
            else if(key=="--height")o.height=std::stoi(value);
            else if(key=="--frames")o.frames=std::stoi(value);
            else if(key=="--warmup")o.warmup=std::stoi(value);
            else if(key=="--scene")o.scene=value;
            else if(key=="--tier")o.tier=value;
            else throw std::runtime_error("unknown option: "+key);
        }
        if(o.width<32||o.height<32||o.width>8192||o.height>8192||o.frames<1||o.warmup<1)
            throw std::runtime_error("invalid benchmark dimensions/counts");
        bool sceneFound=o.scene=="all",tierFound=o.tier=="all";
        for(const auto& s:scenes)sceneFound|=o.scene==s.name;
        for(const auto& t:tiers)tierFound|=o.tier==t.name;
        if(!sceneFound||!tierFound)throw std::runtime_error("unknown scene/tier");
        std::filesystem::create_directories(o.output);
        std::ofstream summary(o.output/"summary.csv");
        summary << "case,width,height,cpu_samples,gpu_samples";
        for(const auto* metric:{"cpu_frame","gpu_frame","gpu_screen","gpu_inject","gpu_composite"})
            for(const auto* p:{"median","p95","p99"})summary << ',' << metric << '_' << p << "_ms";
        summary << '\n';
        if(o.frames<600)std::cout << "Diagnostic run below 600 frames; not hardware performance acceptance.\n";
        for(const auto& s:scenes)if(o.scene=="all"||o.scene==s.name)
            for(const auto& t:tiers)if(o.tier=="all"||o.tier==t.name)run(o,s,t,summary);
        return 0;
    }catch(const std::exception& e){std::cerr << "GI benchmark failed: " << e.what() << '\n';return 1;}
}
