// Included by the display-dependent model smoke test.
#include <fstream>

struct BatchCapture {
    std::vector<uint8_t> pixels;
    RendererPerformanceStats stats;
};

BatchCapture captureBatch(VulkanRenderer& renderer,model::ModelHandle handle,int count,
                          bool batching,size_t pageBytes=0,bool poison=true,bool overlap=false,bool split=false) {
    renderer.modelRenderer().setBatching(batching,pageBytes);
    const auto vp=glm::ortho(-16.f,16.f,-16.f,16.f,.1f,100.f)*
        glm::lookAt(glm::vec3(0,0,20),glm::vec3(0),glm::vec3(0,1,0));
    const auto frame=[&] {
        renderer.beginFrame();renderer.setEnvironment(RenderEnvironment{},glm::vec3(0,0,20));renderer.setViewProjection(vp);
        std::vector<model::ModelInstance> retained(static_cast<size_t>(count));
        for(int submission=0;submission<count;++submission) {
            const int i=split?(submission*11)%count:submission;
            auto& instance=retained[static_cast<size_t>(i)];
            instance.pose.global.assign(256,glm::mat4(1));
            instance.jointPalettes.resize(1);instance.jointPalettes[0].assign(256,glm::mat4(1));
            instance.jointPalettes[0][255]=glm::translate(glm::mat4(1),glm::vec3(count==1?4.f:(i%3)*.7f,0,0));
            model::ModelDraw draw;
            draw.model=handle;draw.instance=&instance;
            draw.transform=glm::translate(glm::mat4(1),glm::vec3((i%32)-15.5f,(i/32)-7.5f,0))*glm::scale(glm::mat4(1),glm::vec3(.38f));
            if(overlap) draw.transform=glm::translate(glm::mat4(1),glm::vec3(0,0,i*.02f))*glm::scale(glm::mat4(1),glm::vec3(3));
            draw.tint=glm::vec4(.3f+.7f*(i%2),.3f+.7f*((i/2)%2),1,1);
            draw.distanceSquared=static_cast<float>(count-i);
            renderer.modelRenderer().queue(draw);
            if(poison) { instance.pose.global.clear();instance.jointPalettes.clear(); }
            if(split && submission%3==2) renderer.flushModels(vp);
        }
        renderer.flushModels(vp);renderer.finishScene(PostProcessState{});renderer.endFrame();
    };
    for(int i=0;i<3;++i) frame();
    VulkanGiSmokeProbe::requestCapture(renderer);frame();
    return {VulkanGiSmokeProbe::readCapture(renderer),renderer.performanceStats()};
}

void modelBatchCases(VulkanRenderer& renderer,const char* capturePath=nullptr) {
    auto makeSkin=[&](int parts,model::AlphaMode mode=model::AlphaMode::Opaque) {
        auto asset=makeModel(mode);asset->materials[0].baseColor=glm::vec4(1,1,1,mode==model::AlphaMode::Blend?.45f:1.f);
        for(auto& vertex:asset->primitives[0].vertices) vertex.joints=glm::uvec4(255,0,0,0);
        asset->nodes.resize(256);asset->nodes[0].skin=0;
        asset->skins.resize(1);
        for(int i=0;i<256;++i) asset->skins[0].joints.push_back(i);
        const auto original=asset->primitives[0];
        for(int i=1;i<parts;++i) {asset->primitives.push_back(original);asset->nodes[0].primitives.push_back(i);}
        return asset;
    };
    const auto oneAsset=makeSkin(1),twoAsset=makeSkin(2);
    const auto one=renderer.modelRenderer().upload(oneAsset),two=renderer.modelRenderer().upload(twoAsset);
    if(renderer.modelRenderer().upload(oneAsset)!=one) throw std::runtime_error("GPU model cache uploaded duplicate geometry");
    for(int count:{128,512}) {
        const auto reference=captureBatch(renderer,one,count,false,0,false);
        const auto batched=captureBatch(renderer,one,count,true);
        size_t visiblePixels=0;
        for(size_t i=0;i<reference.pixels.size();i+=4) if(reference.pixels[i]>120 || reference.pixels[i+1]>120 || reference.pixels[i+2]>120) ++visiblePixels;
        if(visiblePixels<static_cast<size_t>(count)) throw std::runtime_error("model batch capture contains no visible models");
        if(reference.pixels!=batched.pixels) throw std::runtime_error("instancing/snapshot changed model pixels");
        if(reference.stats.modelDrawCalls!=static_cast<uint32_t>(count) || batched.stats.modelDrawCalls!=1)
            throw std::runtime_error("opaque model draws failed to batch");
        const auto multi=captureBatch(renderer,two,count,true);
        if(multi.stats.modelPaletteBytes!=batched.stats.modelPaletteBytes ||
           multi.stats.modelPaletteBytes!=static_cast<uint64_t>(count)*256*64 || multi.stats.modelDrawCalls!=2)
            throw std::runtime_error("palette duplicated across primitives or joint255 lost");
        std::cout << "PASS model batch instances=" << count << " draws=" << batched.stats.modelDrawCalls
                  << " reference_draws=" << reference.stats.modelDrawCalls << " palette_bytes=" << batched.stats.modelPaletteBytes << '\n';
        if(count==512 && capturePath) {
            std::ofstream ppm(capturePath,std::ios::binary);
            ppm << "P6\n" << width << ' ' << height << "\n255\n";
            for(size_t i=0;i<batched.pixels.size();i+=4) ppm.write(reinterpret_cast<const char*>(&batched.pixels[i]),3);
        }
    }
    const auto highJoint=captureBatch(renderer,one,1,true);
    double pixelX=0;size_t colored=0;
    for(size_t p=0;p<highJoint.pixels.size();p+=4) if(highJoint.pixels[p+2]>120 && highJoint.pixels[p+2]>highJoint.pixels[p]*1.5f) {
        pixelX+=(p/4)%width;++colored;
    }
    if(!colored || pixelX/colored<5 || pixelX/colored>11) throw std::runtime_error("joint255 transform was not applied by the GPU");
    const auto large=captureBatch(renderer,one,12,true);
    const auto paged=captureBatch(renderer,one,12,true,16384);
    if(large.pixels!=paged.pixels || paged.stats.modelDrawCalls!=12) throw std::runtime_error("model storage paging dropped instances");
    const auto multiPaged=captureBatch(renderer,two,12,true,16384);
    if(multiPaged.stats.modelPaletteBytes!=large.stats.modelPaletteBytes) throw std::runtime_error("palette duplicated when primitive batches revisit storage pages");
    const auto blend=renderer.modelRenderer().upload(makeSkin(2,model::AlphaMode::Blend));
    const auto ordered=captureBatch(renderer,blend,32,false,0,true,true);
    const auto merged=captureBatch(renderer,blend,32,true,0,true,true);
    if(ordered.pixels!=merged.pixels) throw std::runtime_error("blend batching changed depth order");
    const auto splitBlend=captureBatch(renderer,blend,32,true,0,true,true,true);
    if(ordered.pixels!=splitBlend.pixels) throw std::runtime_error("multiple flushes lost global transparent ordering");
    // clear waits for outstanding frames, increments the generation, and stale
    // handles must not select the replacement resource occupying the same slot.
    renderer.modelRenderer().clear();
    const auto replacement=renderer.modelRenderer().upload(makeSkin(1));
    if(replacement==one) throw std::runtime_error("model resource generation was reused");
    const auto stale=captureBatch(renderer,one,1,true);
    if(stale.stats.modelDrawCalls) throw std::runtime_error("stale model handle remained drawable");
    renderer.modelRenderer().setBatching(true);
}
